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

#include <stdio.h>
#include <string.h>

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"

#include "sensors.h"    /* SensorData - the queue item type */
#include "input.h"      /* DisplayMode - the mode queue item type */

/* Implemented by the Cortex-M3 FreeRTOS port (portable/GCC/ARM_CM3/port.c).
 * Not declared by portmacro.h, hence the prototype here. */
extern "C" void xPortSysTickHandler( void );

static UART_HandleTypeDef huart1;

QueueHandle_t displayQueue = nullptr;
QueueHandle_t alarmQueue   = nullptr;
QueueHandle_t modeQueue    = nullptr;

EventGroupHandle_t systemEvents = nullptr;

void rtos_objects_create( void )
{
    /* Length one: combined with xQueueOverwrite() the consumers always read
     * the most recent sample and the producer never blocks.  modeQueue carries
     * the page rather than a SensorData (milestone 8).  The event group
     * (milestone 10, section 35) starts with every bit clear - no state has
     * been decided yet - and StateTask claims EVENT_ACTIVE as its first act.
     * The serial mutex is added in a later milestone, with the subsystem it
     * serves. */
    displayQueue = xQueueCreate(1, sizeof(SensorData));
    alarmQueue   = xQueueCreate(1, sizeof(SensorData));
    modeQueue    = xQueueCreate(1, sizeof(DisplayMode));
    systemEvents = xEventGroupCreate();

    configASSERT(displayQueue != nullptr);
    configASSERT(alarmQueue != nullptr);
    configASSERT(modeQueue != nullptr);
    configASSERT(systemEvents != nullptr);
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

/* Register-level transmit: owns no HAL state, cannot time out and therefore
 * always delivers the whole message, even from interrupt context or while a
 * critical section is held.  Used by the fault hooks below, where the normal
 * serial_write() path (HAL + timeout) must not be trusted. */
static void serial_write_raw( const char *text )
{
    while( *text != '\0' )
    {
        while( ( USART1->SR & USART_SR_TXE ) == 0U )
        {
        }
        USART1->DR = ( uint16_t )( uint8_t )*text;
        text++;
    }
}

static void serial_write_fault( const char *text )
{
    serial_write_raw(text);
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
    serial_write_fault("ERR: malloc failed\r\n");
    taskDISABLE_INTERRUPTS();
    for( ;; )
    {
    }
}

extern "C" void vApplicationStackOverflowHook( TaskHandle_t xTask,
                                               char *pcTaskName )
{
    /* Reached with the offending task's stack already exhausted, so nothing
     * may be formatted into a local buffer here - the line is assembled from
     * string literals and the task name only. */
    serial_write_fault("ERR: stack overflow in ");
    serial_write_fault( ( pcTaskName != nullptr ) ? pcTaskName : "?" );
    serial_write_fault("\r\n");

    ( void ) xTask;
    taskDISABLE_INTERRUPTS();
    for( ;; )
    {
    }
}

extern "C" void vAssertCalled( const char *pcFile, uint32_t ulLine )
{
    char message[192];

    snprintf(message, sizeof(message), "ERR: assert %s:%lu\r\n",
             pcFile, (unsigned long)ulLine);
    serial_write_fault(message);

    ( void )pcFile;
    ( void )ulLine;
    taskDISABLE_INTERRUPTS();
    for( ;; )
    {
    }
}
