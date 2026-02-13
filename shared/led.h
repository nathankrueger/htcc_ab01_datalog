#ifndef LED_H
#define LED_H

/*
 * Header-only LED library for CubeCell HTCC-AB01 NeoPixel.
 *
 * All functions are static to provide internal linkage - each translation
 * unit (sketch) gets its own copy. This is the pragmatic approach for
 * Arduino's build model where sketches compile as single TUs.
 */

#include "Arduino.h"
#include "CubeCell_NeoPixel.h"
#include <string.h>

/* ─── Configuration ─────────────────────────────────────────────────────── */

/*
 * LED_ORDER selects the NeoPixel color byte ordering.  Most HTCC-AB01 V2
 * boards ship with WS2812B (GRB), but some revisions use an RGB-ordered
 * variant.  Override at build time:  make LED_ORDER=RGB
 */
#ifndef LED_ORDER
#define LED_ORDER "GRB"
#endif

#ifndef LED_BRIGHTNESS
#define LED_BRIGHTNESS 128
#endif

/* Pixel type derived from LED_ORDER define */
#define _LED_STR_EQ(a, b) ((a)[0]==(b)[0] && (a)[1]==(b)[1] && (a)[2]==(b)[2])
#define _LED_PIXEL_TYPE (_LED_STR_EQ(LED_ORDER, "RGB") ? NEO_RGB : NEO_GRB)

/* ─── Types ─────────────────────────────────────────────────────────────── */

typedef enum {
    LED_OFF = 0,
    LED_RED,
    LED_GREEN,
    LED_BLUE,
    LED_YELLOW,      /* Red + Green */
    LED_CYAN,        /* Green + Blue */
    LED_MAGENTA,     /* Red + Blue */
    LED_WHITE        /* All on */
} LEDColor;

/* ─── Static Instance ───────────────────────────────────────────────────── */

static CubeCell_NeoPixel _rgbLed(1, RGB, _LED_PIXEL_TYPE + NEO_KHZ800);

/* ─── Implementation ────────────────────────────────────────────────────── */

/**
 * Initialize the NeoPixel LED.
 * Call once at startup after powering on Vext.
 */
static void ledInit(void)
{
    _rgbLed.begin();
    _rgbLed.clear();
    _rgbLed.show();
    pinMode(RGB, OUTPUT);
    digitalWrite(RGB, LOW);
}

/**
 * Set the LED to an arbitrary RGB color.
 */
static void ledSetRGB(uint8_t r, uint8_t g, uint8_t b)
{
    _rgbLed.setPixelColor(0, _rgbLed.Color(r, g, b));
    _rgbLed.show();
}

/**
 * Turn off the LED.
 */
static void ledOff(void)
{
    _rgbLed.setPixelColor(0, 0);
    _rgbLed.show();
}

/**
 * Set the LED to a predefined color at the given brightness (0-255).
 */
static void ledSetColorBrightness(LEDColor color, uint8_t brightness)
{
    switch (color) {
        case LED_OFF:
            ledOff();
            return;
        case LED_RED:
            ledSetRGB(brightness, 0, 0);
            break;
        case LED_GREEN:
            ledSetRGB(0, brightness, 0);
            break;
        case LED_BLUE:
            ledSetRGB(0, 0, brightness);
            break;
        case LED_YELLOW:
            ledSetRGB(brightness, brightness, 0);
            break;
        case LED_CYAN:
            ledSetRGB(0, brightness, brightness);
            break;
        case LED_MAGENTA:
            ledSetRGB(brightness, 0, brightness);
            break;
        case LED_WHITE:
            ledSetRGB(brightness, brightness, brightness);
            break;
        default:
            ledOff();
            break;
    }
}

/**
 * Set the LED to a predefined color at the default brightness.
 */
static void ledSetColor(LEDColor color)
{
    ledSetColorBrightness(color, LED_BRIGHTNESS);
}

/**
 * Parse a color name string to LEDColor enum.
 * Accepts full names (e.g., "red") or single-letter shortcuts (e.g., "r").
 * Returns LED_OFF for unrecognized colors.
 */
static LEDColor parseColor(const char *colorStr)
{
    if (strcasecmp(colorStr, "red") == 0 || strcasecmp(colorStr, "r") == 0)
        return LED_RED;
    if (strcasecmp(colorStr, "green") == 0 || strcasecmp(colorStr, "g") == 0)
        return LED_GREEN;
    if (strcasecmp(colorStr, "blue") == 0 || strcasecmp(colorStr, "b") == 0)
        return LED_BLUE;
    if (strcasecmp(colorStr, "yellow") == 0 || strcasecmp(colorStr, "y") == 0)
        return LED_YELLOW;
    if (strcasecmp(colorStr, "cyan") == 0 || strcasecmp(colorStr, "c") == 0)
        return LED_CYAN;
    if (strcasecmp(colorStr, "magenta") == 0 || strcasecmp(colorStr, "m") == 0)
        return LED_MAGENTA;
    if (strcasecmp(colorStr, "white") == 0 || strcasecmp(colorStr, "w") == 0)
        return LED_WHITE;
    if (strcasecmp(colorStr, "off") == 0 || strcasecmp(colorStr, "o") == 0)
        return LED_OFF;
    return LED_OFF;
}

/**
 * Rapid blink: flash a color N times with the given on/off period.
 * Blocking — total time ≈ count * 2 * periodMs.
 */
static void ledBlink(LEDColor color, int count, unsigned long periodMs,
                     uint8_t brightness = LED_BRIGHTNESS)
{
    for (int i = 0; i < count; i++) {
        ledSetColorBrightness(color, brightness);
        delay(periodMs);
        ledOff();
        if (i < count - 1) delay(periodMs);
    }
}

/**
 * Cycle through all colors for diagnostic testing.
 * @param delayMs  milliseconds per step (default 5000)
 */
static void ledTest(unsigned long delayMs = 5000,
                    uint8_t brightness = LED_BRIGHTNESS)
{
    const unsigned long delayAmt = delayMs;

    struct { LEDColor color; const char *name; } steps[] = {
        { LED_RED,     "red"     },
        { LED_GREEN,   "green"   },
        { LED_BLUE,    "blue"    },
        { LED_YELLOW,  "yellow"  },
        { LED_CYAN,    "cyan"    },
        { LED_MAGENTA, "magenta" },
        { LED_WHITE,   "white"   },
    };
    const int numSteps = sizeof(steps) / sizeof(steps[0]);

    Serial.printf("LED test: %d colors at brightness %d, %lu ms each\n",
                  numSteps, brightness, delayAmt);

    for (int i = 0; i < numSteps; i++) {
        Serial.printf("  [%d/%d] %s\n", i + 1, numSteps, steps[i].name);
        ledSetColorBrightness(steps[i].color, brightness);
        delay(delayAmt);
    }

    /* Full-brightness primary test */
    Serial.println("  Full-brightness RGB test");
    Serial.println("    red 255");
    ledSetRGB(255, 0, 0);
    delay(delayAmt);
    Serial.println("    green 255");
    ledSetRGB(0, 255, 0);
    delay(delayAmt);
    Serial.println("    blue 255");
    ledSetRGB(0, 0, 255);
    delay(delayAmt);

    ledOff();
    Serial.println("LED test complete");
}

#endif /* LED_H */
