/**
 * sensors.h - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Sensor subsystem (section 7: SensorTask) and the DHT22 single-wire driver
 * (section 20).  Only the driver API is exposed here; the FreeRTOS task that
 * owns it is SensorTask(), which is created in main.cpp.
 *
 * Pin map (see README "Pin Configuration"):
 *   PA0  - LDR / photoresistor AO   (ADC1_IN0)  [milestone 5]
 *   PA1  - DHT22 SDA                (open drain, 10 k pull-up to 3.3 V)
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Configure the sensor GPIOs and the DWT cycle counter used for timing. */
void sensors_init(void);

/**
 * Read one 40-bit frame from the DHT22.
 *
 * @param temperature_c      receives the temperature in degrees Celsius
 * @param humidity_percent   receives the relative humidity in percent
 * @return true when the frame and its checksum are valid, false otherwise
 *
 * The transaction runs with the FreeRTOS scheduler suspended: no other task
 * may preempt the microsecond-scale bit timing, yet the SysTick interrupt
 * keeps running so the HAL time base is not disturbed.  xTaskResumeAll()
 * replays the pended ticks, so the kernel tick count does not slip.
 */
bool dht22_read(float *temperature_c, float *humidity_percent);

/** FreeRTOS task: samples the sensors periodically (section 22). */
void SensorTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* SENSORS_H */
