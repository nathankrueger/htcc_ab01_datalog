/*
 * sensor_drv.h — Sensor driver abstraction for data_log sketch
 *
 * Defines the SensorDriver vtable, a registry for plug-and-play sensor
 * types, and the sensorPack() helper for building LoRa sensor packets.
 * Each sensor type (BME280, battery, etc.) implements a SensorDriver and
 * registers it at startup.  The main loop calls sensorPoll() each cycle.
 */

#ifndef SENSOR_DRV_H
#define SENSOR_DRV_H

#include "packets.h"

/* ─── Limits ───────────────────────────────────────────────────────────── */

#define SENSOR_MAX_DRIVERS  4
#define SENSOR_MAX_READINGS 8

/* ─── Driver Interface ─────────────────────────────────────────────────── */

/*
 * Each sensor type provides a SensorDriver with function pointers.
 * interval_sec points to the runtime global for that sensor's sample
 * interval (e.g. &bme280RateSec), so setparam changes take effect
 * immediately without rebooting.
 */
typedef struct {
    const char *name;                       /* "bme280", "batt"              */
    int  (*init)(void);                     /* 1=ok, 0=fail                  */
    int  (*is_alive)(void);                 /* 1=available, 0=not            */
    int  (*read)(Reading *out, int max);    /* fill readings, return count   */
    uint16_t *interval_sec;                 /* → runtime global (seconds)    */
} SensorDriver;

/* ─── Registry API ─────────────────────────────────────────────────────── */

/*
 * Register a sensor driver.  Call from setup() for each enabled sensor.
 * The SensorDriver struct is copied — the original can be const.
 */
void sensorRegister(const SensorDriver *drv);

/* Call init() on all registered drivers.  Call once from setup(). */
void sensorInitAll(void);

/*
 * Poll all registered sensors.  For each driver whose interval has
 * elapsed: check is_alive (reinit if needed), call read(), append
 * readings to out[].  Returns total reading count (0 = nothing due).
 *
 * `now` should be millis() at cycle start.
 */
int sensorPoll(unsigned long now, Reading *out, int maxReadings);

/* ─── Packet Packing Helper ────────────────────────────────────────────── */

/*
 * Greedily pack readings[offset..count-1] into one sensor packet.
 *
 * Returns packet byte length on success (written to pkt).
 * *nextOffset is set to the index past the last reading included.
 * Returns 0 if a single reading at offset exceeds the payload limit
 * (caller should skip that reading by incrementing offset).
 *
 * Static inline — no Arduino deps, testable natively.
 */
static inline int sensorPack(const char *nodeId, const Reading *readings,
                              int count, int offset, int *nextOffset,
                              char *pkt, int pktCap)
{
    if (offset >= count) {
        *nextOffset = count;
        return 0;
    }

    /*
     * Greedily pack as many readings as possible into one packet
     * while staying at or below LORA_MAX_PAYLOAD.
     *
     * TODO: replace the timestamp (currently 0) with a real Unix epoch.
     *       The gateway passes it through to the dashboard unchanged, so 0
     *       will show up as 1970-01-01.  Add NTP sync or an RTC module to fix.
     */
    int end  = count;
    int pLen = 0;

    while (end > offset) {
        pLen = buildSensorPacket(pkt, pktCap,
                                 nodeId, 0u,
                                 &readings[offset], end - offset);
        if (pLen > 0 && pLen <= LORA_MAX_PAYLOAD) break;
        end--;
    }

    if (end == offset) {
        /* Single reading exceeds max payload — caller should skip it */
        *nextOffset = offset + 1;
        return 0;
    }

    *nextOffset = end;
    return pLen;
}

#endif /* SENSOR_DRV_H */
