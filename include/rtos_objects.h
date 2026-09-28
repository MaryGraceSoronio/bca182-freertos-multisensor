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
#include "event_groups.h"

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
 * The page InputTask selected (section 28), handed to DisplayTask.
 *
 * Same length-one + xQueueOverwrite() pattern as the sensor queues, which
 * gives "latest page wins": a turn that happens while the display is busy
 * updating is never lost and never queued up behind an older one.  It is a
 * queue rather than a direct call or a shared variable so that DisplayTask -
 * the only task allowed to touch the OLED (section 26) - decides when to
 * re-render, and InputTask never blocks on the I2C transfer.
 *
 * Valid only after rtos_objects_create() has run.
 */
extern QueueHandle_t modeQueue;

/*---------------------------------------------------------------
 * Section 35 - event signalling: the system event group
 *
 * The section 35 example names three bits.  Neither FreeRTOS nor the CMSIS
 * headers define BIT0/BIT1/BIT2 (they are bare-metal convenience macros),
 * so they are provided here, guarded in case a future header adds them.
 *-------------------------------------------------------------*/
#ifndef BIT0
#define BIT0 ( 1UL << 0 )
#endif
#ifndef BIT1
#define BIT1 ( 1UL << 1 )
#endif
#ifndef BIT2
#define BIT2 ( 1UL << 2 )
#endif

#define EVENT_ACTIVE BIT0
#define EVENT_MOTION BIT1
#define EVENT_ALARM  BIT2

/**
 * The section 35 documentation table: each bit, its producer, its consumers
 * and exactly when it is set or cleared.
 *
 *  Bit  | Name         | Producer   | Consumer(s)             | Set / cleared
 *  -----+--------------+------------+-------------------------+---------------------------------------------
 *  BIT0 | EVENT_ACTIVE | StateTask  | DisplayTask, InputTask  | Set on the transition into ACTIVE (including
 *       |              |            |                         | the boot state), cleared on the transition
 *       |              |            |                         | into INACTIVE; it always mirrors the current
 *       |              |            |                         | state machine state, so "clear" = INACTIVE.
 *  BIT1 | EVENT_MOTION | MotionTask | StateTask               | Set on every poll while PIR OUT reads high
 *       |              |            |                         | (level, not edge - see motion.cpp); StateTask
 *       |              |            |                         | clears it when it consumes it (wait with
 *       |              |            |                         | clear-on-exit).
 *  BIT2 | EVENT_ALARM  | AlarmTask  | DisplayTask             | Set when evaluateTemperature() first reports
 *       |              |            |                         | (or continues) a non-NORMAL state, cleared
 *       |              |            |                         | when it reports NORMAL again.
 *
 * Read-side convention: consumers use xEventGroupGetBits() (a read that never
 * blocks and never clears), so only StateTask consumes EVENT_MOTION with
 * clear-on-exit; the two state bits are written by their producers alone.
 *
 * Valid only after rtos_objects_create() has run.
 */
extern EventGroupHandle_t systemEvents;

/**
 * Create every FreeRTOS object the application uses: the sensor queues, the
 * system event group (section 35) and - in a later milestone - the
 * serial-output mutex.
 *
 * Must be called after hardware initialisation and before the first
 * xTaskCreate(), so that no task can observe a half-built object.
 *
 * Milestone 6: displayQueue and alarmQueue.  Milestone 8 adds modeQueue.
 * Milestone 10 adds systemEvents.
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
