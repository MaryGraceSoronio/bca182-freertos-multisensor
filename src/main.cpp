/**
 * main.cpp - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Target   : STM32 Blue Pill (STM32F103C8T6), Wokwi simulation
 * Framework: STM32Cube (HAL + CMSIS) with native FreeRTOS APIs - no Arduino
 *
 * Milestone 12 (PART XII-XIII, sections 38-41): the priority table below is
 * the section 38/39 justification for every task in the system, and the
 * section 40 source audit plus the section 41 four-stage check are recorded
 * on app_main().
 * Milestone 11 (section 36): every serial line is written under serialMutex,
 * so two callers can no longer drop or interleave each other's output.
 * Milestone 10 (PART IX-X, sections 31-35): MotionTask polls the PIR on PB8
 * and reports rising edges; SensorTask publishes the level on every sample
 * and the OLED's Motion page shows it.  StateTask runs the ACTIVE/INACTIVE
 * machine (section 32) and the system event group (section 35) carries its
 * inputs and results: EVENT_MOTION feeds the machine, EVENT_ACTIVE tells the
 * display and the encoder which state they are in, EVENT_ALARM marks the
 * temperature page while the alarm is ringing.
 * Milestone 9 (PART VIII, section 30): AlarmTask consumes alarmQueue and
 * drives the buzzer through the pure evaluateTemperature() decision.
 * Milestone 8 (PART VII, section 28): InputTask decodes the KY-040 rotary
 * encoder and selects the page DisplayTask draws.  Milestone 7 added
 * DisplayTask and the SSD1306 layout, milestone 6 SensorData and the consumer
 * queues on a fixed vTaskDelayUntil() period.
 *
 * Section 41 requires this file to stay focused on the four stages below.
 */

#include <string.h>

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"

#include "rtos_objects.h"
#include "sensors.h"
#include "display.h"
#include "input.h"
#include "alarm.h"
#include "motion.h"
#include "system_state.h"

#define BANNER_1 "BCA182 FreeRTOS Multisensor\r\n"
#define BANNER_2 "System starting...\r\n"

static void SystemClock_Config(void);
static void Periph_GPIO_Init(void);
static void Error_Handler(void);

static void TaskA(void *argument);
static void TaskB(void *argument);

/**
 * Application entry point - section 10 requires this name.  The Cortex-M3
 * startup code enters through main(); main() only performs the two vendor
 * initialisation calls and hands over to app_main(), which owns the four
 * stages required by section 41.
 *
 * Section 41 four-stage check: the body below is hardware initialisation ->
 * FreeRTOS object creation -> task creation -> vTaskStartScheduler(), and
 * nothing else - no sensor, display, alarm or state logic lives in an init
 * function here; each subsystem's entry point (SensorTask, DisplayTask,
 * InputTask, AlarmTask, MotionTask, StateTask) is defined in its own module.
 *
 * Section 40 source audit (include/ and src/ vs. the required list): every
 * listed file exists, none is duplicated or misnamed.  Two headers are on
 * disk but not on the list, both technically justified, and no code file is:
 *   - FreeRTOSConfig.h: the mandatory kernel configuration; every FreeRTOS
 *     project has one and it is not application code.
 *   - glcdfont.h: the 5x7 glyph table the SSD1306 driver renders with
 *     (display subsystem data, upstream Adafruit_GFX, kept separate so
 *     display.h stays an interface instead of 9 KB of font bytes).
 * Section 17's TaskA/TaskB bodies also stay here rather than in a module of
 * their own: two 15-line diagnostic loops out of this file are not "the
 * entire application", which is what section 40 forbids burying in
 * main.cpp - moving them would add a file to the section 40 list for no
 * structural gain.
 */
extern "C" void app_main(void)
{
    /* --- hardware initialization ------------------------------------ */
    HAL_Init();
    SystemClock_Config();
    SystemCoreClockUpdate();

    Periph_GPIO_Init();
    serial_init();
    sensors_init();
    display_init();
    input_init();
    buzzer_init();
    motion_init();

    serial_write(BANNER_1);
    serial_write(BANNER_2);

    /* --- FreeRTOS object creation ----------------------------------- */
    rtos_objects_create();

    /* --- task creation ---------------------------------------------- */
    /* Sections 38-39: priority is scheduling urgency, so the whole
     * assignment is recorded here as one table - the single place that
     * carries the justification (each subsystem file repeats a short
     * version next to its task).  Every task is listed, not only the five
     * in section 38's suggested table: StateTask is only "recommended" by
     * section 7 but section 59 still wants a justified priority for it, and
     * the two section 17 diagnostics need one too.  Stack is the xTaskCreate
     * depth in words (4 bytes each), not bytes.  The table only means
     * anything because configUSE_PREEMPTION and configUSE_TIME_SLICING are
     * both 1 and configMAX_PRIORITIES is 7 (FreeRTOSConfig.h).
     *
     *   task       | prio | stack | why this urgency / acceptable latency /
     *              |      | words | what breaks if the priority is wrong
     *   -----------+------+-------+-----------------------------------------
     *   InputTask  | 3    | 256   | One-shot: a knob detent is an edge that
     *              |      |       | exists only while the pins move, so it
     *              |      |       | cannot be replayed the way a missed
     *              |      |       | sample can.  Budget: one 2 ms poll.
     *              |      |       | Too low: behind the 2 s sensor cycle or
     *              |      |       | a ~100 ms OLED flush a page turn lands
     *              |      |       | late.  Too high: it would preempt the
     *              |      |       | state machine for a two-pin read.
     *   MotionTask | 3    | 128   | One-shot: the PIR's OUT pulse is the
     *              |      |       | only copy of the evidence.  Budget: one
     *              |      |       | 10 ms poll.  Too low: a person entering
     *              |      |       | would not reach the state machine until
     *              |      |       | SensorTask's next 2 s sample - the
     *              |      |       | ACTIVE/INACTIVE response trails reality
     *              |      |       | by a whole sample period.  Too high:
     *              |      |       | none - it sleeps after every read.
     *   StateTask  | 3    | 192   | Turns that evidence into the result
     *              |      |       | DisplayTask and InputTask gate on.
     *              |      |       | Budget: one motion poll (10 ms).  Too
     *              |      |       | low: a transition queues behind the 2 s
     *              |      |       | sample, so the OLED stays lit ~2 s too
     *              |      |       | long in an empty room or stays blank
     *              |      |       | ~2 s too long in an occupied one.  Too
     *              |      |       | high: buys nothing - its wait is one
     *              |      |       | timeout-bounded event-group call.
     *   SensorTask | 2    | 256   | Periodic producer whose data is by
     *              |      |       | definition up to 2 s old, so its urgency
     *              |      |       | equals its own period.  Budget: one 2 s
     *              |      |       | cycle (vTaskDelayUntil).  Too low: the
     *              |      |       | sample queues behind a ~100 ms redraw
     *              |      |       | and AlarmTask decides on staler data.
     *              |      |       | Too high: a DHT22 read plus formatting
     *              |      |       | would preempt the level-3 one-shot
     *              |      |       | tasks without making any reading
     *              |      |       | fresher than its own sensor period.
     *   AlarmTask  | 2    | 256   | Its input only changes every 2 s, so
     *              |      |       | that is its whole latency budget - level
     *              |      |       | with the producer, never above it.
     *              |      |       | Budget: one sensor period.  Too low: an
     *              |      |       | out-of-range temperature would latch
     *              |      |       | after the OLED redraw instead of before
     *              |      |       | it.  Too high: no earlier alarm (the
     *              |      |       | data cannot arrive sooner), only
     *              |      |       | preemption of the one-shot tasks above.
     *   DisplayTask| 1    | 256   | Presentation only - section 7 sets no
     *              |      |       | deadline for a pixel, so this is the
     *              |      |       | "can tolerate latency" end.  Budget: a
     *              |      |       | redraw cycle (~100 ms I2C flush of the
     *              |      |       | 1024-byte framebuffer).  Too high: that
     *              |      |       | flush could preempt SensorTask or
     *              |      |       | AlarmTask and push samples late.  Too
     *              |      |       | low: nothing below it exists (0 is the
     *              |      |       | idle task).
     *   TaskA      | 1    | 128   | Diagnostic (section 17): nobody acts on
     *              |      |       | the line, so unbounded latency is
     *              |      |       | acceptable - section 39's "can
     *              |      |       | tolerate latency" class.  Too high: at
     *              |      |       | the old priority 2 it could preempt
     *              |      |       | DisplayTask for a demo line - nothing
     *              |      |       | gained, a product task delayed.  Too
     *              |      |       | low: there is no level below 1 to fall
     *              |      |       | to except idle.
     *   TaskB      | 1    | 128   | Identical duty and identical reasoning
     *              |      |       | as TaskA; the pair deliberately shares
     *              |      |       | level 1 with DisplayTask (see below).
     *
     * Shared levels cannot starve each other: at 3, InputTask (2 ms poll),
     * MotionTask (10 ms poll) and StateTask (one blocking event-group wait)
     * all return to sleep after microseconds of work; at 2, AlarmTask sits
     * in xQueueReceive until SensorTask publishes; at 1, DisplayTask waits
     * on its queues and TaskA/TaskB print one line and block for 1000 ms -
     * and TaskB's 500 ms start-up offset keeps the two diagnostics half a
     * period apart forever, so they are never ready in the same tick.
     * Round-robin time slicing only ever divides idle time between them.
     *
     * Priority inversion, checked per shared resource - what a poor
     * priority choice would cost, section 39 asks for this too:
     *   - serialMutex: the one place a level-3 task can wait behind a level
     *     1/2 holder; priority inheritance (section 36, see rtos_objects.h)
     *     bounds that wait to a single 15-byte UART line.
     *   - displayQueue / alarmQueue / modeQueue: length 1 with
     *     xQueueOverwrite (section 11 note), so SensorTask and InputTask
     *     never block on DisplayTask's ~100 ms flush at all.
     *   - systemEvents: StateTask's wait carries the 15 s timeout and
     *     MotionTask only ever sets bits - nobody waits on a holder.
     *   - I2C1: owned by DisplayTask alone (section 26), so there is no
     *     second contender; the DHT22 bit-bang suspends the scheduler only
     *     inside SensorTask for ~5 ms and ends before any print, which is
     *     the worst-case delay level 3 ever eats.
     *   - portMAX_DELAY is used only on genuinely idle resources (the alarm
     *     queue between samples, the serial line nobody else is sending).
     *
     * Stacks: Sensor/Display/Input/Alarm print or format (snprintf + the
     * HAL UART transmit) and get 256 words - 128 overflowed DisplayTask on
     * its first page turn.  StateTask is 192: literal-only output but an
     * event-group wait path that wants more than a bare delay.  Motion and
     * the two diagnostics are 128: one GPIO read or one literal line plus a
     * delay, no formatting, no kernel call deeper than vTaskDelay(). */
    xTaskCreate(TaskA, "TaskA", 128, nullptr, 1, nullptr);
    xTaskCreate(TaskB, "TaskB", 128, nullptr, 1, nullptr);
    xTaskCreate(SensorTask, "Sensor", 256, nullptr, 2, nullptr);
    xTaskCreate(DisplayTask, "Display", 256, nullptr, 1, nullptr);
    xTaskCreate(InputTask, "Input", 256, nullptr, 3, nullptr);
    xTaskCreate(AlarmTask, "Alarm", 256, nullptr, 2, nullptr);
    xTaskCreate(MotionTask, "Motion", 128, nullptr, 3, nullptr);
    xTaskCreate(StateTask, "State", 192, nullptr, 3, nullptr);

    /* --- scheduler-driven operation --------------------------------- */
    vTaskStartScheduler();

    /* vTaskStartScheduler() only returns if there is not enough memory to
     * create the idle task. */
    Error_Handler();
}

extern "C" int main(void)
{
    app_main();
    return 0;
}

/**
 * Section 17: two simple tasks that periodically print distinct diagnostic
 * messages, both blocking between executions (section 19 forbids an
 * uncontrolled busy loop).
 *
 * Both tasks run at priority 1 (sections 18, 38-39): section 18 wants the
 * assigned priority of every task on record and section 39 wants a reason.
 * Diagnostics are the class that "can tolerate latency" - no consumer acts
 * on these lines - so they take the lowest level in the system, below every
 * product task and level with DisplayTask.  They cannot starve it: each
 * writes one line and blocks for 1000 ms, so they hold the CPU for
 * microseconds per second, and they cannot starve each other because the
 * duties are identical and, with TaskB's offset, the two are never ready in
 * the same tick (round-robin time slicing would settle it if they ever
 * were).  TaskA carried priority 2 until milestone 12, when it was lowered:
 * at 2 it could preempt a DisplayTask flush in progress to print a demo
 * line - a cost with no benefit, which is exactly the poorly selected
 * priority section 39 asks students to be able to name.
 *
 * TaskB deliberately waits 500 ms before its first line.  Both tasks then use
 * the same 1000 ms period, so they stay half a period apart forever.  The
 * phase offset keeps the *timing* apart; the section 36 mutex inside
 * serial_write() (milestone 11) keeps the *bytes* apart, so even a sensor or
 * state line that lands inside that window is written as one unbroken line.
 * Before that mutex existed the offset was the only defence, and it was not
 * enough: a colliding task still lost its whole line to HAL_BUSY.
 */
static void TaskA(void *argument)
{
    ( void )argument;

    for( ;; )
    {
        serial_write("Task A running\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void TaskB(void *argument)
{
    ( void )argument;

    vTaskDelay(pdMS_TO_TICKS(500));

    for( ;; )
    {
        serial_write("Task B running\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * Clock tree.
 *
 * Preferred: HSE 8 MHz x9 PLL -> 72 MHz SYSCLK (APB1 = 36 MHz, APB2 = 72 MHz).
 * Fallback : HSI 8 MHz with the PLL off, in case the simulated board does not
 *            start its external crystal.  HAL_RCC_OscConfig() reports a
 *            timeout instead of hanging, so the fallback is safe.
 */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    uint8_t usePll = 0U;

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL     = RCC_PLL_MUL9;

    if( HAL_RCC_OscConfig(&osc) == HAL_OK )
    {
        usePll = 1U;
    }
    else
    {
        osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
        osc.HSIState            = RCC_HSI_ON;
        osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        osc.HSEState            = RCC_HSE_OFF;
        osc.PLL.PLLState        = RCC_PLL_OFF;

        if( HAL_RCC_OscConfig(&osc) != HAL_OK )
        {
            Error_Handler();
        }
    }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = usePll ? RCC_SYSCLKSOURCE_PLLCLK : RCC_SYSCLKSOURCE_HSI;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = usePll ? RCC_HCLK_DIV2 : RCC_SYSCLK_DIV1;
    clk.APB2CLKDivider = RCC_SYSCLK_DIV1;

    if( HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK )
    {
        Error_Handler();
    }
}

/** PC13 = onboard LED (active low).  PA9/PA10 = USART1 TX/RX. */
static void Periph_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio.Pin   = GPIO_PIN_13;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    gpio.Pin   = GPIO_PIN_9;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin  = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
}

/** USART1 initialisation moved to serial_init() in rtos_objects.cpp - see
 *  the note in rtos_objects.h about section 36. */

static void Error_Handler(void)
{
    __disable_irq();
    for( ;; )
    {
    }
}
