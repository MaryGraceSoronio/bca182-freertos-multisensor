/**
 * main.cpp - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Target   : STM32 Blue Pill (STM32F103C8T6), Wokwi simulation
 * Framework: STM32Cube (HAL + CMSIS) with native FreeRTOS APIs - no Arduino
 *
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

    serial_write(BANNER_1);
    serial_write(BANNER_2);

    /* --- FreeRTOS object creation ----------------------------------- */
    rtos_objects_create();

    /* --- task creation ---------------------------------------------- */
    /* Sections 38-39: every priority is explicit and justified.
     * InputTask runs at 3, the highest so far, because a human turn of the
     * knob is a one-shot event - it cannot be re-run by the kernel the way a
     * periodic sensor sample can, so it must never queue behind the 2 s
     * sensor cycle.  Its cost is a few microseconds per 2 ms iteration
     * followed by a blocking delay (section 19), so the higher priority never
     * starves SensorTask or DisplayTask; the full reasoning is on InputTask
     * itself.
     * AlarmTask runs at 2, level with SensorTask: an alarm decision can be no
     * fresher than the sample it is given, so one sensor period (2 s) is its
     * latency budget and a higher priority would buy no earlier alarm, only
     * preemption of the task that produces the data.  It stays above
     * DisplayTask (1) so an out-of-range temperature is latched before the
     * slower OLED redraw; the full reasoning is on AlarmTask itself.
     * The stack matches Sensor/Display (256 words): publishing a page runs
     * snprintf() and the HAL UART transmit, which a 128 word stack cannot
     * hold - it overflowed on the first turn.  AlarmTask gets the same
     * headroom for its HAL UART transmit even though its lines are literals. */
    xTaskCreate(TaskA, "TaskA", 128, nullptr, 2, nullptr);
    xTaskCreate(TaskB, "TaskB", 128, nullptr, 1, nullptr);
    xTaskCreate(SensorTask, "Sensor", 256, nullptr, 2, nullptr);
    xTaskCreate(DisplayTask, "Display", 256, nullptr, 1, nullptr);
    xTaskCreate(InputTask, "Input", 256, nullptr, 3, nullptr);
    xTaskCreate(AlarmTask, "Alarm", 256, nullptr, 2, nullptr);

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
 * Priorities are explicit from the outset - section 18 asks for the assigned
 * priority of every task and section 39 requires a scheduling justification.
 * TaskA runs at priority 2, TaskB at priority 1: TaskA is allowed to answer
 * first after both wake up, which is what produces the alternating output.
 *
 * TaskB deliberately waits 500 ms before its first line.  Both tasks then use
 * the same 1000 ms period, so they stay half a period apart forever and the
 * two messages never collide on the shared USART before the serial mutex is
 * introduced in milestone 12.
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
