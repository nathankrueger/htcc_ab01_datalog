#include "Arduino.h"
#include "LoRaWan_APP.h"
#include "hw.h"
#include "packets.h"
#include "radio.h"
#include "config.h"
#include "commands.h"
#include "sensors.h"
#include "led.h"

/* ─── Debug Output ──────────────────────────────────────────────────────── */

#ifndef DEBUG
#define DEBUG 1
#endif

#if DEBUG
  #define DBG(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
  #define DBGLN(msg)     Serial.println(msg)
  #define DBGP(msg)      Serial.print(msg)
#else
  #define DBG(fmt, ...)  ((void)0)
  #define DBGLN(msg)     ((void)0)
  #define DBGP(msg)      ((void)0)
#endif

/* ─── CMD/ACK Debug ────────────────────────────────────────────────────── */

#ifndef CMD_DEBUG
#define CMD_DEBUG 0
#endif

#if CMD_DEBUG
  #define CDBG(fmt, ...) Serial.printf("[%lu] " fmt, millis(), ##__VA_ARGS__)
#else
  #define CDBG(fmt, ...) ((void)0)
#endif

/* ─── Configuration ──────────────────────────────────────────────────────── */

/* NODE_ID, NODE_VERSION, TX_OUTPUT_POWER, RX_DUTY_PERCENT_DEFAULT,
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

/* Max random delay (ms) before ACKing a broadcast command.
 * Prevents ACK collisions when multiple nodes respond simultaneously. */
#ifndef BROADCAST_ACK_JITTER_MS
#define BROADCAST_ACK_JITTER_MS  500
#endif

/* CRC-32, Reading, buildSensorPacket, CommandPacket, parseCommand,
 * buildAckPacket are all in packets.h */

/* ─── Globals ────────────────────────────────────────────────────────────── */

static RadioEvents_t radioEvents;

/* Globals shared with commands.cpp (declared extern in commands.h) */
NodeConfig    cfg;
uint8_t       rxDutyPercent;
int8_t        txPower;
uint8_t       spreadFactor;   /* SF7-SF12 */
uint8_t       loraBW;         /* 0=125kHz, 1=250kHz, 2=500kHz */
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
    if (addJitter && BROADCAST_ACK_JITTER_MS > 0) {
        unsigned long jitter = random(1, BROADCAST_ACK_JITTER_MS);
        DBG("Broadcast ACK jitter: %lums\n", jitter);
        delay(jitter);
    }
    Radio.Sleep();
    Radio.SetChannel(RF_N2G_FREQUENCY);
    txDone = false;
    Radio.Send((uint8_t *)buf, len);
    DBG("%s [%d bytes]\n", label, len);
    CDBG("ACK_TX %s bytes=%d\n", label, len);

    unsigned long ackStart = millis();
    while (!txDone && (millis() - ackStart) < 1000) {
        Radio.IrqProcess();
        delay(1);
    }
    Radio.Sleep();
    Radio.SetChannel(RF_G2N_FREQUENCY);
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
    if (cmd.node_id[0] != '\0' && strcmp(cmd.node_id, cfg.nodeId) != 0) {
        DBG("RX: Command not for us (node_id='%s', our id='%s')\n",
                      cmd.node_id, cfg.nodeId);
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

    /* Look up handler to check earlyAck / ackJitter flags */
    CommandHandler *handler = cmdLookup(&cmdRegistry, &cmd);
    bool useEarlyAck = (handler == NULL || handler->earlyAck);
    bool addJitter = (handler != NULL && handler->ackJitter);

    /* For earlyAck handlers, send ACK before dispatch (and cache it) */
    if (useEarlyAck && !isDuplicate) {
        lastAckLen = buildAckPacket(lastAckBuf, sizeof(lastAckBuf),
                                    cmd.timestamp, cmd.crc, cfg.nodeId);
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
                                                   cfg.nodeId, cmdResponseBuf);
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
     * When UPDATE_CFG=1, compile-time values are written to EEPROM. */
    cfgLoad(&cfg);
    rxDutyPercent = cfg.rxDutyPercent;
    txPower       = cfg.txOutputPower;
    spreadFactor  = cfg.spreadingFactor;
    loraBW        = cfg.bandwidth;

    /* BME280 sensor */
    sensorInit();

    /* LoRa — register callbacks */
    radioEvents.TxDone = onTxDone;
    radioEvents.RxDone = onRxDone;
    radioEvents.RxTimeout = onRxTimeout;
    radioEvents.RxError = onRxError;

    Radio.Init(&radioEvents);
    Radio.SetChannel(RF_N2G_FREQUENCY);  /* Start on N2G for sensor TX */

    /* Apply TX + RX config from runtime params */
    applyTxConfig();
    applyRxConfig();

    /* Command registry */
    cmdRegistryInit(&cmdRegistry, cfg.nodeId);
    commandsInit(&cmdRegistry);

    DBG("Initialization complete for Node: %s (v%u, tx=%ddBm, rxduty=%d%%)\n",
        cfg.nodeId, cfg.nodeVersion, txPower, rxDutyPercent);
}

void loop(void)
{
    unsigned long cycleStart = millis();

    /* ── Sample sensor ── */
    if (!sensorAvailable()) {
        DBGLN("BME280 not available — retrying...");
        sensorInit();
        delay(CYCLE_PERIOD_MS);
        return;
    }

    Reading readings[SENSOR_MAX_READINGS];
    int nRead = sensorRead(readings, SENSOR_MAX_READINGS);
    if (nRead <= 0) {
        DBGLN("ERROR: sensor read failed, skipping cycle");
        delay(CYCLE_PERIOD_MS);
        return;
    }

    /* ── Build and send sensor packets ── */
    char pkt[LORA_MAX_PAYLOAD + 1];
    int offset = 0;

    while (offset < nRead) {
        int nextOffset;
        int pLen = sensorPack(cfg.nodeId, readings, nRead,
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
            delay(1);
        }

        offset = nextOffset;
        if (offset < nRead)
            delay(100);   /* brief gap between split packets */
    }

    /* ── Tick loop: RX + housekeeping until cycle ends ── */
    unsigned long rxWindowMs = getRxWindowMs();
    unsigned long rxDeadline = cycleStart + TX_TIME_MS + rxWindowMs;
    bool radioListening = false;

    /* Start RX on G2N if duty cycle allows */
    if (rxWindowMs > 0) {
        DBG("Opening RX window for %lu ms on G2N (%.1f MHz)...\n",
                      rxWindowMs, RF_G2N_FREQUENCY / 1e6);
        CDBG("RX_OPEN dur=%lums\n", rxWindowMs);
        Radio.Sleep();
        Radio.SetChannel(RF_G2N_FREQUENCY);
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

        delay(1);
    }

    /* Ensure clean state for next cycle */
    if (radioListening) Radio.Sleep();
    Radio.SetChannel(RF_N2G_FREQUENCY);
}
