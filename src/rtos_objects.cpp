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
#include "semphr.h"

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

SemaphoreHandle_t serialMutex = nullptr;

void rtos_objects_create( void )
{
    /* Length one: combined with xQueueOverwrite() the consumers always read
     * the most recent sample and the producer never blocks.  modeQueue carries
     * the page rather than a SensorData (milestone 8).  The event group
     * (milestone 10, section 35) starts with every bit clear - no state has
     * been decided yet - and StateTask claims EVENT_ACTIVE as its first act.
     * serialMutex (milestone 11, section 36) guards USART1 for every
     * serial_write() caller - see the section 36/37 statement in
     * rtos_objects.h and the take/give pair in serial_write() below. */
    displayQueue = xQueueCreate(1, sizeof(SensorData));
    alarmQueue   = xQueueCreate(1, sizeof(SensorData));
    modeQueue    = xQueueCreate(1, sizeof(DisplayMode));
    systemEvents = xEventGroupCreate();

    /* A mutex, not a binary semaphore (xSemaphoreCreateMutex(), not
     * xSemaphoreCreateBinary()): only a mutex has an owner, and ownership is
     * what switches on priority inheritance - the waiter's priority is lent
     * to the holder for the length of the critical section, which is what
     * keeps a mid-priority task from stretching the wait on the USART.  A
     * binary semaphore would serialise the writes just as well but could
     * still suffer unbounded priority inversion, and section 9 wants a
     * mechanism whose properties are actually used. */
    serialMutex = xSemaphoreCreateMutex();

    configASSERT(displayQueue != nullptr);
    configASSERT(alarmQueue != nullptr);
    configASSERT(modeQueue != nullptr);
    configASSERT(systemEvents != nullptr);
    configASSERT(serialMutex != nullptr);
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

/**
 * Transmit one line atomically - section 36's take/give pair.
 *
 * The entire HAL_UART_Transmit() runs between xSemaphoreTake() and
 * xSemaphoreGive(), so to every other task the line is indivisible: it can
 * no longer be refused with HAL_BUSY (the measured defect - a whole line
 * silently vanishing whenever two tasks reached the USART together) and it
 * cannot be cut in half so that a second task's characters land between
 * this line's bytes.  One call = one unbroken line, and no caller has to
 * know the mutex exists: the protection lives at this single choke point.
 * The section 37 statement (shared resource, every competing task, both
 * failure modes) is on serialMutex in rtos_objects.h.
 *
 * Pre-scheduler guard: taking a mutex before vTaskStartScheduler() is
 * illegal - nothing can ever unblock the waiter - and app_main() prints the
 * banner before rtos_objects_create() has even built serialMutex.  So the
 * mutex is taken only while the scheduler is provably running; before that
 * the transmit happens directly, with no lock, and there is no concurrency
 * to protect against because no other task exists yet.  That is why the two
 * banner lines still appear exactly once in the log while every task line
 * afterwards is serialised.
 *
 * portMAX_DELAY cannot hang: every serial_write() caller is task context -
 * never an ISR (SysTick_Handler, the stack-overflow hook, the malloc hook
 * and vAssertCalled all use serial_write_fault() below instead) - no caller
 * disables interrupts, enters a critical section or suspends the scheduler
 * around a print, and the mutex is given back on the same path that took
 * it, so no task can be left waiting on an owner that is not allowed to run.
 */
void serial_write( const char *text )
{
    const bool locked = ( serialMutex != nullptr ) &&
                        ( xTaskGetSchedulerState() == taskSCHEDULER_RUNNING );

    if( locked )
    {
        ( void )xSemaphoreTake(serialMutex, portMAX_DELAY);
    }

    /* Return value deliberately ignored: with the mutex held this handle
     * cannot be BUSY_TX for anyone else, and a timeout here would only mean
     * the wire itself stopped - neither is a condition the caller could
     * act on. */
    HAL_UART_Transmit(&huart1, (uint8_t *)text, (uint16_t)strlen(text), 1000U);

    if( locked )
    {
        ( void )xSemaphoreGive(serialMutex);
    }
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

/* Fault reporting deliberately bypasses BOTH the HAL and the section 36
 * mutex: it can run from a hook whose stack is already exhausted, from
 * inside configASSERT() - possibly the assert that fired while serialMutex
 * was held by the very task that faulted - or from an interrupt.  A
 * non-recursive mutex taken here could deadlock precisely when the system
 * is already in trouble, so this path never touches serialMutex.  Section 37
 * therefore pairs every mutex with this free-running escape hatch. */
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
