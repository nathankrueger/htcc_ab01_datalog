#include "Arduino.h"
#include "LoRaWan_APP.h"
#include "hw.h"
#include <Adafruit_BME280.h>
#include "packets.h"
#include "radio.h"
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

/* ─── Configuration ──────────────────────────────────────────────────────── */

#ifndef NODE_ID
#define NODE_ID                  "ab01"
#endif

/*
 * Timing / Power Configuration
 *
 * CYCLE_PERIOD_MS: Total time for one TX→RX→Sleep cycle
 * rxDutyPercent: Percentage of cycle spent listening for commands (0-100)
 *   - Higher = more reliable command reception, more power consumption
 *   - Lower  = less power, commands may require more gateway retries
 *   - 0      = TX-only mode, never listen for commands
 *   - Can be changed at runtime via "rxduty" command
 *
 * Derived at runtime:
 *   rxWindowMs = (CYCLE_PERIOD_MS - TX_TIME_MS) * rxDutyPercent / 100
 */
#ifndef CYCLE_PERIOD_MS
#define CYCLE_PERIOD_MS          5000         /* Total cycle time */
#endif

#ifndef RX_DUTY_PERCENT_DEFAULT
#define RX_DUTY_PERCENT_DEFAULT  90           /* 0-100, initial value */
#endif

#define TX_TIME_MS               200          /* Estimated TX time */

#ifndef LED_BRIGHTNESS
#define LED_BRIGHTNESS           128          /* 0-255, default brightness */
#endif

/* TX power for normal sensor operation (see radio.h for limits) */
#define TX_OUTPUT_POWER          DEFAULT_TX_POWER

/*
 * Sensor-class IDs are assigned by alphabetical sort of the Python class names
 * at import time in sensors/__init__.py.  Current registry:
 *   0 = BME280TempPressureHumidity
 *   1 = MMA8452Accelerometer
 * If you add a new sensor class on the Python side, re-derive these.
 */
#define SENSOR_ID_BME280         0

/* CRC-32, Reading, buildSensorPacket, CommandPacket, parseCommand,
 * buildAckPacket are all in packets.h */

/* ─── Globals ────────────────────────────────────────────────────────────── */

static RadioEvents_t radioEvents;
static Adafruit_BME280 bme;
static bool bmeOk = false;

/* RX duty cycle - adjustable at runtime via "rxduty" command */
static int rxDutyPercent = RX_DUTY_PERCENT_DEFAULT;

/* TX power - adjustable at runtime via "txpwr" command */
static int8_t txPower = TX_OUTPUT_POWER;

/* Last processed command ID for duplicate detection */
static char lastCommandId[32] = "";

/* Cached ACK for duplicate handling - corresponds to lastCommandId */
static char lastAckBuf[LORA_MAX_PAYLOAD];
static int  lastAckLen = 0;

/* Calculate RX window duration based on current duty cycle */
static inline unsigned long getRxWindowMs(void)
{
    if (rxDutyPercent <= 0) return 0;
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

static float c_to_f(float c) { return c * 9.0f / 5.0f + 32.0f; }

/* ─── Command Handlers ───────────────────────────────────────────────────── */

static void handlePing(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count)
{
    DBGLN("PING received");
}

static void handleBlink(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count)
{
    /* Require at least the color argument */
    if (arg_count < 1) {
        DBGLN("BLINK: missing color argument");
        return;
    }

    /* Parse color */
    LEDColor color = parseColor(args[0]);
    DBG("BLINK: color=%s", args[0]);

    /* Parse optional seconds argument (default 0.5s) */
    float seconds = 0.5f;
    if (arg_count >= 2) {
        seconds = strtof(args[1], NULL);
        if (seconds <= 0.0f) {
            DBGLN(" ERROR: invalid seconds value");
            return;
        }
    }
    /* Parse optional brightness argument (default LED_BRIGHTNESS) */
    uint8_t brightness = LED_BRIGHTNESS;
    if (arg_count >= 3) {
        int val = atoi(args[2]);
        if (val < 0 || val > 255) {
            DBGLN(" ERROR: brightness must be 0-255");
            return;
        }
        brightness = (uint8_t)val;
    }
    DBG(" seconds=%.2f brightness=%d\n", seconds, brightness);

    /* Blink the LED */
    ledSetColorBrightness(color, brightness);
    delay((unsigned long)(seconds * 1000.0f));
    ledSetColor(LED_OFF);
}

static void handleRxDuty(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count)
{
    if (arg_count < 1) {
        DBG("RXDUTY: current=%d%% (window=%lums)\n",
                      rxDutyPercent, getRxWindowMs());
        return;
    }

    int newDuty = atoi(args[0]);
    if (newDuty < 0 || newDuty > 100) {
        DBG("RXDUTY: ERROR value %d out of range (0-100)\n", newDuty);
        return;
    }

    int oldDuty = rxDutyPercent;
    rxDutyPercent = newDuty;
    DBG("RXDUTY: %d%% -> %d%% (window=%lums)\n",
                  oldDuty, rxDutyPercent, getRxWindowMs());
}

/* Helper to apply TX config with current txPower */
static void applyTxConfig(void)
{
    Radio.SetTxConfig(MODEM_LORA, txPower, 0, LORA_BANDWIDTH,
                      LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                      LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                      true, 0, 0, LORA_IQ_INVERSION_ON, 3000);
}

static void handleTxPwr(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count)
{
    if (arg_count < 1) {
        DBG("TXPWR: current=%d dBm\n", txPower);
        return;
    }

    int newPower = atoi(args[0]);
    if (newPower < -17 || newPower > 22) {
        DBG("TXPWR: ERROR value %d out of range (-17 to 22)\n", newPower);
        return;
    }

    int8_t oldPower = txPower;
    txPower = (int8_t)newPower;
    applyTxConfig();
    DBG("TXPWR: %d dBm -> %d dBm\n", oldPower, txPower);
}

static void handleParams(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count)
{
    /*
     * Write current params to response buffer - gateway receives in ACK payload.
     * IMPORTANT: Keys must be in alphabetical order for CRC to match Python's
     * json.dumps(sort_keys=True). Order: rxduty < txpwr
     */
    snprintf(cmdResponseBuf, CMD_RESPONSE_BUF_SIZE,
             "{\"rxduty\":%d,\"txpwr\":%d}",
             rxDutyPercent, txPower);
    DBG("PARAMS: responding with %s\n", cmdResponseBuf);
}

/* ─── Radio Callbacks ────────────────────────────────────────────────────── */

static void onTxDone(void)
{
    txDone = true;
    Radio.Sleep();
}

static void onRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    DBG("RX: Got packet! size=%d rssi=%d snr=%d\n", size, rssi, snr);
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

    /* BME280 — try both common I2C addresses */
    bmeOk = bme.begin(0x76);
    if (!bmeOk) bmeOk = bme.begin(0x77);
    if (!bmeOk) DBGLN("ERROR: BME280 not found on 0x76 or 0x77");

    /* LoRa — register callbacks */
    radioEvents.TxDone = onTxDone;
    radioEvents.RxDone = onRxDone;
    radioEvents.RxTimeout = onRxTimeout;
    radioEvents.RxError = onRxError;

    Radio.Init(&radioEvents);
    Radio.SetChannel(RF_N2G_FREQUENCY);  /* Start on N2G for sensor TX */

    /* TX config */
    applyTxConfig();

    /* RX config - CRC enabled to match gateway */
    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                      LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                      LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                      0, true, 0, 0, LORA_IQ_INVERSION_ON, true);

    /* Command registry */
    cmdRegistryInit(&cmdRegistry, NODE_ID);
    cmdRegister(&cmdRegistry, "ping",   handlePing,   CMD_SCOPE_ANY, true);
    cmdRegister(&cmdRegistry, "blink",  handleBlink,  CMD_SCOPE_ANY, true);   /* slow! */
    cmdRegister(&cmdRegistry, "rxduty", handleRxDuty, CMD_SCOPE_ANY, true);
    cmdRegister(&cmdRegistry, "txpwr",  handleTxPwr,  CMD_SCOPE_ANY, true);
    cmdRegister(&cmdRegistry, "params", handleParams, CMD_SCOPE_ANY, false);  /* returns data */

    DBG("Initialization complete for Node: %s\n", NODE_ID);

    /* Test LED */
    // ledTest();
}

void loop(void)
{
    unsigned long cycleStart = millis();

    if (!bmeOk) {
        DBGLN("BME280 not available — retrying...");
        bmeOk = bme.begin(0x76);
        if (!bmeOk) bmeOk = bme.begin(0x77);
        delay(CYCLE_PERIOD_MS);
        return;
    }

    /* ── Read sensor ── */
    float tempF   = c_to_f(bme.readTemperature());   /* °C → °F */
    float pressure = bme.readPressure() / 100.0f;    /* Pa  → hPa */
    float humidity = bme.readHumidity();

    /* Guard against NaN / Inf from a bad read — %g would produce non-JSON */
    if (isnan(tempF) || isinf(tempF) ||
        isnan(pressure) || isinf(pressure) ||
        isnan(humidity) || isinf(humidity)) {
        DBGLN("ERROR: BME280 returned NaN/Inf, skipping");
        delay(CYCLE_PERIOD_MS);
        return;
    }

    /* Round to match BME280 output precision */
    tempF     = roundf(tempF     * 10.0f)  / 10.0f;   /* 1 dp */
    pressure  = roundf(pressure  * 100.0f) / 100.0f;  /* 2 dp */
    humidity  = roundf(humidity  * 10.0f)  / 10.0f;   /* 1 dp */

    DBG("T=%.1f F  P=%.2f hPa  H=%.1f %%\n",
                  tempF, pressure, humidity);

    /*
     * Reading descriptors.
     *
     * Units must use JSON \uXXXX escapes for any non-ASCII characters so the
     * CRC matches Python's json.dumps(..., ensure_ascii=True).
     * The degree sign ° (U+00B0) becomes \u00b0 in the JSON wire bytes.
     * In this C string literal the backslash is escaped once: "\\u00b0F".
     */
    Reading readings[] = {
        { "Temperature", SENSOR_ID_BME280, "\\u00b0F", tempF    },
        { "Pressure",    SENSOR_ID_BME280, "hPa",      pressure },
        { "Humidity",    SENSOR_ID_BME280, "%",        humidity  },
    };
    const int NUM_READINGS = 3;

    /*
     * Send with automatic packet splitting.
     *
     * All three BME280 readings fit comfortably in one packet (~173 bytes for
     * a short NODE_ID), but the loop handles the general case: it greedily
     * packs as many readings as possible into each packet while staying at or
     * below LORA_MAX_PAYLOAD.
     *
     * TODO: replace the timestamp (currently 0) with a real Unix epoch.
     *       The gateway passes it through to the dashboard unchanged, so 0
     *       will show up as 1970-01-01.  Add NTP sync or an RTC module to fix.
     */
    char pkt[LORA_MAX_PAYLOAD + 1];
    int  start = 0;

    while (start < NUM_READINGS) {
        int end  = NUM_READINGS;
        int pLen = 0;

        /* shrink the window until the packet fits */
        while (end > start) {
            pLen = buildSensorPacket(pkt, sizeof(pkt),
                                     NODE_ID, 0u,
                                     &readings[start], end - start);
            if (pLen > 0 && pLen <= LORA_MAX_PAYLOAD) break;
            end--;
        }

        if (end == start) {
            DBG("ERROR: reading \"%s\" alone exceeds max payload\n",
                          readings[start].name);
            start++;
            continue;
        }

        /* Send and wait for TX completion */
        txDone = false;
        Radio.Send((uint8_t *)pkt, pLen);
        DBG("Sent %d/%d readings [%d bytes]\n",
                      end - start, NUM_READINGS, pLen);

        /* Wait for TX to complete (with timeout) */
        unsigned long txStart = millis();
        while (!txDone && (millis() - txStart) < 3000) {
            Radio.IrqProcess();
            delay(1);
        }

        start = end;
        if (start < NUM_READINGS)
            delay(100);   /* brief gap between split packets */
    }

    /* ── Open RX window for commands on G2N channel ── */
    unsigned long rxWindowMs = getRxWindowMs();

    if (rxWindowMs > 0) {
        DBG("Opening RX window for %lu ms on G2N (%.1f MHz)...\n",
                      rxWindowMs, RF_G2N_FREQUENCY / 1e6);

        /* Switch to G2N channel for command reception */
        Radio.Sleep();
        Radio.SetChannel(RF_G2N_FREQUENCY);

        rxDone = false;
        rxLen = 0;

        /* Use continuous RX mode - doesn't stop on packet reception */
        Radio.Rx(0);

        unsigned long rxStart = millis();
        bool commandReceived = false;

        while ((millis() - rxStart) < rxWindowMs && !commandReceived) {
            Radio.IrqProcess();

            if (rxDone) {
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
                if (parseCommand(jsonStart, jsonLen, &cmd)) {
                    DBG("RX: Valid command parsed: %s\n", cmd.cmd);
                    /* Check if for us (or broadcast) */
                    if (cmd.node_id[0] == '\0' || strcmp(cmd.node_id, NODE_ID) == 0) {
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

                        /* Look up handler to check earlyAck flag */
                        CommandHandler *handler = cmdLookup(&cmdRegistry, &cmd);
                        bool useEarlyAck = (handler == NULL || handler->earlyAck);

                        /* For earlyAck handlers, send ACK before dispatch (and cache it) */
                        if (useEarlyAck && !isDuplicate) {
                            lastAckLen = buildAckPacket(lastAckBuf, sizeof(lastAckBuf),
                                                        cmd.timestamp, cmd.crc, NODE_ID);
                            if (lastAckLen > 0) {
                                Radio.Sleep();
                                Radio.SetChannel(RF_N2G_FREQUENCY);
                                txDone = false;
                                Radio.Send((uint8_t *)lastAckBuf, lastAckLen);
                                DBG("ACK sent on N2G [%d bytes]\n", lastAckLen);

                                unsigned long ackStart = millis();
                                while (!txDone && (millis() - ackStart) < 1000) {
                                    Radio.IrqProcess();
                                    delay(1);
                                }
                                Radio.Sleep();
                                Radio.SetChannel(RF_G2N_FREQUENCY);
                                Radio.Rx(0);
                            }
                        }

                        /* Dispatch to registered handlers (skip duplicates) */
                        if (isDuplicate) {
                            /*
                             * Safety: isDuplicate is only true when commandId matches
                             * lastCommandId, so lastAckBuf was built for this exact command.
                             */
                            DBG("CMD: Duplicate %s, resending cached ACK\n", commandId);

                            /* Resend the cached ACK (handles both early and late ACK cases) */
                            if (lastAckLen > 0) {
                                Radio.Sleep();
                                Radio.SetChannel(RF_N2G_FREQUENCY);
                                txDone = false;
                                Radio.Send((uint8_t *)lastAckBuf, lastAckLen);
                                DBG("Cached ACK resent [%d bytes]\n", lastAckLen);

                                unsigned long ackStart = millis();
                                while (!txDone && (millis() - ackStart) < 1000) {
                                    Radio.IrqProcess();
                                    delay(1);
                                }
                                Radio.Sleep();
                                Radio.SetChannel(RF_G2N_FREQUENCY);
                                Radio.Rx(0);
                            }
                        } else {
                            /* New command - update dedup tracking */
                            strncpy(lastCommandId, commandId,
                                    sizeof(lastCommandId) - 1);
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
                                                                       NODE_ID, cmdResponseBuf);
                                if (lastAckLen > 0) {
                                    Radio.Sleep();
                                    Radio.SetChannel(RF_N2G_FREQUENCY);
                                    txDone = false;
                                    Radio.Send((uint8_t *)lastAckBuf, lastAckLen);
                                    DBG("ACK+payload sent on N2G [%d bytes]\n", lastAckLen);

                                    unsigned long ackStart = millis();
                                    while (!txDone && (millis() - ackStart) < 1000) {
                                        Radio.IrqProcess();
                                        delay(1);
                                    }
                                    Radio.Sleep();
                                    Radio.SetChannel(RF_G2N_FREQUENCY);
                                    Radio.Rx(0);
                                }
                            }
                        }
                        commandReceived = true;  /* Exit loop after handling command */
                    } else {
                        DBG("RX: Command not for us (node_id='%s', our id='%s')\n",
                                      cmd.node_id, NODE_ID);
                    }
                } else {
                    DBGLN("RX: Not a command packet, continuing to listen...");
                }

                /* Reset flags - radio stays in continuous RX mode */
                rxDone = false;
                rxLen = 0;
            }

            delay(1);
        }

        if (!commandReceived) {
            DBGLN("RX: Window closed, no command received");
        }

        /* Switch back to N2G for next sensor TX */
        Radio.Sleep();
        Radio.SetChannel(RF_N2G_FREQUENCY);
    } else {
        DBGLN("RX disabled (rxDutyPercent=0)");
        Radio.Sleep();
    }

    /* Wait remaining cycle time */
    unsigned long elapsed = millis() - cycleStart;
    if (elapsed < CYCLE_PERIOD_MS) {
        delay(CYCLE_PERIOD_MS - elapsed);
    }
}
