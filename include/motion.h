/**
 * motion.h - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Motion subsystem (section 31): the PIR sensor, MotionTask and the motion
 * level the rest of the system publishes.
 *
 * Hardware findings (Wokwi part reference)
 * ----------------------------------------
 * The simulated sensor is the Wokwi part "wokwi-pir-motion-sensor"
 * (docs.wokwi.com/parts/wokwi-pir-motion-sensor).  Its pins are VCC, OUT and
 * GND, and its OUT pin is an actively driven digital output:
 *
 *   - a detection drives OUT high and holds it for the part's delayTime
 *     attribute (default 5 s), then returns it low;
 *   - the part re-triggers by default, so while motion keeps being detected
 *     OUT stays high - the delay is extended rather than a new edge
 *     produced, and
 *   - inhibitTime (default 1.2 s) only delays the *next* detection after
 *     OUT falls; it never affects the level the MCU observes.
 *
 * OUT is never high impedance, so the input needs no external pull resistor;
 * motion_init() nevertheless enables the MCU's internal pull-down so the pin
 * has a defined low level from reset until the sensor starts driving it.
 * Detection polarity is therefore active high: GPIO_PIN_SET = motion seen.
 *
 * Pin map (see README "Pin Configuration"):
 *   PB8 - PIR OUT (GPIO input, internal pull-down, active high)
 */

#ifndef MOTION_H
#define MOTION_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configure the PIR input (PB8); part of hardware initialisation (section 41).
 *
 * Called from app_main() before rtos_objects_create(), so MotionTask can
 * never start on an unconfigured pin.
 */
void motion_init(void);

/**
 * Latest PIR level as observed by MotionTask.
 *
 * SensorTask folds this into SensorData.motionDetected (section 24) so that
 * DisplayTask renders the Motion page from the same source MotionTask acts
 * on.  The value is false until MotionTask's first poll and is updated on
 * every poll; reading a bool is atomic on Cortex-M3, so no lock is needed.
 */
bool motion_detected(void);

/** FreeRTOS task: monitors PIR activity (sections 7 and 31). */
void MotionTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_H */
