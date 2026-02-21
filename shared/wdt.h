/*
 * wdt.h — WDT-safe delay utility for CubeCell HTCC-AB01
 *
 * Breaks long delays into chunks, feeding the PSoC4 hardware watchdog
 * at regular intervals to prevent unexpected resets.
 *
 * feedInnerWdt() is a no-op when WDT is not enabled, so this is safe
 * to call from both data_log (WDT enabled) and range_test (WDT disabled).
 */

#ifndef WDT_H
#define WDT_H

#include "Arduino.h"

/* Forward-declare rather than #include "innerWdt.h" — the CubeCell
 * header lacks include guards and defines functions inline, so a
 * second inclusion in the same TU causes redefinition errors. */
extern void feedInnerWdt();

/*
 * WDT feed interval — half the ~4s hardware timeout.
 * PSoC4 ILO match=0xFFFF at ~32 kHz gives ~2s per match,
 * reset after two missed clears → ~4s.  Feed every 2s for safety.
 */
#ifndef WDT_FEED_INTERVAL_MS
#define WDT_FEED_INTERVAL_MS 2000
#endif

/*
 * WDT-safe delay: sleep for totalMs while feeding the watchdog.
 *
 * Breaks the delay into WDT_FEED_INTERVAL_MS chunks, calling
 * feedInnerWdt() between each chunk.  The final chunk handles
 * the remainder so total sleep time is accurate.
 */
static inline void sleepWdt(unsigned long totalMs)
{
    unsigned long elapsed = 0;

    while (elapsed < totalMs) {
        unsigned long chunk = totalMs - elapsed;
        if (chunk > WDT_FEED_INTERVAL_MS)
            chunk = WDT_FEED_INTERVAL_MS;

        delay(chunk);
        feedInnerWdt();
        elapsed += chunk;
    }
}

#endif /* WDT_H */
