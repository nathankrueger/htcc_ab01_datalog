#include "Arduino.h"
#include "LoRaWan_APP.h"
#include "hw.h"
#include <Adafruit_BME280.h>
#include "packets.h"

/* ─── Configuration ──────────────────────────────────────────────────────── */

#ifndef NODE_ID
#define NODE_ID                  "ab01"
#endif

#ifndef SEND_INTERVAL_MS
#define SEND_INTERVAL_MS        5000/* 5 s between sensor reads */
#endif

#define SEND_INTERVAL_MS         5000      
#define RF_FREQUENCY             915000000  /* Hz */
#define TX_OUTPUT_POWER          14         /* dBm */
#define LORA_BANDWIDTH           0          /* 0 = 125 kHz */
#define LORA_SPREADING_FACTOR    7          /* SF7 */
#define LORA_CODINGRATE          1          /* 4/5 */
#define LORA_PREAMBLE_LENGTH     8
#define LORA_SYMBOL_TIMEOUT      0
#define LORA_FIX_LENGTH_PAYLOAD_ON  false
#define LORA_IQ_INVERSION_ON     false
#define RX_WINDOW_MS             500        /* RX window after each TX */

/* Must match LORA_MAX_PAYLOAD in utils/protocol.py */
#define LORA_MAX_PAYLOAD         250

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

/* ─── LED Pin Definitions ────────────────────────────────────────────────── */

#define RGB_RED    GPIO0
#define RGB_GREEN  GPIO1
#define RGB_BLUE   GPIO2

typedef enum {
    LED_OFF = 0,
    LED_RED,
    LED_GREEN,
    LED_BLUE,
    LED_YELLOW,      // Red + Green
    LED_CYAN,        // Green + Blue
    LED_MAGENTA,     // Red + Blue
    LED_WHITE        // All on
} LEDColor;

/* ─── Globals ────────────────────────────────────────────────────────────── */

static RadioEvents_t radioEvents;
static Adafruit_BME280 bme;
static bool bmeOk = false;

/* RX state */
static uint8_t rxBuffer[LORA_MAX_PAYLOAD + 1];
static volatile int rxLen = 0;
static volatile bool rxDone = false;
static volatile bool txDone = false;

/* Command registry */
static CommandRegistry cmdRegistry;

static float c_to_f(float c) { return c * 9.0f / 5.0f + 32.0f; }

/* ─── LED Control Functions ──────────────────────────────────────────────── */

static void ledSetRGB(bool red, bool green, bool blue)
{
    digitalWrite(RGB_RED, red ? HIGH : LOW);
    digitalWrite(RGB_GREEN, green ? HIGH : LOW);
    digitalWrite(RGB_BLUE, blue ? HIGH : LOW);
}

static void ledSetColor(LEDColor color)
{
    switch (color) {
        case LED_OFF:
            ledSetRGB(false, false, false);
            break;
        case LED_RED:
            ledSetRGB(true, false, false);
            break;
        case LED_GREEN:
            ledSetRGB(false, true, false);
            break;
        case LED_BLUE:
            ledSetRGB(false, false, true);
            break;
        case LED_YELLOW:
            ledSetRGB(true, true, false);
            break;
        case LED_CYAN:
            ledSetRGB(false, true, true);
            break;
        case LED_MAGENTA:
            ledSetRGB(true, false, true);
            break;
        case LED_WHITE:
            ledSetRGB(true, true, true);
            break;
    }
}

static void ledInit(void)
{
    pinMode(RGB_RED, OUTPUT);
    pinMode(RGB_GREEN, OUTPUT);
    pinMode(RGB_BLUE, OUTPUT);
    ledSetRGB(false, false, false);
}

/* ─── Command Handlers ───────────────────────────────────────────────────── */

static void handlePing(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count)
{
    Serial.println("PING received");
}

static LEDColor parseColor(const char *colorStr)
{
    if (strcasecmp(colorStr, "red") == 0 || strcasecmp(colorStr, "r") == 0)      return LED_RED;
    if (strcasecmp(colorStr, "green") == 0 || strcasecmp(colorStr, "g") == 0)    return LED_GREEN;
    if (strcasecmp(colorStr, "blue") == 0 || strcasecmp(colorStr, "b") == 0)     return LED_BLUE;
    if (strcasecmp(colorStr, "yellow") == 0 || strcasecmp(colorStr, "y") == 0)   return LED_YELLOW;
    if (strcasecmp(colorStr, "cyan") == 0 || strcasecmp(colorStr, "c") == 0)     return LED_CYAN;
    if (strcasecmp(colorStr, "magenta") == 0 || strcasecmp(colorStr, "m") == 0)  return LED_MAGENTA;
    if (strcasecmp(colorStr, "white") == 0 || strcasecmp(colorStr, "w") == 0)    return LED_WHITE;
    if (strcasecmp(colorStr, "off") == 0 || strcasecmp(colorStr, "o") == 0)      return LED_OFF;
    return LED_OFF;  /* default */
}

static void handleBlink(const char *cmd, char args[][CMD_MAX_ARG_LEN], int arg_count)
{
    /* Require at least the color argument */
    if (arg_count < 1) {
        Serial.println("BLINK: missing color argument");
        return;
    }

    /* Parse color */
    LEDColor color = parseColor(args[0]);
    Serial.printf("BLINK: color=%s", args[0]);

    /* Parse optional seconds argument (default 0.5s) */
    float seconds = 0.5f;
    if (arg_count >= 2) {
        seconds = strtof(args[1], NULL);
        if (seconds <= 0.0f) {
            Serial.println(" ERROR: invalid seconds value");
            return;
        }
    }
    Serial.printf(" seconds=%.2f\n", seconds);

    /* Blink the LED */
    ledSetColor(color);
    delay((unsigned long)(seconds * 1000.0f));
    ledSetColor(LED_OFF);
}

/* ─── Radio Callbacks ────────────────────────────────────────────────────── */

static void onTxDone(void)
{
    txDone = true;
    Radio.Sleep();
}

static void onRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    if (size > 0 && size <= LORA_MAX_PAYLOAD) {
        memcpy(rxBuffer, payload, size);
        rxLen = size;
        rxDone = true;
    }
    Radio.Sleep();
}

static void onRxTimeout(void)
{
    Radio.Sleep();
}

static void onRxError(void)
{
    Radio.Sleep();
}

/* ─── setup / loop ───────────────────────────────────────────────────────── */

void setup(void)
{
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW);          /* power on peripherals */

    Serial.begin(115200);
    boardInitMcu();

    /* LED initialization */
    ledInit();

    /* BME280 — try both common I2C addresses */
    bmeOk = bme.begin(0x76);
    if (!bmeOk) bmeOk = bme.begin(0x77);
    if (!bmeOk) Serial.println("ERROR: BME280 not found on 0x76 or 0x77");

    /* LoRa — register callbacks */
    radioEvents.TxDone = onTxDone;
    radioEvents.RxDone = onRxDone;
    radioEvents.RxTimeout = onRxTimeout;
    radioEvents.RxError = onRxError;

    Radio.Init(&radioEvents);
    Radio.SetChannel(RF_FREQUENCY);

    /* TX config */
    Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                      LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                      LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                      true, 0, 0, LORA_IQ_INVERSION_ON, 3000);

    /* RX config */
    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                      LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                      LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                      0, true, 0, 0, LORA_IQ_INVERSION_ON, true);

    /* Command registry */
    cmdRegistryInit(&cmdRegistry, NODE_ID);
    cmdRegister(&cmdRegistry, "ping", handlePing, CMD_SCOPE_ANY);
    cmdRegister(&cmdRegistry, "blink", handleBlink, CMD_SCOPE_ANY);
}

void loop(void)
{
    if (!bmeOk) {
        Serial.println("BME280 not available — retrying...");
        bmeOk = bme.begin(0x76);
        if (!bmeOk) bmeOk = bme.begin(0x77);
        delay(SEND_INTERVAL_MS);
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
        Serial.println("ERROR: BME280 returned NaN/Inf, skipping");
        delay(SEND_INTERVAL_MS);
        return;
    }

    /* Round to match BME280 output precision */
    tempF     = roundf(tempF     * 10.0f)  / 10.0f;   /* 1 dp */
    pressure  = roundf(pressure  * 100.0f) / 100.0f;  /* 2 dp */
    humidity  = roundf(humidity  * 10.0f)  / 10.0f;   /* 1 dp */

    Serial.printf("T=%.1f F  P=%.2f hPa  H=%.1f %%\n",
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
            Serial.printf("ERROR: reading \"%s\" alone exceeds max payload\n",
                          readings[start].name);
            start++;
            continue;
        }

        /* Send and wait for TX completion */
        txDone = false;
        Radio.Send((uint8_t *)pkt, pLen);
        Serial.printf("Sent %d/%d readings [%d bytes]\n",
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

    /* ── Open RX window for commands ── */
    rxDone = false;
    rxLen = 0;
    Radio.Rx(RX_WINDOW_MS);

    unsigned long rxStart = millis();
    while ((millis() - rxStart) < RX_WINDOW_MS + 50) {
        Radio.IrqProcess();

        if (rxDone) {
            CommandPacket cmd;
            if (parseCommand(rxBuffer, rxLen, &cmd)) {
                /* Check if for us (or broadcast) */
                if (cmd.node_id[0] == '\0' || strcmp(cmd.node_id, NODE_ID) == 0) {
                    Serial.printf("CMD: %s (from %s)\n",
                                  cmd.cmd,
                                  cmd.node_id[0] ? cmd.node_id : "broadcast");

                    /* Build and send ACK */
                    char ackBuf[128];
                    int ackLen = buildAckPacket(ackBuf, sizeof(ackBuf),
                                                cmd.timestamp, cmd.crc, NODE_ID);
                    if (ackLen > 0) {
                        txDone = false;
                        Radio.Send((uint8_t *)ackBuf, ackLen);
                        Serial.printf("ACK sent [%d bytes]\n", ackLen);

                        /* Wait for ACK TX completion */
                        unsigned long ackStart = millis();
                        while (!txDone && (millis() - ackStart) < 1000) {
                            Radio.IrqProcess();
                            delay(1);
                        }
                    }

                    /* Dispatch to registered handlers */
                    cmdDispatch(&cmdRegistry, &cmd);
                }
            }
            rxDone = false;
            rxLen = 0;
            break;  /* Exit RX window after handling command */
        }

        delay(1);
    }

    Radio.Sleep();

    /* Wait remaining interval time */
    unsigned long elapsed = millis() - rxStart;
    if (elapsed < SEND_INTERVAL_MS) {
        delay(SEND_INTERVAL_MS - elapsed);
    }
}
