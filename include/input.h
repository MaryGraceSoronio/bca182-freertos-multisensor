/**
 * input.h - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Input subsystem (section 28: InputTask) - the KY-040 rotary encoder and
 * the page it selects.  The encoder is a read-only device: InputTask decodes
 * it and publishes the selected page, DisplayTask stays the only writer of
 * the OLED (section 26).
 *
 * Pin map (see README "Pin Configuration"):
 *   PB12 - encoder CLK  (rotary pin A, EXTI-free plain GPIO)
 *   PB13 - encoder DT   (rotary pin B)
 *   PB14 - encoder SW   (push button, wired and pulled up; no action is
 *                       mapped to it - sections 28-29 use rotation only)
 */

#ifndef INPUT_H
#define INPUT_H

/**
 * Section 28: the displayed page, as a type instead of bare numbers.
 *
 * enum class is C++ syntax, so this part of the header sits outside the
 * extern "C" block below and is additionally guarded for C++ only - a C
 * translation unit could not parse it.  Only .cpp files include input.h.
 */
#ifdef __cplusplus

enum class DisplayMode
{
    TEMPERATURE,
    HUMIDITY,
    LIGHT,
    MOTION
};

/**
 * Section 29: navigation with wraparound in both directions.
 *
 * These two are free functions rather than file-static helpers because
 * section 42 lists nextDisplayMode()/previousDisplayMode() as the
 * recommended first unit-test targets; keeping them here lets the tests
 * call exactly the code InputTask runs.
 */
DisplayMode nextDisplayMode(DisplayMode mode);
DisplayMode previousDisplayMode(DisplayMode mode);

#endif /* __cplusplus */

#ifdef __cplusplus
extern "C" {
#endif

/** Configure the encoder GPIOs; part of hardware init (section 41). */
void input_init(void);

/** FreeRTOS task: decodes the rotary encoder and selects the page
 *  (sections 28-29). */
void InputTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* INPUT_H */
