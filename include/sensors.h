/**
 * sensors.h - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Sensor subsystem (section 7: SensorTask) and the DHT22 single-wire driver
 * (section 20).  Only the driver API is exposed here; the FreeRTOS task that
 * owns it is SensorTask(), which is created in main.cpp.
 *
 * Pin map (see README "Pin Configuration"):
 *   PA0  - LDR / photoresistor AO   (ADC1_IN0, 12 bit single conversion)
 *   PA1  - DHT22 SDA                (open drain, 10 k pull-up to 3.3 V)
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Configure the sensor GPIOs, the ADC and the DWT cycle counter used for
 *  timing. */
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

/**
 * Perform one single conversion on ADC1 channel 0 (PA0), the photoresistor
 * module's AO output.
 *
 * @param raw_value receives the right-aligned 12-bit result, 0..4095
 * @return true when a conversion completed within the timeout
 *
 * Section 23 restricts the system to ADC1 without DMA, which is also the
 * subset Wokwi simulates, so the result is polled with
 * HAL_ADC_PollForConversion().
 */
bool ldr_read_raw(uint16_t *raw_value);

/**
 * Convert the raw 12-bit reading into the documented 0-100 % light level
 * required by section 21.
 *
 * The photoresistor module puts the LDR in the lower leg of its divider, so
 * its AO voltage *falls* as the scene gets brighter - Wokwi's reference
 * table lists 4.96 V at 0.1 lux and 0.04 V at 100,000 lux.  The mapping is
 * therefore inverted, which makes 100 % the brightest possible reading and
 * 0 % the darkest.
 *
 * This is a relative light index, not calibrated lux: no lux curve is
 * implemented, so no lux figure is ever reported (section 21).
 */
uint8_t ldr_to_light_percent(uint16_t raw_value);

/** FreeRTOS task: samples the sensors periodically (section 22). */
void SensorTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* SENSORS_H */
