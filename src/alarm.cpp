/**
 * alarm.cpp - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Testable alarm decision (section 30) and the AlarmTask/buzzer pair that
 * acts on it (section 7, FR-07).
 *
 * Division of labour
 * ------------------
 * evaluateTemperature() reads nothing but its argument and the two section 4
 * limits from alarm.h, so the same source can be compiled for the target and
 * for a host unit test (section 42 lists it first among the things worth
 * testing).  buzzer_init() and AlarmTask() are the hardware side and are the
 * only places in this file that touch a register.
 *
 * Buzzer polarity and drive
 * -------------------------
 * Wokwi's wokwi-buzzer part (docs.wokwi.com/parts/wokwi-buzzer) is a
 * two-pin piezo with named pins "1" = Negative (black) and "2" = Positive
 * (red); the documented wiring puts pin 1 on GND and pin 2 on the MCU
 * output.  The element therefore sees the GPIO level across itself, which
 * makes the output active high: GPIO_PIN_SET is "driving", GPIO_PIN_RESET is
 * "silent" and is the level buzzer_init() forces at start-up.  The part has
 * no VCC pin, so nothing else has to be wired.
 *
 * The part is a passive piezo: a static level charges it once and bends it
 * once, while an *audible* tone needs an alternating waveform (timer PWM or
 * a bit-banged square wave).  This milestone asserts the drive line - which
 * is what "activate" means at the GPIO level - and reports the state on the
 * serial console; generating sound is deliberately out of scope here so that
 * AlarmTask stays a blocking, bounded-work task (section 19).
 *
 * Pin map (see include/alarm.h):
 *   PB0 - buzzer drive (GPIO push-pull, active high)
 */

#include "alarm.h"

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "event_groups.h"

#include "rtos_objects.h"   /* alarmQueue, serial_write(), systemEvents */
#include "sensors.h"        /* SensorData - the queue item type */

/*-----------------------------------------------------------
 * Pin assignment
 *----------------------------------------------------------*/
#define BUZZER_GPIO_PORT  GPIOB
#define BUZZER_GPIO_PIN   GPIO_PIN_0

/*-----------------------------------------------------------
 * Section 30 - pure decision logic
 *----------------------------------------------------------*/

/**
 * Decide the alarm state from one temperature reading.
 *
 * Boundary rule - the limits are inclusive: <= TEMP_LOW_LIMIT_C is LOW,
 * >= TEMP_HIGH_LIMIT_C is HIGH, everything strictly between them is NORMAL.
 * Section 4 calls 18 degrees Celsius the "low temperature limit" and 30
 * degrees Celsius the "high temperature limit", and FR-07 wants the buzzer
 * on whenever the temperature is "outside the configured normal range": with
 * this rule the normal range is the open interval (18, 30), so a room
 * sitting exactly on a limit is already alarmed instead of being silently
 * accepted.  The unit test asserts each of the two boundary values
 * explicitly, because the choice is invisible in ordinary readings.
 *
 * No HAL, no FreeRTOS, no static state - the same input always yields the
 * same output, which is the whole point of section 30.
 */
AlarmState evaluateTemperature( float temperature )
{
    if( temperature <= TEMP_LOW_LIMIT_C )
    {
        return AlarmState::LOW_TEMPERATURE;
    }

    if( temperature >= TEMP_HIGH_LIMIT_C )
    {
        return AlarmState::HIGH_TEMPERATURE;
    }

    return AlarmState::NORMAL;
}

/*-----------------------------------------------------------
 * Buzzer GPIO - the hardware half of FR-07
 *----------------------------------------------------------*/

/** PB0 as a plain push-pull output; no timer, no PWM (see the file header). */
void buzzer_init( void )
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin   = BUZZER_GPIO_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BUZZER_GPIO_PORT, &gpio);

    /* Inactive before the scheduler starts, so the line can never come up
     * in the driving state regardless of what the reset state of the ODR
     * register happened to be. */
    HAL_GPIO_WritePin(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN, GPIO_PIN_RESET);
}

/** Apply one decision to the buzzer line - active high, see the header. */
static void buzzer_write( bool active )
{
    HAL_GPIO_WritePin(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN,
                      active ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/*-----------------------------------------------------------
 * Serial report
 *----------------------------------------------------------*/

/**
 * The whole line as one literal, so a single serial_write() emits it.
 *
 * Assembling it from fragments would give a preemption the chance to slot
 * another task's line into the middle of this one - and although serial_write()
 * now holds the section 36 mutex for the whole transmit (milestone 11), a
 * literal that is emitted in one call never has to depend on that protection
 * in the first place.  The line is fixed text anyway, so no formatting is
 * needed and the task's stack stays small.
 */
static const char *alarm_line( AlarmState state )
{
    switch( state )
    {
        case AlarmState::NORMAL:           return "Alarm: NORMAL\r\n";
        case AlarmState::LOW_TEMPERATURE:  return "Alarm: LOW_TEMPERATURE\r\n";
        case AlarmState::HIGH_TEMPERATURE: return "Alarm: HIGH_TEMPERATURE\r\n";
    }

    return "Alarm: NORMAL\r\n";
}

/*-----------------------------------------------------------
 * AlarmTask - sections 7 and 25
 *----------------------------------------------------------*/

/**
 * Consume sensor samples and drive the buzzer.
 *
 * Section 25: the sample comes from alarmQueue, which SensorTask overwrites
 * every 2 s, and section 19 is respected by construction - xQueueReceive()
 * with portMAX_DELAY blocks, so the task uses no CPU while the room is
 * quiet and can never act on a stale value: a length-one queue always holds
 * the newest sample.
 *
 * The state line is printed only when the state *changes*, plus once for the
 * first sample.  A steady room therefore prints exactly one line - "Alarm:
 * NORMAL" - instead of one every sensor period, and that first line doubles
 * as the proof that the queue is really wired up; the buzzer output is
 * written on the same transitions, so both stay in step.
 *
 * Priority 2 (sections 38-39): the same level as SensorTask, because an
 * alarm can be no fresher than the sample that feeds it - the latency budget
 * is one sensor period (2 s), and running above that level would not make
 * the decision arrive any sooner, it would only preempt the task that
 * produces the data.  It stays below InputTask (3), where a one-shot human
 * turn of the knob must never queue behind an alarm evaluation, and above
 * DisplayTask (1), so an over-temperature condition is latched before the
 * slower OLED redraw rather than after it.  The cost at this priority is one
 * blocked receive, three comparisons and one GPIO write per 2 s sample, so
 * nothing can starve; had it been left at priority 1 instead, a congested
 * display update would have delayed the alarm by up to a whole redraw.
 *
 * Section 35: AlarmTask is the producer of EVENT_ALARM.  The bit is set
 * whenever the evaluation leaves NORMAL and cleared whenever it returns to
 * NORMAL - the same transitions that drive the buzzer, so the buzzer line,
 * the "Alarm: ..." serial line and the bit always agree.  DisplayTask is
 * the consumer and marks the temperature page while the bit is set.
 */
void AlarmTask( void *argument )
{
    SensorData sample = {};
    AlarmState state = AlarmState::NORMAL;
    bool have_state = false;

    ( void )argument;

    for( ;; )
    {
        ( void )xQueueReceive(alarmQueue, &sample, portMAX_DELAY);

        AlarmState next = evaluateTemperature(sample.temperature);

        if( have_state && ( next == state ) )
        {
            continue;
        }

        state      = next;
        have_state = true;

        if( next == AlarmState::NORMAL )
        {
            ( void )xEventGroupClearBits(systemEvents, EVENT_ALARM);
        }
        else
        {
            ( void )xEventGroupSetBits(systemEvents, EVENT_ALARM);
        }

        buzzer_write(next != AlarmState::NORMAL);
        serial_write(alarm_line(next));
    }
}
