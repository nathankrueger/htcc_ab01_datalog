/*
 * sensors.h — Sensor abstraction for data_log sketch
 *
 * Owns the BME280 hardware, sampling, and packet building.
 * data_log.ino calls sensorInit() once from setup(), then
 * sensorRead() + sensorPack() each cycle.  TX stays in the main loop.
 */

#ifndef SENSORS_H
#define SENSORS_H

#include "packets.h"

#define SENSOR_MAX_READINGS 8

/*
 * Initialize BME280 sensor.
 * Tries I2C addresses 0x76 and 0x77.
 * Returns true if sensor found.
 */
bool sensorInit(void);

/* Returns true if sensor is currently available. */
bool sensorAvailable(void);

/*
 * Read BME280 and populate readings[].
 * Returns number of valid readings (0 on error or sensor unavailable).
 */
int sensorRead(Reading *readings, int maxReadings);

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

#endif /* SENSORS_H */
