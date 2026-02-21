/*
 * sensor_drv.cpp — Sensor driver registry and polling logic
 *
 * Manages an array of registered SensorDriver slots.  Each slot tracks
 * the driver's alive state and last-TX timestamp independently, enabling
 * per-sensor sample intervals.
 */

#include "Arduino.h"
#include "sensor_drv.h"

/* ─── Debug Output ──────────────────────────────────────────────────────── */

#include "dbg.h"

/* ─── Registry State ────────────────────────────────────────────────────── */

static struct {
    SensorDriver  drv;
    unsigned long last_tx_time;
    bool          alive;
} slots[SENSOR_MAX_DRIVERS];

static int slotCount = 0;

/* ─── Public API ────────────────────────────────────────────────────────── */

void sensorRegister(const SensorDriver *drv)
{
    if (slotCount >= SENSOR_MAX_DRIVERS) {
        DBG("ERROR: sensor registry full, cannot add '%s'\n", drv->name);
        return;
    }
    slots[slotCount].drv         = *drv;
    slots[slotCount].last_tx_time = 0;
    slots[slotCount].alive        = false;
    slotCount++;
}

void sensorInitAll(void)
{
    for (int i = 0; i < slotCount; i++) {
        slots[i].alive = (slots[i].drv.init() != 0);
        DBG("Sensor '%s': %s\n", slots[i].drv.name,
            slots[i].alive ? "OK" : "FAIL");
    }
}

int sensorPoll(unsigned long now, Reading *out, int maxReadings)
{
    int total = 0;

    for (int i = 0; i < slotCount && total < maxReadings; i++) {
        /* Determine interval from runtime global (via pointer) */
        uint16_t interval = slots[i].drv.interval_sec
                          ? *slots[i].drv.interval_sec : 5;
        if (interval == 0) interval = 1;
        unsigned long intervalMs = (unsigned long)interval * 1000UL;

        bool due = (slots[i].last_tx_time == 0) ||
                   (now - slots[i].last_tx_time >= intervalMs);
        if (!due) continue;

        /* Check alive, attempt reinit if not */
        if (!slots[i].alive || !slots[i].drv.is_alive()) {
            DBG("Sensor '%s' not available — attempting reinit...\n",
                slots[i].drv.name);
            slots[i].alive = (slots[i].drv.init() != 0);
            if (!slots[i].alive) {
                DBG("ERROR: '%s' reinit failed, skipping\n",
                    slots[i].drv.name);
                continue;
            }
        }

        int nRead = slots[i].drv.read(out + total, maxReadings - total);
        if (nRead > 0) {
            total += nRead;
            slots[i].last_tx_time = now;
        } else {
            DBG("ERROR: '%s' read failed, skipping\n", slots[i].drv.name);
        }
    }

    return total;
}
