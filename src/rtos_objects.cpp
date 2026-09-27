/**
 * rtos_objects.cpp - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * FreeRTOS object creation and Cortex-M3 kernel glue.
 *
 * SysTick ownership
 * -----------------
 * STM32Cube HAL and FreeRTOS both want SysTick.  The split used here is:
 *
 *   - HAL_IncTick()  is driven from SysTick_Handler() so HAL_GetTick() and
 *                    HAL_Delay() keep working during hardware bring-up.
 *   - xPortSysTickHandler() is only forwarded once the scheduler has started.
 *     Before vTaskStartScheduler() the kernel's delayed/ready lists are not
 *     initialised yet, so incrementing the RTOS tick there would walk a NULL
 *     list pointer.  tasks.c initialises the lists when the first task is
 *     created and marks the scheduler running immediately before it hands
 *     control to the port, so xTaskGetSchedulerState() is a safe gate.
 *   - FreeRTOS then re-programs SysTick itself from vPortSetupTimerInterrupt()
 *     inside xPortStartFirstTask(), at which point the kernel owns the tick
 *     rate.  Both sides use 1 ms, so no reload conflict exists.
 *
 * Consequently HAL_Delay() must only be used before the scheduler starts
 * (hardware initialisation).  Task code uses vTaskDelay() /
 * vTaskDelayUntil() instead - section 19 forbids busy loops anyway.
 */

#include "rtos_objects.h"

#include <string.h>

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"

/* Implemented by the Cortex-M3 FreeRTOS port (portable/GCC/ARM_CM3/port.c).
 * Not declared by portmacro.h, hence the prototype here. */
extern "C" void xPortSysTickHandler( void );

static UART_HandleTypeDef huart1;

void rtos_objects_create( void )
{
    /* Queues (milestone 6), event group (milestone 11) and the serial mutex
     * (milestone 12) are added here as the subsystems they serve appear. */
}

/** USART1, 115200 8N1 - matches monitor_speed in platformio.ini. */
void serial_init( void )
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
        taskDISABLE_INTERRUPTS();
        for( ;; )
        {
        }
    }
}

void serial_write( const char *text )
{
    /* Milestone 12 wraps this HAL_UART_Transmit() call in the section 36
     * mutex.  Before the scheduler runs the mutex must not be taken, so the
     * guard is added together with the mutex itself. */
    HAL_UART_Transmit(&huart1, (uint8_t *)text, (uint16_t)strlen(text), 1000U);
}

extern "C" void SysTick_Handler( void )
{
    HAL_IncTick();

    if( xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED )
    {
        xPortSysTickHandler();
    }
}

extern "C" void vApplicationMallocFailedHook( void )
{
    taskDISABLE_INTERRUPTS();
    for( ;; )
    {
    }
}

extern "C" void vApplicationStackOverflowHook( TaskHandle_t xTask,
                                               char *pcTaskName )
{
    ( void ) xTask;
    ( void ) pcTaskName;
    taskDISABLE_INTERRUPTS();
    for( ;; )
    {
    }
}

extern "C" void vAssertCalled( const char *pcFile, uint32_t ulLine )
{
    ( void ) pcFile;
    ( void ) ulLine;
    taskDISABLE_INTERRUPTS();
    for( ;; )
    {
    }
}
