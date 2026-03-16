/*
 * gps_sensor.cpp — NEO-6M GPS sensor driver
 *
 * Reads GPS via the hardware UART (Serial at 9600 baud).  The AB01 has only
 * one UART, so DEBUG serial output must be disabled when this driver is active
 * (the Makefile enforces this with a build error).
 *
 * Unlike instant-read sensors (BME280, battery), GPS requires continuous UART
 * feeding.  gpsFeed() must be called from the main loop every cycle; the
 * driver's read() returns cached data from the last valid fix.
 *
 * Produces 4 readings per sample: alt, lat, lng, sats (sensor class ID 4).
 * Returns 0 readings if no valid GPS fix has been acquired.
 */

#ifdef SENSOR_GPS

#include "Arduino.h"
#include <TinyGPS++.h>
#include "gps_sensor.h"

/* ─── Sensor Config ─────────────────────────────────────────────────────── */

/*
 * Sensor class ID — matches manual registry in ../data_log/sensors/__init__.py.
 *   0=BME280, 1=MMA8452, 2=ADS1115, 3=Battery, 4=NEO6MGPS
 */
#define SENSOR_ID_GPS 4

#define GPS_BAUD 9600

/* If no UART chars arrive for this long, consider GPS disconnected */
#define GPS_UART_TIMEOUT_MS 2000

/* ─── State ─────────────────────────────────────────────────────────────── */

static TinyGPSPlus gps;

/* Cached GPS values (last valid fix) */
static double   cachedLat  = 0.0;
static double   cachedLon  = 0.0;
static double   cachedAlt  = 0.0;
static uint32_t cachedSats = 0;
static unsigned long lastValidGpsTime = 0;

/* UART activity tracking */
static unsigned long lastGpsChars    = 0;
static unsigned long lastGpsCharTime = 0;

/* ─── SensorDriver Interface ───────────────────────────────────────────── */

static int gps_init(void)
{
    Serial.begin(GPS_BAUD);
    return 1;  /* UART always available — cannot probe GPS module directly */
}

static int gps_is_alive(void)
{
    /* If we've never seen any chars, assume still initializing (cold start) */
    if (lastGpsCharTime == 0)
        return 1;

    /* UART went silent — GPS module may be disconnected */
    if ((millis() - lastGpsCharTime) >= GPS_UART_TIMEOUT_MS)
        return 0;

    return 1;
}

static int gps_read(Reading *out, int max)
{
    if (max < 4) return 0;

    /* Only report readings when we have a valid fix */
    if (!gps.location.isValid())
        return 0;

    out[0] = { "alt",  SENSOR_ID_GPS, "m",   cachedAlt              };
    out[1] = { "lat",  SENSOR_ID_GPS, "deg", cachedLat              };
    out[2] = { "lng",  SENSOR_ID_GPS, "deg", cachedLon              };
    out[3] = { "sats", SENSOR_ID_GPS, "",    (double)cachedSats     };

    return 4;
}

/* ─── Public: GPS UART Feeding ─────────────────────────────────────────── */

void gpsFeed(void)
{
    /* Drain serial buffer into TinyGPS++ parser */
    while (Serial.available() > 0)
        gps.encode(Serial.read());

    /* Track UART activity */
    unsigned long currentChars = gps.charsProcessed();
    if (currentChars > lastGpsChars) {
        lastGpsChars    = currentChars;
        lastGpsCharTime = millis();
    }

    /* Cache valid GPS data */
    if (gps.location.isValid()) {
        cachedLat = gps.location.lat();
        cachedLon = gps.location.lng();
        lastValidGpsTime = millis();
    }
    if (gps.altitude.isValid()) {
        cachedAlt = gps.altitude.meters();
    }
    if (gps.satellites.isValid()) {
        cachedSats = gps.satellites.value();
    }
}

/* ─── Driver Instance ──────────────────────────────────────────────────── */

extern uint16_t gpsRateSec;

const SensorDriver gpsDriver = {
    "gps", gps_init, gps_is_alive, gps_read, &gpsRateSec
};

#endif /* SENSOR_GPS */
