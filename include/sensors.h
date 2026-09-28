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

/**
 * Section 24: the message SensorTask publishes to its consumers.
 *
 * motionDetected is part of the required layout from the start.  It stays
 * false until MotionTask (section 31) starts producing motion events; the
 * field is filled in by SensorTask so that DisplayTask and AlarmTask always
 * receive one self-contained sample.
 */
typedef struct SensorData
{
    float temperature;    /* degrees Celsius                       */
    float humidity;       /* percent relative humidity             */
    int   lightLevel;     /* 0-100 % relative light index (section 21) */
    bool  motionDetected; /* latest PIR state (section 31)         */
} SensorData;

/** How often SensorTask publishes a sample - section 22, 2000 ms. */
#define SENSOR_SAMPLE_PERIOD_MS 2000U

/**
 * Attempts SensorTask makes on one DHT22 reading before it reports failure.
 *
 * A single-wire frame can fail transiently - the bit timing is sampled with
 * the cycle counter while the SysTick interrupt still runs inside the
 * suspension window (sensors.h' dht22_read comment), so one interrupted
 * sample can spoil a checksum.  In simulation the very first frame after
 * boot is the fragile one; every later read at the 2 s cadence is clean, so
 * re-driving the start pulse after a short gap turns the transient into a
 * normal reading instead of a false alarm on the serial log.  Only if every
 * attempt fails is "DHT22 read failed" printed, so a persistent fault is
 * still reported honestly (section 19: each attempt is finite work followed
 * by a blocking delay).
 *
 * On real hardware the delay between attempts should be raised to the
 * sensor's 2 s minimum sampling period; 100 ms is safe in simulation and
 * keeps the retry inside the current 2 s sample slot.
 */
#define SENSOR_READ_ATTEMPTS       3U
#define SENSOR_RETRY_DELAY_MS    100U

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
 * The host start pulse (>= 1 ms low) runs with the scheduler still enabled:
 * the output register holds the level across any preemption, so nothing
 * time-critical can be disturbed there.  The sensor's answer and the frame
 * - the microsecond-scale part - run inside a single suspension window, so
 * no other task may preempt the bit timing, yet the SysTick interrupt keeps
 * running so the HAL time base is not disturbed.  xTaskResumeAll() replays
 * the pended ticks, so the kernel tick count does not slip.
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
