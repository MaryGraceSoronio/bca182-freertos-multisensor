/**
 * main.cpp - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Target   : STM32 Blue Pill (STM32F103C8T6), Wokwi simulation
 * Framework: STM32Cube (HAL + CMSIS) via PlatformIO - no Arduino
 *
 * Milestone 2 (PART II, section 16 of the specification):
 * bring-up of the Blue Pill alone with a recognizable serial banner.
 *
 * Structure required by section 41:
 *
 *     hardware initialization
 *             |
 *     FreeRTOS object creation      <- from milestone 3 onwards
 *             |
 *     task creation                 <- from milestone 3 onwards
 *             |
 *     scheduler-driven operation    <- from milestone 3 onwards
 */

#include <string.h>

#include "stm32f1xx_hal.h"

#define BANNER_1  "BCA182 FreeRTOS Multisensor\r\n"
#define BANNER_2  "System starting...\r\n"

static UART_HandleTypeDef huart1;

static void SystemClock_Config(void);
static void Periph_GPIO_Init(void);
static void UART1_Init(void);
static void Serial_Write(const char *text);
static void Error_Handler(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    Periph_GPIO_Init();
    UART1_Init();

    Serial_Write(BANNER_1);
    Serial_Write(BANNER_2);

    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(500);
    }
}

/**
 * Clock tree.
 *
 * Preferred: HSE 8 MHz x9 PLL -> 72 MHz SYSCLK (APB1 = 36 MHz, APB2 = 72 MHz).
 * Fallback : HSI 8 MHz, PLL off, in case the simulated board does not start
 *            its external crystal. HAL_RCC_OscConfig() returns a timeout
 *            status instead of hanging, so the fallback is safe.
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

    if (HAL_RCC_OscConfig(&osc) == HAL_OK) {
        usePll = 1U;
    } else {
        osc.OscillatorType     = RCC_OSCILLATORTYPE_HSI;
        osc.HSIState           = RCC_HSI_ON;
        osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        osc.HSEState           = RCC_HSE_OFF;
        osc.PLL.PLLState       = RCC_PLL_OFF;
        if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
            Error_Handler();
        }
    }

    clk.ClockType       = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                          RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource    = usePll ? RCC_SYSCLKSOURCE_PLLCLK : RCC_SYSCLKSOURCE_HSI;
    clk.AHBCLKDivider   = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider  = usePll ? RCC_HCLK_DIV2 : RCC_SYSCLK_DIV1;
    clk.APB2CLKDivider  = RCC_SYSCLK_DIV1;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        Error_Handler();
    }
}

/** PC13 = onboard LED (active low). PA9/PA10 = USART1 TX/RX. */
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

    gpio.Pin  = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
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

    if (HAL_UART_Init(&huart1) != HAL_OK) {
        Error_Handler();
    }
}

static void Serial_Write(const char *text)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)text, (uint16_t)strlen(text), 1000U);
}

static void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}
