/*
 * bme280_sensor.h — BME280 temperature/pressure/humidity sensor driver
 */

#ifndef BME280_SENSOR_H
#define BME280_SENSOR_H

#ifdef SENSOR_BME280
#include "sensor_drv.h"
extern const SensorDriver bme280Driver;
#endif

#endif /* BME280_SENSOR_H */
