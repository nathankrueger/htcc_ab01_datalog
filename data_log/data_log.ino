#include "Arduino.h"
#include "LoRaWan_APP.h"
#include "hw.h"

/* ─── CMD/ACK Debug (must be before packets.h) ─────────────────────────── */

#ifndef CMD_DEBUG
#define CMD_DEBUG 1  /* Enabled for debugging broadcast issue */
#endif

#if CMD_DEBUG
  #define CDBG(fmt, ...) Serial.printf("[%lu] " fmt, millis(), ##__VA_ARGS__)
#else
  #define CDBG(fmt, ...) ((void)0)
#endif

#include "packets.h"
#include "radio.h"
#include "config.h"
#include "commands.h"
#include "sensor_drv.h"
#include "bme280_sensor.h"
#include "batt_sensor.h"
#include "led.h"
#include "innerWdt.h"

/* ─── Debug Output ──────────────────────────────────────────────────────── */

#include "dbg.h"

/* ─── Configuration ──────────────────────────────────────────────────────── */

/* DEFAULT_NODE_ID, NODE_VERSION, TX_OUTPUT_POWER, RX_DUTY_PERCENT_DEFAULT,
 * and UPDATE_CFG are defined in config.h (overridable via Makefile). */

/*
 * Timing / Power Configuration
 *
 * CYCLE_PERIOD_MS: Total time for one TX→RX→Sleep cycle
 * rxDutyPercent: Percentage of cycle spent listening for commands (0-100)
 *   - Higher = more reliable command reception, more power consumption
 *   - Lower  = less power, commands may require more gateway retries
 *   - 0      = TX-only mode, never listen for commands
 *   - Can be changed at runtime via "setparam rxduty" command
 *
 * Derived at runtime:
 *   rxWindowMs = (CYCLE_PERIOD_MS - TX_TIME_MS) * rxDutyPercent / 100
 */
#ifndef CYCLE_PERIOD_MS
#define CYCLE_PERIOD_MS          5000         /* Total cycle time */
#endif

#define TX_TIME_MS               200          /* Estimated TX time */

#ifndef LED_BRIGHTNESS
#define LED_BRIGHTNESS           128          /* 0-255, default brightness */
#endif

/* CRC-32, Reading, buildSensorPacket, CommandPacket, parseCommand,
 * buildAckPacket are all in packets.h */

/* ─── Globals ────────────────────────────────────────────────────────────── */

static RadioEvents_t radioEvents;

/* Globals shared with commands.cpp (declared extern in commands.h) */
NodeConfig    cfg;
char          nodeId[16];     /* Runtime node ID — loaded from EEPROM, separate from cfg */
uint8_t       rxDutyPercent;
int8_t        txPower;
uint8_t       spreadFactor;   /* SF7-SF12 */
uint8_t       loraBW;         /* 0=125kHz, 1=250kHz, 2=500kHz */
uint32_t      n2gFreqHz;      /* Node-to-Gateway frequency (Hz) */
uint32_t      g2nFreqHz;      /* Gateway-to-Node frequency (Hz) */
uint16_t      broadcastAckJitterMs; /* Max jitter before ACK (0=off) */
uint16_t      bme280RateSec;  /* BME280 sample interval (seconds) */
uint16_t      battRateSec;    /* Battery sample interval (seconds) */
uint16_t      forceSampleCount = 0; /* >0: force all sensors to sample, decrement each cycle */
bool          blinkActive  = false;
unsigned long blinkOffTime = 0;

/* Last processed command ID for duplicate detection */
static char lastCommandId[32] = "";

/* Cached ACK for duplicate handling - corresponds to lastCommandId */
static char lastAckBuf[LORA_MAX_PAYLOAD];
static int  lastAckLen = 0;

/* Calculate RX window duration based on current duty cycle */
static inline unsigned long getRxWindowMs(void)
{
    if (rxDutyPercent == 0) return 0;
    if (rxDutyPercent > 100) return CYCLE_PERIOD_MS - TX_TIME_MS;
    return ((unsigned long)(CYCLE_PERIOD_MS - TX_TIME_MS) * rxDutyPercent) / 100;
}

/* RX state */
static uint8_t rxBuffer[LORA_MAX_PAYLOAD + 1];
static volatile int rxLen = 0;
static volatile bool rxDone = false;
static volatile bool txDone = false;
int16_t lastRxRssi = 0;  /* RSSI of last received packet (shared with commands.cpp) */

/* Command registry */
static CommandRegistry cmdRegistry;

/* ─── Radio Callbacks ────────────────────────────────────────────────────── */

static void onTxDone(void)
{
    txDone = true;
    Radio.Sleep();
}

static void onRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    DBG("RX: Got packet! size=%d rssi=%d snr=%d\n", size, rssi, snr);
    CDBG("RX_PKT size=%d rssi=%d\n", size, rssi);
    if (size > 0 && size <= LORA_MAX_PAYLOAD) {
        memcpy(rxBuffer, payload, size);
        rxLen = size;
        rxDone = true;
        lastRxRssi = rssi;

        /* Try to print as string */
        DBG("RX: Payload: %.*s\n", size, payload);

        /* Also print first 32 bytes as hex for debugging */
        DBGP("RX: Hex: ");
        for (int i = 0; i < size && i < 32; i++) {
            DBG("%02x ", payload[i]);
        }
        if (size > 32) DBGP("...");
        DBGLN();
    } else {
        DBG("RX: Invalid size %d\n", size);
    }
    Radio.Sleep();
}

static void onRxTimeout(void)
{
    DBGLN("RX: Timeout");
    Radio.Sleep();
}

static void onRxError(void)
{
    DBGLN("RX: Error");
    Radio.Sleep();
}

/* ─── ACK + RX Resume Helper ─────────────────────────────────────────────── */

/*
 * Send an ACK buffer on N2G, wait for TX done, then switch back to G2N RX.
 * Used by handleRxPacket() to avoid repeating this pattern 3 times.
 */
static void sendAckAndResumeRx(const char *buf, int len, const char *label,
                               bool addJitter = false)
{
    if (addJitter && broadcastAckJitterMs > 0) {
        unsigned long jitter = random(1, broadcastAckJitterMs);
        DBG("Broadcast ACK jitter: %lums\n", jitter);
        sleepWdt(jitter);
    }
    Radio.Sleep();
    Radio.SetChannel(n2gFreqHz);
    txDone = false;
    Radio.Send((uint8_t *)buf, len);
    DBG("%s [%d bytes]\n", label, len);
    CDBG("ACK_TX %s bytes=%d\n", label, len);

    unsigned long ackStart = millis();
    while (!txDone && (millis() - ackStart) < 1000) {
        Radio.IrqProcess();
        feedInnerWdt();
        delay(1);
    }
    Radio.Sleep();
    Radio.SetChannel(g2nFreqHz);
    Radio.Rx(0);
}

/* ─── RX Packet Handler ─────────────────────────────────────────────────── */

/*
 * Process a received packet from the RX buffer.
 * Parses command, handles dedup, sends ACK, dispatches handler,
 * and resumes RX on G2N.  Called from the tick loop when rxDone is set.
 */
static void handleRxPacket(void)
{
    DBGLN("RX: Processing received packet");

    /* Skip padding bytes - find first '{' character */
    uint8_t *jsonStart = rxBuffer;
    int jsonLen = rxLen;
    for (int i = 0; i < rxLen && i < 8; i++) {
        if (rxBuffer[i] == '{') {
            jsonStart = &rxBuffer[i];
            jsonLen = rxLen - i;
            break;
        }
    }

    CommandPacket cmd;
    if (!parseCommand(jsonStart, jsonLen, &cmd)) {
        DBGLN("RX: Not a command packet, continuing to listen...");
        CDBG("RX_DROP\n");
        return;
    }

    DBG("RX: Valid command parsed: %s\n", cmd.cmd);

    /* Check if for us (or broadcast) */
    if (cmd.node_id[0] != '\0' && strcmp(cmd.node_id, nodeId) != 0) {
        DBG("RX: Command not for us (node_id='%s', our id='%s')\n",
                      cmd.node_id, nodeId);
        CDBG("RX_NOTME node=%s\n", cmd.node_id);
        return;
    }

    /* Build command ID for dedup check */
    char commandId[32];
    snprintf(commandId, sizeof(commandId), "%u_%.4s",
             cmd.timestamp, cmd.crc);
    bool isDuplicate = (strcmp(commandId, lastCommandId) == 0);

    DBG("CMD: %s (from %s, id=%s%s)\n",
                  cmd.cmd,
                  cmd.node_id[0] ? cmd.node_id : "broadcast",
                  commandId,
                  isDuplicate ? ", DUP" : "");
    CDBG("CMD cmd=%s id=%s dup=%s\n",
         cmd.cmd, commandId, isDuplicate ? "Y" : "N");

    /* Look up handler to check earlyAck flag */
    CommandHandler *handler = cmdLookup(&cmdRegistry, &cmd);
    bool useEarlyAck = (handler == NULL || handler->earlyAck);
    /* Add jitter for ALL broadcast responses to prevent ACK collisions */
    bool is_broadcast = (cmd.node_id[0] == '\0');
    bool addJitter = is_broadcast && broadcastAckJitterMs > 0;

    /* For earlyAck handlers, send ACK before dispatch (and cache it) */
    if (useEarlyAck && !isDuplicate) {
        lastAckLen = buildAckPacket(lastAckBuf, sizeof(lastAckBuf),
                                    cmd.timestamp, cmd.crc, nodeId);
        if (lastAckLen > 0)
            sendAckAndResumeRx(lastAckBuf, lastAckLen, "ACK sent on N2G", addJitter);
    }

    /* Dispatch to registered handlers (skip duplicates) */
    if (isDuplicate) {
        DBG("CMD: Duplicate %s, resending cached ACK\n", commandId);
        if (lastAckLen > 0)
            sendAckAndResumeRx(lastAckBuf, lastAckLen, "Cached ACK resent", addJitter);
    } else {
        /* New command - update dedup tracking */
        strncpy(lastCommandId, commandId, sizeof(lastCommandId) - 1);
        lastCommandId[sizeof(lastCommandId) - 1] = '\0';

        /* Clear response buffer before dispatch */
        cmdResponseBuf[0] = '\0';

        if (handler != NULL) {
            handler->callback(cmd.cmd,
                              (char (*)[CMD_MAX_ARG_LEN])cmd.args,
                              cmd.arg_count);
        }

        /* For late-ACK handlers, send ACK with response after dispatch */
        if (!useEarlyAck) {
            lastAckLen = buildAckPacketWithPayload(lastAckBuf, sizeof(lastAckBuf),
                                                   cmd.timestamp, cmd.crc,
                                                   nodeId, cmdResponseBuf);
            if (lastAckLen > 0)
                sendAckAndResumeRx(lastAckBuf, lastAckLen, "ACK+payload sent on N2G", addJitter);
        }
    }
}

/* ─── setup / loop ───────────────────────────────────────────────────────── */

void setup(void)
{
    /* Kill RGB data pin before powering peripherals */
    pinMode(RGB, OUTPUT);
    digitalWrite(RGB, LOW);

    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW);          /* power on peripherals */
    delay(1);                         /* let Vext rail stabilise */

    /* Immediately clear the NeoPixel before it latches garbage */
    ledInit();

    Serial.begin(115200);

    /* Load config from EEPROM (or compile-time defaults on first boot).
     * When UPDATE_CFG=1, compile-time values are written to EEPROM.
     * Node ID is in a separate unversioned EEPROM region — survives
     * CFG_VERSION bumps.  Use WRITE_NODE_ID=abXX to set it. */
    cfgLoad(&cfg);
    cfgLoadNodeId(nodeId);
#ifdef WRITE_NODE_ID
    cfgSaveNodeId(WRITE_NODE_ID);
    cfgLoadNodeId(nodeId);
#endif
    rxDutyPercent = cfg.rxDutyPercent;
    txPower       = cfg.txOutputPower;
    spreadFactor  = cfg.spreadingFactor;
    loraBW        = cfg.bandwidth;
    n2gFreqHz     = cfg.n2gFrequencyHz;
    g2nFreqHz     = cfg.g2nFrequencyHz;
    broadcastAckJitterMs = cfg.broadcastAckJitterMs;
    bme280RateSec = cfg.bme280RateSec;
    battRateSec   = cfg.battRateSec;

    /* Sensor drivers — register enabled sensors, then init all */
#ifdef SENSOR_BME280
    sensorRegister(&bme280Driver);
#endif
#ifdef SENSOR_BATT
    sensorRegister(&battDriver);
#endif
    sensorInitAll();

    /* LoRa — register callbacks */
    radioEvents.TxDone = onTxDone;
    radioEvents.RxDone = onRxDone;
    radioEvents.RxTimeout = onRxTimeout;
    radioEvents.RxError = onRxError;

    Radio.Init(&radioEvents);
    Radio.SetChannel(n2gFreqHz);  /* Start on N2G for sensor TX */

    /* Apply TX + RX config from runtime params */
    applyTxConfig();
    applyRxConfig();

    /* Seed PRNG — combine ADC noise, micros() jitter, and radio RSSI */
    {
        int16_t rssi = Radio.Rssi(MODEM_LORA);
        uint32_t seed = (uint32_t)analogRead(ADC) ^ micros() ^ (uint16_t)rssi;
        randomSeed(seed);
        DBG("PRNG seed: adc=%d micros=%lu rssi=%d -> 0x%08lX\n",
            analogRead(ADC), micros(), rssi, seed);
    }

    /* Command registry */
    cmdRegistryInit(&cmdRegistry, nodeId);
    commandsInit(&cmdRegistry);

    /* Watchdog timer — manual feed; resets MCU if main loop stalls.
     * PSoC4 ILO ~32 kHz, match=0xFFFF → ~2s per match.
     * Reset after two missed clears → ~4s total timeout.
     * feedInnerWdt() must be called in every busy-wait loop. */
    innerWdtEnable(false);

    DBG("Initialization complete for Node: %s (v%u, tx=%ddBm, rxduty=%d%%)\n",
        nodeId, (unsigned)NODE_VERSION, txPower, rxDutyPercent);
}

void loop(void)
{
    unsigned long cycleStart = millis();

    /* ── Force sample: reset all sensor timers if requested ── */
    if (forceSampleCount > 0) {
        sensorResetTimers();
        forceSampleCount--;
        DBG("Force sample: %u remaining\n", (unsigned)forceSampleCount);
    }

    /* ── Poll sensors — each driver checks its own interval ── */
    Reading readings[SENSOR_MAX_READINGS];
    int nRead = sensorPoll(cycleStart, readings, SENSOR_MAX_READINGS);

    if (nRead > 0) {
        /* ── Build and send sensor packets ── */
        char pkt[LORA_MAX_PAYLOAD + 1];
        int offset = 0;

        while (offset < nRead) {
            int nextOffset;
            int pLen = sensorPack(nodeId, readings, nRead,
                                  offset, &nextOffset, pkt, sizeof(pkt));
            if (pLen == 0) {
                DBG("ERROR: reading \"%s\" alone exceeds max payload\n",
                    readings[offset].name);
                offset = nextOffset;
                continue;
            }

            /* Send and wait for TX completion */
            txDone = false;
            Radio.Send((uint8_t *)pkt, pLen);
            DBG("Sent %d/%d readings [%d bytes]\n",
                nextOffset - offset, nRead, pLen);

            unsigned long txStart = millis();
            while (!txDone && (millis() - txStart) < 3000) {
                Radio.IrqProcess();
                feedInnerWdt();
                delay(1);
            }

            offset = nextOffset;
            if (offset < nRead)
                delay(100);   /* brief gap between split packets */
        }
    }

    /* ── Tick loop: RX + housekeeping until cycle ends ── */
    unsigned long rxWindowMs = getRxWindowMs();
    unsigned long rxDeadline = cycleStart + TX_TIME_MS + rxWindowMs;
    bool radioListening = false;

    /* Start RX on G2N if duty cycle allows */
    if (rxWindowMs > 0) {
        DBG("Opening RX window for %lu ms on G2N (%.1f MHz)...\n",
                      rxWindowMs, g2nFreqHz / 1e6);
        CDBG("RX_OPEN dur=%lums\n", rxWindowMs);
        Radio.Sleep();
        Radio.SetChannel(g2nFreqHz);
        rxDone = false;
        rxLen = 0;
        Radio.Rx(0);
        radioListening = true;
    } else {
        DBGLN("RX disabled (rxDutyPercent=0)");
        Radio.Sleep();
    }

    while (millis() - cycleStart < CYCLE_PERIOD_MS) {
        Radio.IrqProcess();
        feedInnerWdt();

        /* Handle received packet */
        if (rxDone) {
            handleRxPacket();
            rxDone = false;
            rxLen = 0;
            /* onRxDone() calls Radio.Sleep(); re-enter RX if window still open */
            if (radioListening) Radio.Rx(0);
        }

        /* Stop radio after RX window expires */
        if (radioListening && millis() >= rxDeadline) {
            DBGLN("RX: Window closed");
            CDBG("RX_CLOSE\n");
            Radio.Sleep();
            radioListening = false;
        }

        /* Non-blocking blink: turn off LED when timer expires */
        if (blinkActive && millis() >= blinkOffTime) {
            ledOff();
            blinkActive = false;
        }

        /* Fast-cycle if more forced samples are pending */
        if (forceSampleCount > 0) {
            DBG("Force sample pending, ending cycle early\n");
            break;
        }

        delay(1);
    }

    /* Ensure clean state for next cycle */
    if (radioListening) Radio.Sleep();
    Radio.SetChannel(n2gFreqHz);
}
