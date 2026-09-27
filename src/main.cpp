/**
 * main.cpp - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Target   : STM32 Blue Pill (STM32F103C8T6), Wokwi simulation
 * Framework: STM32Cube (HAL + CMSIS) with native FreeRTOS APIs - no Arduino
 *
 * Milestone 3 (PART III, sections 17-19): two simple FreeRTOS tasks that both
 * block between executions, proving that the kernel is really running.
 *
 * Section 41 requires this file to stay focused on the four stages below.
 */

#include <string.h>

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"

#include "rtos_objects.h"

#define BANNER_1 "BCA182 FreeRTOS Multisensor\r\n"
#define BANNER_2 "System starting...\r\n"

static UART_HandleTypeDef huart1;

static void SystemClock_Config(void);
static void Periph_GPIO_Init(void);
static void UART1_Init(void);
static void Serial_Write(const char *text);
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
    UART1_Init();

    Serial_Write(BANNER_1);
    Serial_Write(BANNER_2);

    /* --- FreeRTOS object creation ----------------------------------- */
    rtos_objects_create();

    /* --- task creation ---------------------------------------------- */
    xTaskCreate(TaskA, "TaskA", 128, nullptr, 2, nullptr);
    xTaskCreate(TaskB, "TaskB", 128, nullptr, 1, nullptr);

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
        Serial_Write("Task A running\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void TaskB(void *argument)
{
    ( void )argument;

    vTaskDelay(pdMS_TO_TICKS(500));

    for( ;; )
    {
        Serial_Write("Task B running\r\n");
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

/** USART1, 115200 8N1 - matches monitor_speed in platformio.ini. */
static void UART1_Init(void)
{
    __HAL_RCC_USART1_CLK_ENABLE();

    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 115200;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if( HAL_UART_Init(&huart1) != HAL_OK )
    {
        Error_Handler();
    }
}

/**
 * Blocking transmit.  Only safe from task context once the scheduler is
 * running; before that it relies on HAL_GetTick(), which SysTick_Handler()
 * keeps advancing.
 */
static void Serial_Write(const char *text)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)text, (uint16_t)strlen(text), 1000U);
}

static void Error_Handler(void)
{
    __disable_irq();
    for( ;; )
    {
    }
}
