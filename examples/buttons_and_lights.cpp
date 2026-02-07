#include "buttons_and_lights.h"

/* ─── Pin Definitions ────────────────────────────────────────────────────── */

#define RGB_RED    GPIO0
#define RGB_GREEN  GPIO1
#define RGB_BLUE   GPIO2
#define USR_BUTTON GPIO7

/* ─── Button Debouncing State ────────────────────────────────────────────── */

static bool lastButtonState = HIGH;
static bool buttonState = HIGH;
static unsigned long lastDebounceTime = 0;
static const unsigned long DEBOUNCE_DELAY = 50;  /* ms */

/* ─── RGB LED Functions ──────────────────────────────────────────────────── */

void ledInit(void)
{
    pinMode(RGB_RED, OUTPUT);
    pinMode(RGB_GREEN, OUTPUT);
    pinMode(RGB_BLUE, OUTPUT);
    ledOff();
}

void ledSetRGB(bool red, bool green, bool blue)
{
    digitalWrite(RGB_RED, red ? HIGH : LOW);
    digitalWrite(RGB_GREEN, green ? HIGH : LOW);
    digitalWrite(RGB_BLUE, blue ? HIGH : LOW);
}

void ledSetColor(LEDColor color)
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

void ledOff(void)
{
    ledSetRGB(false, false, false);
}

/* ─── Button Functions ───────────────────────────────────────────────────── */

void buttonInit(void)
{
    pinMode(USR_BUTTON, INPUT_PULLUP);
    lastButtonState = HIGH;
    buttonState = HIGH;
    lastDebounceTime = 0;
}

bool buttonRead(void)
{
    /* Button is active LOW (pressed = LOW) */
    return digitalRead(USR_BUTTON) == LOW;
}

bool buttonPressed(void)
{
    /* Read current button state */
    int reading = digitalRead(USR_BUTTON);

    /* If button state changed, reset debounce timer */
    if (reading != lastButtonState) {
        lastDebounceTime = millis();
    }

    /* If enough time has passed, accept the new reading */
    bool pressed = false;
    if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
        /* If button state has changed */
        if (reading != buttonState) {
            buttonState = reading;

            /* Only trigger on button press (HIGH to LOW transition) */
            if (buttonState == LOW) {
                pressed = true;
            }
        }
    }

    lastButtonState = reading;
    return pressed;
}
