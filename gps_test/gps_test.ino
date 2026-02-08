/*
 * GPS Display Test for CubeCell HTCC-AB01 V2
 *
 * Reads NMEA from NEO-6M GPS on Serial (P3_0 RX / P3_1 TX) at 9600 baud
 * and shows parsed data on SSD1306 OLED.
 *
 * Wiring:
 *   GPS TX  -> J3 pin 7 (UART_RX / P3_0)
 *   GPS VCC -> 3V3
 *   GPS GND -> GND
 *
 * LED: green = fix, yellow = receiving but no fix, off = no data
 *
 * Build:  make SKETCH=gps_test
 * Flash:  make upload SKETCH=gps_test
 *         (disconnect GPS TX from P3_0 before flashing!)
 */

#include "Arduino.h"
#include "HT_SSD1306Wire.h"
#include "CubeCell_NeoPixel.h"
#include <TinyGPS++.h>

#ifndef LED_BRIGHTNESS
#define LED_BRIGHTNESS 64
#endif

SSD1306Wire oled(0x3c, 500000, SDA, SCL, GEOMETRY_128_64, -1);
CubeCell_NeoPixel led(1, RGB, NEO_GRB + NEO_KHZ800);
TinyGPSPlus gps;

static uint32_t totalChars = 0;
static uint32_t lastCharTime = 0;

static void ledColor(uint8_t r, uint8_t g, uint8_t b)
{
    led.setPixelColor(0, led.Color(r, g, b));
    led.show();
}

static void ledOff(void)
{
    led.setPixelColor(0, 0);
    led.show();
    pinMode(GPIO4, OUTPUT);
    digitalWrite(GPIO4, LOW);
}

void setup()
{
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW);
    delay(100);

    led.begin();
    ledOff();

    oled.init();
    oled.setFont(ArialMT_Plain_10);
    oled.clear();
    oled.drawString(0, 0, "GPS Test");
    oled.drawString(0, 16, "Waiting for data...");
    oled.drawString(0, 32, "GPS TX -> J3 pin 7");
    oled.display();

    Serial.begin(9600);
    delay(100);
}

void loop()
{
    unsigned long start = millis();
    while (millis() - start < 1000) {
        while (Serial.available()) {
            gps.encode(Serial.read());
            totalChars++;
            lastCharTime = millis();
        }
        delay(1);
    }

    bool receiving = (millis() - lastCharTime) < 3000;
    bool hasFix = gps.location.isValid();

    if (hasFix)
        ledColor(0, LED_BRIGHTNESS, 0);
    else if (receiving)
        ledColor(LED_BRIGHTNESS, LED_BRIGHTNESS, 0);
    else
        ledOff();

    char line[5][32];

    if (!receiving) {
        snprintf(line[0], 32, "NO GPS DATA");
        snprintf(line[1], 32, "GPS TX -> J3 pin 7");
        snprintf(line[2], 32, "Chars: %lu", (unsigned long)totalChars);
        line[3][0] = '\0';
        line[4][0] = '\0';
    } else if (!hasFix) {
        snprintf(line[0], 32, "Acquiring fix...");
        snprintf(line[1], 32, "Sats: %d", (int)gps.satellites.value());
        snprintf(line[2], 32, "Chars: %lu", (unsigned long)totalChars);
        snprintf(line[3], 32, "Fixes: %lu", (unsigned long)gps.sentencesWithFix());
        snprintf(line[4], 32, "Uptime: %lus", millis() / 1000);
    } else {
        snprintf(line[0], 32, "%.6f, %.6f", gps.location.lat(), gps.location.lng());
        snprintf(line[1], 32, "Sats: %d  Alt: %.0fm",
                 (int)gps.satellites.value(),
                 gps.altitude.isValid() ? gps.altitude.meters() : 0.0);
        if (gps.time.isValid())
            snprintf(line[2], 32, "UTC %02d:%02d:%02d",
                     gps.time.hour(), gps.time.minute(), gps.time.second());
        else
            snprintf(line[2], 32, "UTC --:--:--");
        snprintf(line[3], 32, "HDOP: %.1f",
                 gps.hdop.isValid() ? gps.hdop.hdop() : 0.0);
        snprintf(line[4], 32, "Chars: %lu", (unsigned long)totalChars);
    }

    oled.clear();
    oled.setFont(ArialMT_Plain_10);
    for (int i = 0; i < 5; i++)
        oled.drawString(0, i * 13, line[i]);
    oled.display();
}
