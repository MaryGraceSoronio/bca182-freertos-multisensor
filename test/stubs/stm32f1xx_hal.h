/**
 * stm32f1xx_hal.h - host stub for the unit tests (sections 42-44)
 *
 * The native environment compiles src/alarm.cpp and src/input.cpp so that
 * the tests exercise the same objects the firmware runs.  Those files
 * legitimately include the STM32 HAL for buzzer_init()/AlarmTask() and for
 * the encoder GPIO, which a desktop compiler cannot provide, so this header
 * declares - and no-ops - exactly the handful of HAL names those functions
 * touch.  It is on the include path only for [env:native] (see
 * platformio.ini); the firmware build never sees it, and no test calls the
 * functions that use it.
 */

#ifndef STM32F1XX_HAL_H
#define STM32F1XX_HAL_H

#include <stdint.h>

typedef struct
{
    uint32_t unused;
} GPIO_TypeDef;

typedef struct
{
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
} GPIO_InitTypeDef;

typedef enum
{
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET   = 1
} GPIO_PinState;

#define GPIOB                ( ( GPIO_TypeDef * )0 )
#define GPIO_PIN_0            ( ( uint16_t )0x0001 )
#define GPIO_PIN_12           ( ( uint16_t )0x1000 )
#define GPIO_PIN_13           ( ( uint16_t )0x2000 )
#define GPIO_PIN_14           ( ( uint16_t )0x4000 )
#define GPIO_MODE_OUTPUT_PP   0x01U
#define GPIO_MODE_INPUT       0x00U
#define GPIO_NOPULL           0x00U
#define GPIO_PULLUP           0x01U
#define GPIO_SPEED_FREQ_LOW   0x00U

#define __HAL_RCC_GPIOB_CLK_ENABLE() ( ( void )0 )

static inline void HAL_GPIO_Init( GPIO_TypeDef *port, GPIO_InitTypeDef *init )
{
    ( void )port;
    ( void )init;
}

static inline GPIO_PinState HAL_GPIO_ReadPin( GPIO_TypeDef *port,
                                              uint16_t pin )
{
    ( void )port;
    ( void )pin;
    return GPIO_PIN_RESET;
}

static inline void HAL_GPIO_WritePin( GPIO_TypeDef *port, uint16_t pin,
                                      GPIO_PinState state )
{
    ( void )port;
    ( void )pin;
    ( void )state;
}

#endif /* STM32F1XX_HAL_H */
