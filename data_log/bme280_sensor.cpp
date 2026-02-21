/*
 * bme280_sensor.cpp — BME280 temperature/pressure/humidity sensor driver
 *
 * Reads via I2C (Adafruit_BME280 library).  Produces 3 readings per sample:
 * Temperature (°F), Pressure (hPa), Humidity (%).  Auto-reinit on disconnect.
 */

#ifdef SENSOR_BME280

#include "Arduino.h"
#include <Wire.h>
#include <Adafruit_BME280.h>
#include "bme280_sensor.h"

/* ─── Debug Output ──────────────────────────────────────────────────────── */

#include "dbg.h"

/* ─── Sensor Config ─────────────────────────────────────────────────────── */

/*
 * Sensor class ID — matches manual registry in ../data_log/sensors/__init__.py.
 *   0=BME280, 1=MMA8452, 2=ADS1115, 3=Battery, 4=NEO6MGPS
 */
#define SENSOR_ID_BME280 0

/* ─── State ─────────────────────────────────────────────────────────────── */

static Adafruit_BME280 bme;
static bool bmeOk = false;
static uint8_t bmeAddr = 0;  /* I2C address found during init */

/* ─── Helpers ───────────────────────────────────────────────────────────── */

static float c_to_f(float c) { return c * 9.0f / 5.0f + 32.0f; }

/*
 * Live I2C probe: send address byte, check for ACK.
 * Returns true if the device responds on the bus.
 */
static bool i2cProbe(uint8_t addr)
{
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
}

/* ─── SensorDriver Interface ───────────────────────────────────────────── */

static int bme280_init(void)
{
    /* Full I2C bus reset — tears down the peripheral and reinitializes.
     * Recovers from stuck SDA (bus lockup after hot-unplug) that a
     * plain Wire.begin() inside bme.begin() cannot fix. */
    Wire.end();
    delay(10);

    bmeOk = bme.begin(0x76);
    if (bmeOk) { bmeAddr = 0x76; }
    else {
        bmeOk = bme.begin(0x77);
        if (bmeOk) bmeAddr = 0x77;
    }
    if (!bmeOk) DBGLN("ERROR: BME280 not found on 0x76 or 0x77");
    return bmeOk ? 1 : 0;
}

static int bme280_is_alive(void)
{
    if (!bmeOk) return 0;

    /* Live I2C probe — catches physical disconnection immediately */
    if (!i2cProbe(bmeAddr)) {
        DBGLN("ERROR: BME280 not responding on I2C — marking unavailable");
        bmeOk = false;
        return 0;
    }
    return 1;
}

static int bme280_read(Reading *out, int max)
{
    if (!bmeOk || max < 3) return 0;

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
    out[0] = { "Temperature", SENSOR_ID_BME280, "\\u00b0F", tempF    };
    out[1] = { "Pressure",    SENSOR_ID_BME280, "hPa",      pressure };
    out[2] = { "Humidity",    SENSOR_ID_BME280, "%",        humidity  };

    return 3;
}

/* ─── Driver Instance ──────────────────────────────────────────────────── */

extern uint16_t bme280RateSec;

const SensorDriver bme280Driver = {
    "bme280", bme280_init, bme280_is_alive, bme280_read, &bme280RateSec
};

#endif /* SENSOR_BME280 */
