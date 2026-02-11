/*
 * sensors.cpp — BME280 sensor logic for data_log sketch
 *
 * Owns the Adafruit_BME280 instance and provides init, read, and
 * packet-building functions.  data_log.ino handles the TX loop.
 */

#include "Arduino.h"
#include <Adafruit_BME280.h>
#include "sensors.h"

/* ─── Debug Output ──────────────────────────────────────────────────────── */

#ifndef DEBUG
#define DEBUG 1
#endif

#if DEBUG
  #define DBG(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
  #define DBGLN(msg)     Serial.println(msg)
#else
  #define DBG(fmt, ...)  ((void)0)
  #define DBGLN(msg)     ((void)0)
#endif

/* ─── Sensor Config ─────────────────────────────────────────────────────── */

/*
 * Sensor-class IDs are assigned by alphabetical sort of the Python class names
 * at import time in sensors/__init__.py.  Current registry:
 *   0 = BME280TempPressureHumidity
 *   1 = MMA8452Accelerometer
 * If you add a new sensor class on the Python side, re-derive these.
 */
#define SENSOR_ID_BME280 0

/* ─── State ─────────────────────────────────────────────────────────────── */

static Adafruit_BME280 bme;
static bool bmeOk = false;

/* ─── Helpers ───────────────────────────────────────────────────────────── */

static float c_to_f(float c) { return c * 9.0f / 5.0f + 32.0f; }

/* ─── Public API ────────────────────────────────────────────────────────── */

bool sensorInit(void)
{
    bmeOk = bme.begin(0x76);
    if (!bmeOk) bmeOk = bme.begin(0x77);
    if (!bmeOk) DBGLN("ERROR: BME280 not found on 0x76 or 0x77");
    return bmeOk;
}

bool sensorAvailable(void)
{
    return bmeOk;
}

int sensorRead(Reading *readings, int maxReadings)
{
    if (!bmeOk || maxReadings < 3) return 0;

    float tempF    = c_to_f(bme.readTemperature());   /* °C → °F */
    float pressure = bme.readPressure() / 100.0f;     /* Pa  → hPa */
    float humidity = bme.readHumidity();

    /* Guard against NaN / Inf from a bad read — %g would produce non-JSON */
    if (isnan(tempF) || isinf(tempF) ||
        isnan(pressure) || isinf(pressure) ||
        isnan(humidity) || isinf(humidity)) {
        DBGLN("ERROR: BME280 returned NaN/Inf, skipping");
        return 0;
    }

    /* Round to match BME280 output precision */
    tempF    = roundf(tempF    * 10.0f)  / 10.0f;   /* 1 dp */
    pressure = roundf(pressure * 100.0f) / 100.0f;  /* 2 dp */
    humidity = roundf(humidity * 10.0f)  / 10.0f;   /* 1 dp */

    DBG("T=%.1f F  P=%.2f hPa  H=%.1f %%\n", tempF, pressure, humidity);

    /*
     * Units must use JSON \uXXXX escapes for any non-ASCII characters so the
     * CRC matches Python's json.dumps(..., ensure_ascii=True).
     * The degree sign ° (U+00B0) becomes \u00b0 in the JSON wire bytes.
     * In this C string literal the backslash is escaped once: "\\u00b0F".
     */
    readings[0] = { "Temperature", SENSOR_ID_BME280, "\\u00b0F", tempF    };
    readings[1] = { "Pressure",    SENSOR_ID_BME280, "hPa",      pressure };
    readings[2] = { "Humidity",    SENSOR_ID_BME280, "%",        humidity  };

    return 3;
}

/* sensorPack() is static inline in sensors.h (testable without Arduino deps) */
