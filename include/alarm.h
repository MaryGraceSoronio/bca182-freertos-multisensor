/**
 * alarm.h - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Alarm subsystem (section 30: testable alarm logic, section 7: AlarmTask).
 *
 * Section 30 asks for the temperature decision to be separated from the
 * buzzer hardware so that the decision can be unit tested without Wokwi or
 * the STM32.  This header therefore carries everything the decision needs -
 * the two section 4 limits, the AlarmState enum and evaluateTemperature() -
 * with no HAL, GPIO or FreeRTOS dependency, next to the two entry points
 * that do touch hardware (buzzer_init() and AlarmTask()).  The function is
 * defined in src/alarm.cpp, which a host build can compile as-is because
 * nothing in the declaration pulls in a peripheral header.
 *
 * Pin map (see README "Pin Configuration"):
 *   PB0 - buzzer drive, active high (polarity derived in src/alarm.cpp)
 */

#ifndef ALARM_H
#define ALARM_H

/**
 * Section 4 - LOW TEMPERATURE LIMIT = 18 degrees Celsius.
 *
 * A reading at or below this value is a low-temperature alarm (FR-07).  The
 * constant is spelled out here rather than inlined in the code so that the
 * unit test and the firmware can only ever disagree if one of them stops
 * including this header.
 */
#define TEMP_LOW_LIMIT_C  18.0f

/** Section 4 - HIGH TEMPERATURE LIMIT = 30 degrees Celsius (FR-07). */
#define TEMP_HIGH_LIMIT_C 30.0f

/**
 * Section 30: the result of the pure temperature decision.
 *
 * enum class is C++ syntax, so - as in input.h - this part of the header
 * sits outside the extern "C" block below and is guarded for C++ only; a C
 * translation unit could not parse it.  Only .cpp files include alarm.h.
 */
#ifdef __cplusplus

enum class AlarmState
{
    NORMAL,
    LOW_TEMPERATURE,
    HIGH_TEMPERATURE
};

/**
 * Section 30: decide what the room is doing, given one temperature reading.
 *
 * Pure by construction: the result depends on the argument alone, on the two
 * limits above and on nothing else - no peripheral, no queue, no clock.  The
 * boundary rule is inclusive (a reading exactly on a limit is already
 * alarmed) and is documented and asserted at the definition in src/alarm.cpp.
 *
 * @param temperature  reading in degrees Celsius
 * @return NORMAL strictly between the limits, LOW_TEMPERATURE at or below
 *         TEMP_LOW_LIMIT_C, HIGH_TEMPERATURE at or above TEMP_HIGH_LIMIT_C
 */
AlarmState evaluateTemperature(float temperature);

#endif /* __cplusplus */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configure the buzzer GPIO and force it to the inactive level.
 *
 * Part of hardware initialisation (section 41), called from app_main()
 * before the FreeRTOS objects are created, so no task can observe the line
 * in an undefined state.  Lives in alarm.h because the buzzer belongs to the
 * alarm subsystem, not because it is shared with any other one.
 */
void buzzer_init(void);

/** FreeRTOS task: evaluates alarmQueue samples and drives the buzzer
 *  (sections 7 and 30). */
void AlarmTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* ALARM_H */
