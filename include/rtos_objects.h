/**
 * rtos_objects.h - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Section 40 of the specification requires the application to be split into
 * separate source modules.  rtos_objects owns everything that belongs to the
 * FreeRTOS "object creation" stage of section 41:
 *
 *     hardware initialization
 *             |
 *     FreeRTOS object creation     <- rtos_objects_create() lives here
 *             |
 *     task creation
 *             |
 *     scheduler-driven operation
 *
 * It also holds the Cortex-M3 kernel glue (the SysTick handler and the kernel
 * callbacks) so that the entry point in main.cpp stays focused on wiring.
 */

#ifndef RTOS_OBJECTS_H
#define RTOS_OBJECTS_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Section 25: one queue per consumer.
 *
 * A single shared queue cannot feed two consumers - a queue hands each item
 * to exactly one receiver, so DisplayTask and AlarmTask would race for every
 * sample and each would see only part of the stream.  The specification's
 * diagram is therefore implemented as two length-one queues that both carry
 * the same SensorData message, one dedicated to each consumer.  Length one
 * plus xQueueOverwrite() gives "latest sample wins" semantics: a slow
 * display never blocks the sensor task and never receives stale data.
 *
 * Valid only after rtos_objects_create() has run.
 */
extern QueueHandle_t displayQueue;
extern QueueHandle_t alarmQueue;

/**
 * Create every FreeRTOS object the application uses: the sensor queues, the
 * serial-output mutex and the system event group.
 *
 * Must be called after hardware initialisation and before the first
 * xTaskCreate(), so that no task can observe a half-built object.
 *
 * Milestone 6: displayQueue and alarmQueue.  The event group appears in
 * milestone 11 and the mutex in milestone 12.
 */
void rtos_objects_create(void);

/**
 * Bring up USART1 (115200 8N1, PA9/PA10) - part of hardware initialisation.
 *
 * The serial port lives in this module rather than in main.cpp because
 * section 36 makes it a shared resource that must be guarded by a FreeRTOS
 * mutex; keeping the peripheral handle, the writer and the mutex together
 * means there is exactly one place where that protection is applied
 * (milestone 12).  Section 40 allows the file list to differ from the
 * suggested one when there is a technical justification.
 */
void serial_init(void);

/**
 * Blocking transmit of a zero-terminated string.
 *
 * Called from task context after the scheduler has started.  Section 36's
 * mutex is taken here in milestone 12; until then the USART is used directly
 * and the diagnostic tasks keep their output phases apart instead.
 */
void serial_write(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_OBJECTS_H */
