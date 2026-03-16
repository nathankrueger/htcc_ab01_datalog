/*
 * gps_sensor.h — NEO-6M GPS sensor driver
 */

#ifndef GPS_SENSOR_H
#define GPS_SENSOR_H

#ifdef SENSOR_GPS
#include "sensor_drv.h"
extern const SensorDriver gpsDriver;

/*
 * Feed GPS UART data into TinyGPS++ parser.
 * Call from main loop every cycle — GPS requires continuous NMEA feeding
 * to maintain a fix, unlike instant I2C/ADC reads.
 */
void gpsFeed(void);
#endif

#endif /* GPS_SENSOR_H */
