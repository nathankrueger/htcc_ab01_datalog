#include "Arduino.h"
#include "LoRaWan_APP.h"
#include "hw.h"
#include "packets.h"
#include "radio.h"
#include "led.h"
#include "HT_SSD1306Wire.h"
#include <TinyGPS++.h>

/* ─── Debug Output ──────────────────────────────────────────────────────── */

#ifndef DEBUG
#define DEBUG 1
#endif

/*
 * Serial is used for GPS on the AB01 (only one hardware UART).
 * Debug macros are no-ops — use the OLED for status.
 */
#define DBG(fmt, ...)  ((void)0)
#define DBGLN(msg)     ((void)0)
#define DBGP(msg)      ((void)0)

/* ─── Configuration ──────────────────────────────────────────────────────── */

#ifndef NODE_ID
#define NODE_ID "ab01"
#endif

#ifndef CYCLE_PERIOD_MS
#define CYCLE_PERIOD_MS          5000
#endif

#ifndef LED_BRIGHTNESS
#define LED_BRIGHTNESS           128
#endif

/* Use max TX power for range testing */
#define TX_OUTPUT_POWER          MAX_TX_POWER

/*
 * Sensor class ID for GPS readings.
 * Alphabetical sort: BME280=0, MMA8452=1, NEO6MGPS=2.
 */
#define SENSOR_ID_GPS            2

#define GPS_BAUD                 9600

/* ─── Hardware Objects ──────────────────────────────────────────────────── */

/* SSD1306 OLED 128x64, I2C addr 0x3C, no reset pin */
static SSD1306Wire oled(0x3c, 500000, SDA, SCL, GEOMETRY_128_64, -1);

static TinyGPSPlus gps;

/* ─── Radio State ───────────────────────────────────────────────────────── */

static RadioEvents_t radioEvents;
static uint8_t rxBuffer[LORA_MAX_PAYLOAD + 1];
static volatile int    rxLen  = 0;
static volatile bool   rxDone = false;
static volatile bool   txDone = false;
static volatile int16_t rxRssi = 0;

/* Command dedup */
static char lastCommandId[32] = "";

/* Command registry */
static CommandRegistry cmdRegistry;

/* Display stats */
static int           packetCount = 0;
static int16_t       lastDisplayRssi = 0;
static unsigned long lastRxTime = 0;

/* Cached GPS values (last valid fix) */
static double   cachedLat  = 0.0;
static double   cachedLon  = 0.0;
static double   cachedAlt  = 0.0;
static uint32_t cachedSats = 0;
static unsigned long lastValidGpsTime = 0;

/* GPS UART activity tracking */
static unsigned long lastGpsChars = 0;
static unsigned long lastGpsCharTime = 0;
#define GPS_UART_TIMEOUT_MS 2000  /* no uart if no chars for 2s */

/* Display modes */
enum DisplayMode { DISP_FULL, DISP_BIG_RSSI, DISP_OFF };
static DisplayMode displayMode = DISP_FULL;

/* Button state (USER_KEY = P3_3 = GPIO7) */
static bool lastButtonState = HIGH;
static unsigned long lastButtonTime = 0;
#define BUTTON_DEBOUNCE_MS 50

/* Bar graph RSSI range */
#define RSSI_MIN -120
#define RSSI_MAX -40

/* ─── OLED Display ──────────────────────────────────────────────────────── */

static void updateDisplayFull(void)
{
    oled.setFont(ArialMT_Plain_10);
    oled.setTextAlignment(TEXT_ALIGN_LEFT);
    char buf[32];

    /* Line 1: RSSI + time since last RX */
    if (packetCount > 0) {
        unsigned long rxAgo = (millis() - lastRxTime) / 1000;
        snprintf(buf, sizeof(buf), "%d dBm (%lus)", lastDisplayRssi, rxAgo);
    } else {
        snprintf(buf, sizeof(buf), "Waiting...");
    }
    oled.drawString(0, 0, buf);

    /* Line 2: Packet count + satellites */
    snprintf(buf, sizeof(buf), "Pkts: %d  Sats: %lu", packetCount, cachedSats);
    oled.drawString(0, 13, buf);

    /* Line 3: GPS status - check if UART is active */
    bool uartActive = (lastGpsCharTime > 0) &&
                      ((millis() - lastGpsCharTime) < GPS_UART_TIMEOUT_MS);
    if (!uartActive) {
        snprintf(buf, sizeof(buf), "GPS: no uart");
    } else if (gps.location.isValid()) {
        snprintf(buf, sizeof(buf), "GPS: fix");
    } else {
        snprintf(buf, sizeof(buf), "GPS: acquiring");
    }
    oled.drawString(0, 26, buf);

    /* Line 4: Time since last valid GPS fix */
    if (lastValidGpsTime > 0) {
        unsigned long gpsAgo = (millis() - lastValidGpsTime) / 1000;
        snprintf(buf, sizeof(buf), "Last fix: %lus", gpsAgo);
    } else {
        snprintf(buf, sizeof(buf), "Last fix: --");
    }
    oled.drawString(0, 39, buf);

    /* Line 5: Lat/lon or placeholder */
    if (lastValidGpsTime > 0) {
        snprintf(buf, sizeof(buf), "%.4f, %.4f", cachedLat, cachedLon);
    } else {
        snprintf(buf, sizeof(buf), "--, --");
    }
    oled.drawString(0, 52, buf);
}

static void updateDisplayBigRssi(void)
{
    char buf[32];

    /* Large RSSI value centered at top */
    oled.setFont(ArialMT_Plain_24);
    oled.setTextAlignment(TEXT_ALIGN_CENTER);
    if (packetCount > 0) {
        snprintf(buf, sizeof(buf), "%d dBm", lastDisplayRssi);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    oled.drawString(64, 0, buf);

    /* Time since last RX below RSSI */
    oled.setFont(ArialMT_Plain_10);
    if (packetCount > 0) {
        unsigned long rxAgo = (millis() - lastRxTime) / 1000;
        snprintf(buf, sizeof(buf), "%lus ago", rxAgo);
    } else {
        snprintf(buf, sizeof(buf), "Waiting...");
    }
    oled.drawString(64, 28, buf);

    /* Bar graph at bottom (y=48 to y=63, 16px tall) */
    /* RSSI range: -120 dBm (empty) to -40 dBm (full) */
    int barY = 48;
    int barHeight = 14;
    int barMaxWidth = 124;  /* leave 2px margin on each side */

    /* Draw bar outline */
    oled.drawRect(2, barY, barMaxWidth, barHeight);

    /* Calculate fill width based on RSSI */
    if (packetCount > 0) {
        int rssi = lastDisplayRssi;
        if (rssi < RSSI_MIN) rssi = RSSI_MIN;
        if (rssi > RSSI_MAX) rssi = RSSI_MAX;
        int fillWidth = ((rssi - RSSI_MIN) * (barMaxWidth - 4)) / (RSSI_MAX - RSSI_MIN);
        if (fillWidth > 0) {
            oled.fillRect(4, barY + 2, fillWidth, barHeight - 4);
        }
    }
}

static void updateDisplay(void)
{
    oled.clear();

    switch (displayMode) {
        case DISP_FULL:
            updateDisplayFull();
            break;
        case DISP_BIG_RSSI:
            updateDisplayBigRssi();
            break;
        case DISP_OFF:
            /* Just leave it blank */
            break;
    }

    oled.display();
    ledSetColor(LED_OFF);   /* I2C on port P0 can glitch the NeoPixel data pin (P0_7) */
}

/* ─── Command Handler ───────────────────────────────────────────────────── */

static void handlePing(const char *cmd, char args[][CMD_MAX_ARG_LEN],
                       int arg_count)
{
    DBG("RANGE TEST: ping received\n");
}

/* ─── Radio Callbacks ───────────────────────────────────────────────────── */

static void onTxDone(void)
{
    txDone = true;
    Radio.Sleep();
}

static void onRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    DBG("RX: size=%d rssi=%d snr=%d\n", size, rssi, snr);
    if (size > 0 && size <= LORA_MAX_PAYLOAD) {
        memcpy(rxBuffer, payload, size);
        rxLen  = size;
        rxDone = true;
        rxRssi = rssi;
    }
    Radio.Sleep();
}

static void onRxTimeout(void) { Radio.Sleep(); }
static void onRxError(void)   { Radio.Sleep(); }

/* ─── TX Helpers ────────────────────────────────────────────────────────── */

/* Block until txDone or timeout (ms). Returns true if TX completed. */
static bool waitTx(unsigned long timeoutMs)
{
    unsigned long start = millis();
    while (!txDone && (millis() - start) < timeoutMs) {
        Radio.IrqProcess();
        delay(1);
    }
    return txDone;
}

/* ─── setup / loop ──────────────────────────────────────────────────────── */

void setup(void)
{
    /* Kill RGB data pin before powering peripherals */
    pinMode(RGB, OUTPUT);
    digitalWrite(RGB, LOW);

    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW);       /* power on peripherals */
    delay(1);                      /* let Vext rail stabilise */

    /* LED initialization (same as datalog sketch) */
    ledInit();

    /* USER button for display mode toggle */
    pinMode(USER_KEY, INPUT);

    Serial.begin(GPS_BAUD);        /* NEO-6M GPS on hardware UART (no Serial1 on AB01) */

    /* OLED splash screen */
    oled.init();
    oled.clear();
    oled.setFont(ArialMT_Plain_16);
    oled.setTextAlignment(TEXT_ALIGN_CENTER);
    oled.drawString(64, 16, "Range Test");
    oled.setFont(ArialMT_Plain_10);
    oled.drawString(64, 40, NODE_ID);
    oled.display();
    ledSetColor(LED_OFF);                      /* kill any I2C-induced glitch on P0_7 */

    /* LoRa radio */
    radioEvents.TxDone    = onTxDone;
    radioEvents.RxDone    = onRxDone;
    radioEvents.RxTimeout = onRxTimeout;
    radioEvents.RxError   = onRxError;

    Radio.Init(&radioEvents);
    ledSetColor(LED_OFF);                      /* Radio.Init() may disturb the pin */
    Radio.SetChannel(RF_G2N_FREQUENCY);

    Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                      LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                      LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                      true, 0, 0, LORA_IQ_INVERSION_ON, 3000);

    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                      LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                      LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                      0, true, 0, 0, LORA_IQ_INVERSION_ON, true);

    /* Command registry — only ping for range test */
    cmdRegistryInit(&cmdRegistry, NODE_ID);
    cmdRegister(&cmdRegistry, "ping", handlePing, CMD_SCOPE_ANY, true);

    DBG("Range test initialized — node %s\n", NODE_ID);
    delay(2000);   /* show splash */
}

void loop(void)
{
    ledSetColor(LED_OFF);   /* ensure LED is off at start of every cycle */

    /* ── Feed GPS ── */
    while (Serial.available() > 0)
        gps.encode(Serial.read());

    /* Track GPS UART activity */
    unsigned long currentChars = gps.charsProcessed();
    if (currentChars > lastGpsChars) {
        lastGpsChars = currentChars;
        lastGpsCharTime = millis();
    }

    /* ── Listen on G2N for commands ── */
    Radio.Sleep();
    Radio.SetChannel(RF_G2N_FREQUENCY);
    rxDone = false;
    rxLen  = 0;
    Radio.Rx(0);   /* continuous RX */

    unsigned long listenStart = millis();

    while ((millis() - listenStart) < CYCLE_PERIOD_MS) {
        Radio.IrqProcess();

        /* Keep feeding GPS while listening */
        while (Serial.available() > 0)
            gps.encode(Serial.read());

        /* Track GPS UART activity */
        unsigned long currentChars = gps.charsProcessed();
        if (currentChars > lastGpsChars) {
            lastGpsChars = currentChars;
            lastGpsCharTime = millis();
        }

        /* Cache valid GPS readings (last known good position) */
        if (gps.location.isValid()) {
            cachedLat = gps.location.lat();
            cachedLon = gps.location.lng();
            lastValidGpsTime = millis();
        }
        if (gps.altitude.isValid()) {
            cachedAlt = gps.altitude.meters();
        }
        if (gps.satellites.isValid()) {
            cachedSats = gps.satellites.value();
        }

        /* Check USER button for display mode toggle */
        bool buttonState = digitalRead(USER_KEY);
        if (buttonState == LOW && lastButtonState == HIGH &&
            (millis() - lastButtonTime) > BUTTON_DEBOUNCE_MS) {
            lastButtonTime = millis();
            /* Cycle through modes: FULL -> BIG_RSSI -> OFF -> FULL */
            if (displayMode == DISP_FULL)
                displayMode = DISP_BIG_RSSI;
            else if (displayMode == DISP_BIG_RSSI)
                displayMode = DISP_OFF;
            else
                displayMode = DISP_FULL;
            updateDisplay();  /* immediate feedback */
        }
        lastButtonState = buttonState;

        if (!rxDone) {
            delay(1);
            continue;
        }

        /* ── Received a packet — try to parse as command ── */
        uint8_t *jsonStart = rxBuffer;
        int jsonLen = rxLen;
        for (int i = 0; i < rxLen && i < 8; i++) {
            if (rxBuffer[i] == '{') {
                jsonStart = &rxBuffer[i];
                jsonLen   = rxLen - i;
                break;
            }
        }

        CommandPacket cmd;
        if (!parseCommand(jsonStart, jsonLen, &cmd)) {
            DBG("RX: not a valid command\n");
            rxDone = false;
            rxLen  = 0;
            continue;
        }

        /* Check if addressed to us (or broadcast) */
        if (cmd.node_id[0] != '\0' && strcmp(cmd.node_id, NODE_ID) != 0) {
            DBG("RX: command for '%s', not us\n", cmd.node_id);
            rxDone = false;
            rxLen  = 0;
            continue;
        }

        /* Dedup check */
        char commandId[32];
        snprintf(commandId, sizeof(commandId), "%u_%.4s",
                 cmd.timestamp, cmd.crc);
        bool isDuplicate = (strcmp(commandId, lastCommandId) == 0);

        /* ── Turn on red LED ── */
        unsigned long ledOnAt = millis();
        ledSetColor(LED_RED);

        /* ── Send ACK on N2G ── */
        Radio.Sleep();
        Radio.SetChannel(RF_N2G_FREQUENCY);

        char ackBuf[128];
        int ackLen = buildAckPacket(ackBuf, sizeof(ackBuf),
                                    cmd.timestamp, cmd.crc, NODE_ID);
        if (ackLen > 0) {
            txDone = false;
            Radio.Send((uint8_t *)ackBuf, ackLen);
            waitTx(1000);
            DBG("ACK sent [%d bytes]\n", ackLen);
        }

        /* ── Send GPS sensor packet on N2G (skip for duplicates) ── */
        if (!isDuplicate) {
            strncpy(lastCommandId, commandId, sizeof(lastCommandId) - 1);
            lastCommandId[sizeof(lastCommandId) - 1] = '\0';

            double lat  = cachedLat;
            double lon  = cachedLon;
            double alt  = cachedAlt;
            double sats = (double)cachedSats;
            double rssi = (double)rxRssi;

            Reading readings[] = {
                { "alt",  SENSOR_ID_GPS, "m",   alt  },
                { "lat",  SENSOR_ID_GPS, "deg", lat  },
                { "lng",  SENSOR_ID_GPS, "deg", lon  },
                { "rssi", SENSOR_ID_GPS, "dBm", rssi },
                { "sats", SENSOR_ID_GPS, "",    sats },
            };

            char pkt[LORA_MAX_PAYLOAD + 1];
            int pLen = buildSensorPacket(pkt, sizeof(pkt),
                                         NODE_ID, 0u,
                                         readings, 5);
            if (pLen > 0 && pLen <= LORA_MAX_PAYLOAD) {
                delay(150);  /* gap after ACK for gateway to be ready */
                txDone = false;
                Radio.Send((uint8_t *)pkt, pLen);
                waitTx(3000);
                DBG("GPS packet sent [%d bytes]\n", pLen);
            }

            cmdDispatch(&cmdRegistry, &cmd);
        } else {
            DBG("Duplicate %s — ACK resent, dispatch skipped\n", commandId);
        }

        /* Update stats */
        packetCount++;
        lastDisplayRssi = rxRssi;
        lastRxTime = millis();

        /* Keep LED on for ~1s total */
        unsigned long ledElapsed = millis() - ledOnAt;
        if (ledElapsed < 1000)
            delay(1000 - ledElapsed);
        ledSetColor(LED_OFF);

        /* Switch back to G2N and resume listening */
        Radio.Sleep();
        Radio.SetChannel(RF_G2N_FREQUENCY);
        rxDone = false;
        rxLen  = 0;
        Radio.Rx(0);
    }

    /* ── Update display every cycle ── */
    updateDisplay();

    DBG("GPS: chars=%lu ok=%lu fail=%lu fix=%s sats=%lu\n",
        gps.charsProcessed(), gps.passedChecksum(), gps.failedChecksum(),
        gps.location.isValid() ? "yes" : "no", gps.satellites.value());
}
