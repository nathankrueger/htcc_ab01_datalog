/*
 * batt_sensor.h — CubeCell battery voltage sensor driver
 */

#ifndef BATT_SENSOR_H
#define BATT_SENSOR_H

#ifdef SENSOR_BATT
#include "sensor_drv.h"
extern const SensorDriver battDriver;
#endif

#endif /* BATT_SENSOR_H */
