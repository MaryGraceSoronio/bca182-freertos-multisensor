/**
 * motion.cpp - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * PIR driver and MotionTask (section 31: "Create MotionTask to monitor
 * motion").
 *
 * Polling strategy
 * ----------------
 * MotionTask reads the PIR OUT pin every MOTION_POLL_PERIOD_MS and then
 * sleeps with a blocking vTaskDelay() - section 19 forbids an uncontrolled
 * busy loop, and a Wokwi detection holds OUT high for delayTime seconds (5 s
 * by default, see include/motion.h), so even a 100 Hz poll lands many samples
 * inside every pulse and can never miss an edge.  10 ms was chosen over the
 * 2 ms the encoder uses because nothing here needs millisecond resolution:
 * half as many wake-ups leave more of every tick to the display and the
 * sensor cycle, and the task's duty cycle stays well under one percent at
 * priority 3.  Between reads it sleeps, so it holds no CPU while the room is
 * quiet.
 *
 * Edge-triggered reporting
 * ------------------------
 * The serial line reports only the 0 -> 1 transition ("Motion: detected"),
 * so one detection produces exactly one line instead of one per poll; a
 * steady state - idle or continuously re-triggered - prints nothing and the
 * log stays readable.  The level itself is published through
 * motion_detected(), which SensorTask copies into every sample.
 *
 * Priority 3 (sections 38-39): a person entering the room is a one-shot
 * event - if this task queued behind the 2 s sensor cycle a detection could
 * be reported late by up to two seconds - and the work at that priority is
 * one GPIO read, one comparison and one 10 ms blocking delay per iteration,
 * so it cannot starve the tasks below it.  The full justification lives on
 * MotionTask itself.
 *
 * Pin map (see include/motion.h):
 *   PB8 - PIR OUT (GPIO input, internal pull-down, active high)
 */

#include "motion.h"

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"

#include "rtos_objects.h"   /* serial_write() */

/*-----------------------------------------------------------
 * Pin assignment and tuning constants
 *----------------------------------------------------------*/
#define PIR_GPIO_PORT  GPIOB
#define PIR_GPIO_PIN   GPIO_PIN_8

/** Poll period.  See the polling-strategy note in the file header. */
#define MOTION_POLL_PERIOD_MS  10U

/** Latest level published to the rest of the system (see motion_detected()). */
static volatile bool motion_level = false;

/*-----------------------------------------------------------
 * GPIO bring-up
 *----------------------------------------------------------*/

/**
 * PB8 as a plain input with the internal pull-down.
 *
 * The Wokwi PIR drives OUT actively in both states (include/motion.h), so no
 * external resistor is required and the weak internal pull-down never fights
 * the sensor; it only guarantees a defined low from reset - and while the
 * sensor is unpowered - so a floating pin can never masquerade as motion.
 * STM32F1 HAL has no GPIO_MODE_INPUT_PULLDOWN shorthand either: mode and pull
 * are separate fields, hence GPIO_MODE_INPUT + GPIO_PULLDOWN.
 */
void motion_init( void )
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin   = PIR_GPIO_PIN;
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(PIR_GPIO_PORT, &gpio);
}

static bool pir_output_high( void )
{
    return ( HAL_GPIO_ReadPin(PIR_GPIO_PORT, PIR_GPIO_PIN) == GPIO_PIN_SET );
}

bool motion_detected( void )
{
    return motion_level;
}

/*-----------------------------------------------------------
 * MotionTask - sections 7 and 31
 *----------------------------------------------------------*/

/**
 * Monitor PIR activity and report rising edges.
 *
 * Section 31 asks for a MotionTask with a short inactivity timeout for
 * laboratory testing; the timeout itself is owned by StateTask (section 32),
 * this task only produces the motion evidence it consumes.  Section 19 is
 * satisfied by construction: the loop does finite work (one GPIO read, a
 * comparison, at most one serial line) and then blocks for 10 ms.
 *
 * The filter state is primed from the real pin level before the loop so that
 * a line that happens to read high at start-up is treated as "already in
 * progress" instead of inventing a detection nobody caused.
 *
 * Priority 3 (sections 38-39): tied with InputTask as the highest in the
 * system, because a motion edge is a one-shot event that exists for only as
 * long as it takes this task to sample it - deferring it by a sensor period
 * would put the whole ACTIVE/INACTIVE response (section 32) behind a
 * schedule that has nothing to do with the person who walked in.  The cost
 * at this priority is bounded to microseconds per 10 ms, so the higher
 * priority never starves SensorTask, AlarmTask or DisplayTask; the reasoning
 * is repeated here because section 39 rejects "it is important" as a
 * justification.
 */
void MotionTask( void *argument )
{
    ( void )argument;

    bool previous = pir_output_high();

    motion_level = previous;

    for( ;; )
    {
        bool level = pir_output_high();

        motion_level = level;

        if( level )
        {
            if( !previous )
            {
                serial_write("Motion: detected\r\n");
            }
        }

        previous = level;

        vTaskDelay(pdMS_TO_TICKS(MOTION_POLL_PERIOD_MS));
    }
}
