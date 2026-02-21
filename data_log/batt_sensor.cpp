/*
 * batt_sensor.cpp — CubeCell battery voltage sensor driver
 *
 * Reads the on-board ADC via getBatteryVoltage() (CubeCell SDK).
 * Produces 1 reading: Voltage in millivolts.
 * Always available — no external hardware required.
 */

#ifdef SENSOR_BATT

#include "Arduino.h"
#include "LoRaWan_APP.h"
#include "batt_sensor.h"

/* ─── Debug Output ──────────────────────────────────────────────────────── */

#include "dbg.h"

/* ─── Sensor Config ─────────────────────────────────────────────────────── */

/*
 * Sensor class ID — matches manual registry in ../data_log/sensors/__init__.py.
 *   0=BME280, 1=MMA8452, 2=ADS1115, 3=Battery, 4=NEO6MGPS
 */
#define SENSOR_ID_BATT 3

/* ─── SensorDriver Interface ───────────────────────────────────────────── */

static int batt_init(void)
{
    /* CubeCell ADC is always available — nothing to initialize */
    return 1;
}

static int batt_is_alive(void)
{
    return 1;
}

static int batt_read(Reading *out, int max)
{
    if (max < 1) return 0;

    uint16_t mv = getBatteryVoltage();
    out[0] = { "Voltage", SENSOR_ID_BATT, "mV", (double)mv };

    DBG("BATT: %u mV\n", (unsigned)mv);
    return 1;
}

/* ─── Driver Instance ──────────────────────────────────────────────────── */

extern uint16_t battRateSec;

const SensorDriver battDriver = {
    "batt", batt_init, batt_is_alive, batt_read, &battRateSec
};

#endif /* SENSOR_BATT */
