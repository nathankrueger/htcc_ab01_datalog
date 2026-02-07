#ifndef BUTTONS_AND_LIGHTS_H
#define BUTTONS_AND_LIGHTS_H

#include "Arduino.h"

/* ─── RGB LED Control ────────────────────────────────────────────────────── */

/* RGB LED colors */
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

/* Initialize RGB LED pins */
void ledInit(void);

/* Set RGB LED to a specific color */
void ledSetColor(LEDColor color);

/* Turn off all RGB LED channels */
void ledOff(void);

/* Set individual RGB channels (true = on, false = off) */
void ledSetRGB(bool red, bool green, bool blue);

/* ─── Button Control ─────────────────────────────────────────────────────── */

/* Initialize button pin */
void buttonInit(void);

/* Read button state with debouncing. Returns true if button was just pressed. */
bool buttonPressed(void);

/* Raw button read (true = pressed, false = released) */
bool buttonRead(void);

#endif /* BUTTONS_AND_LIGHTS_H */
