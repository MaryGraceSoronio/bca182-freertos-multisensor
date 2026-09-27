/**
 * FreeRTOSConfig.h - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Target : STM32F103C8T6 (STM32 Blue Pill), 72 MHz, 20 KB RAM, 64 KB flash
 * Kernel : FreeRTOS (bundled with the framework-stm32cubef1 package)
 *
 * Section 6 of the specification requires native FreeRTOS APIs.  This file is
 * the single place where kernel behaviour is configured.
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif
/* Declared by system_stm32f1xx.c.  Kept up to date after every clock change,
 * so the kernel always derives its tick reload from the real core clock. */
extern uint32_t SystemCoreClock;
#ifdef __cplusplus
}
#endif

/*-----------------------------------------------------------
 * Scheduler
 *----------------------------------------------------------*/
#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE                 0
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TIME_SLICING                  1

/*-----------------------------------------------------------
 * Clocks
 *----------------------------------------------------------*/
#define configCPU_CLOCK_HZ                      ( SystemCoreClock )
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )

/*-----------------------------------------------------------
 * Memory
 *
 * The Blue Pill only has 20 KB of RAM.  9 KB of heap comfortably covers the
 * FreeRTOS objects, every task stack and the SSD1306 framebuffer while
 * leaving headroom for the HAL.
 *----------------------------------------------------------*/
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 9 * 1024 ) )
#define configAPPLICATION_ALLOCATED_HEAP        0
#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW          2

/*-----------------------------------------------------------
 * Tasks
 *----------------------------------------------------------*/
#define configMAX_PRIORITIES                    ( 7 )
#define configMINIMAL_STACK_SIZE                ( ( uint16_t ) 128 )
#define configMAX_TASK_NAME_LEN                 ( 16 )
#define configUSE_16_BIT_TICKS                  0
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configGENERATE_RUN_TIME_STATS           0

/*-----------------------------------------------------------
 * Synchronisation and inter-task communication
 *
 * Sections 25, 35, 36 and 43 of the specification require a queue, an event
 * group / task notification and a mutex, plus unit-testable decision logic.
 *----------------------------------------------------------*/
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    0
#define configQUEUE_REGISTRY_SIZE               8

/*-----------------------------------------------------------
 * Software timers and co-routines - not used by this application
 *----------------------------------------------------------*/
#define configUSE_TIMERS                        0
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         2

/*-----------------------------------------------------------
 * Optional API functions
 *----------------------------------------------------------*/
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xResumeFromISR                  1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          0
#define INCLUDE_xTaskAbortDelay                 0
#define INCLUDE_xTaskGetTaskName                1

/*-----------------------------------------------------------
 * Cortex-M3 interrupt priorities
 *
 * Interrupts that priority-encode numerically below
 * configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5) must never call a FreeRTOS
 * "FromISR" API.  No peripheral interrupt in this application does, because
 * every driver is polled from a task.
 *----------------------------------------------------------*/
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS                         __NVIC_PRIO_BITS
#else
#define configPRIO_BITS                         4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/*-----------------------------------------------------------
 * Assert
 *----------------------------------------------------------*/
#ifdef __cplusplus
extern "C" {
#endif
void vAssertCalled( const char *pcFile, uint32_t ulLine );
#ifdef __cplusplus
}
#endif

#define configASSERT( x ) \
    if( ( x ) == 0 ) { vAssertCalled( ( const char * ) __FILE__, ( uint32_t ) __LINE__ ); }

/*-----------------------------------------------------------
 * Kernel entry points
 *
 * The Cortex-M3 startup file declares SVC_Handler, PendSV_Handler and
 * SysTick_Handler as weak aliases of Default_Handler.  Renaming the two
 * context-switch handlers here installs the FreeRTOS implementations.
 * SysTick_Handler is *not* renamed: it is implemented in rtos_objects.cpp so
 * that it can also advance the STM32 HAL time base.
 *----------------------------------------------------------*/
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler

#endif /* FREERTOS_CONFIG_H */
