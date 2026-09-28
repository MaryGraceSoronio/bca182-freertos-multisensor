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
#include "semphr.h"

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

/*---------------------------------------------------------------
 * Section 36 - shared-resource protection: the serial-output mutex
 *
 * (This block is the section 37 material: resource, competing tasks and
 * failure mode, written so the report can be lifted straight from it.)
 *
 * The shared resource
 * -------------------
 * USART1 transmit, reached only through serial_write(): the peripheral's
 * single TX path, its HAL handle `huart1` and the HAL state machine inside
 * it (gState / TxXferCount / the transmit loop that polls TXE).  There is
 * exactly one USART1 and one handle, and every diagnostic line in the system
 * funnels through them, which is why the protection belongs at that one
 * choke point and not in the callers.
 *
 * The competing tasks
 * -------------------
 * Every serial_write() caller in the system (the list was taken from
 * `grep -n "serial_write("` over src/, so nothing can be missed):
 *
 *   Caller           | Priority | Lines it emits
 *   -----------------+----------+------------------------------------------------
 *   TaskA            |    2     | "Task A running"                     (section 17)
 *   TaskB            |    1     | "Task B running"                     (section 17)
 *   SensorTask       |    2     | "Temperature: ...", "Humidity: ...",
 *                    |          | "Light level: ...", "DHT22 read failed",
 *                    |          | "LDR read failed"
 *   InputTask        |    3     | "Page: ..."
 *   MotionTask       |    3     | "Motion: detected"
 *   StateTask        |    3     | "State: ACTIVE" / "State: INACTIVE"
 *   AlarmTask        |    2     | "Alarm: NORMAL" / "LOW_..." / "HIGH_..."
 *   app_main         |    n/a   | the two banner lines, before the scheduler runs
 *
 * DisplayTask is the only task that never prints: it owns the OLED (section
 * 26) and reports nothing on the serial console.
 *
 * The failure mode the mutex prevents
 * -----------------------------------
 * HAL_UART_Transmit() refuses to start while a transmission is already
 * outstanding: if `huart1.gState` is not HAL_UART_STATE_READY it returns
 * HAL_BUSY at once, having sent nothing.  Before this milestone every caller
 * ignored that return value, so whenever two tasks reached the transmit at
 * the same moment the loser's *whole line* was silently dropped - measured
 * in simulation over 60 s: Temperature: 30 vs Humidity: 29 / Light level: 29,
 * and "Task A running" 56 vs "Task B running" 60 (four Task A lines lost),
 * plus the "State: ACTIVE" line that used to disappear after a motion
 * re-activation.  The second, latent failure mode is byte-level
 * interleaving: HAL_UART_Transmit() emits one byte per TXE poll, so a caller
 * that retried with a timeout (or any preemption inside the loop) could let
 * a second task's characters land between this line's characters, producing
 * garbled output such as "TempeHumidity: 61.20 %\r\nrature: 25.40 C".  One
 * mutex-guarded call = one atomic, unbroken line, which kills both.
 *
 * Mutex, not binary semaphore
 * ---------------------------
 * xSemaphoreCreateMutex() (not xSemaphoreCreateBinary()) because only a
 * mutex has an owner, and ownership is what enables priority inheritance:
 * if InputTask (priority 3) has to wait while SensorTask (priority 2) holds
 * the USART, the holder is boosted to the waiter's priority for the duration
 * of the critical section, so an unrelated mid-priority task cannot stretch
 * the wait - the classic unbounded priority-inversion stall a binary
 * semaphore would allow.  Section 36 shows the take/give pair; section 9
 * requires a mutex with a legitimate role, and this is it.
 *
 * Deadlock / timeout reasoning
 * ----------------------------
 * portMAX_DELAY is safe here because no caller prints from an ISR (SysTick
 * and the kernel hooks use serial_write_fault(), see rtos_objects.cpp) and
 * no caller holds a critical section or suspends the scheduler across a
 * print (the DHT22 suspension window in sensors.cpp ends before SensorTask
 * formats its lines), so nothing can be waiting on a task that is not
 * allowed to run.  serial_write() takes the mutex once and gives it once
 * around the whole transmit - never recursively, so a task can never block
 * on a mutex it already owns.
 *
 * Valid only after rtos_objects_create() has run - and serial_write() is
 * careful not to touch it before then, see the pre-scheduler note there.
 */
extern SemaphoreHandle_t serialMutex;

/**
 * Create every FreeRTOS object the application uses: the sensor queues, the
 * system event group (section 35) and the serial-output mutex (section 36).
 *
 * Must be called after hardware initialisation and before the first
 * xTaskCreate(), so that no task can observe a half-built object.
 *
 * Milestone 6: displayQueue and alarmQueue.  Milestone 8 adds modeQueue.
 * Milestone 10 adds systemEvents.  Milestone 11 adds serialMutex.
 */
void rtos_objects_create(void);

/**
 * Bring up USART1 (115200 8N1, PA9/PA10) - part of hardware initialisation.
 *
 * The serial port lives in this module rather than in main.cpp because
 * section 36 makes it a shared resource that must be guarded by a FreeRTOS
 * mutex; keeping the peripheral handle, the writer and the mutex together
 * means there is exactly one place where that protection is applied
 * (milestone 11).  Section 40 allows the file list to differ from the
 * suggested one when there is a technical justification.
 */
void serial_init(void);

/**
 * Blocking transmit of a zero-terminated string, as one atomic line.
 *
 * Section 36's mutex is taken here - around the whole HAL_UART_Transmit(),
 * so a caller never sees its line split or dropped - and released on the way
 * out.  Callers keep their ordinary signature: nobody outside this file
 * takes serialMutex, which is the entire benefit of guarding the single
 * choke point.  Before the scheduler starts the mutex must not be taken, so
 * serial_write() transmits directly in that case (see the guard documented
 * on the definition); the banner printed by app_main() therefore still
 * appears exactly once.  Kernel fault paths deliberately do NOT come through
 * here - they use serial_write_fault(), which bypasses both the HAL and the
 * mutex.
 */
void serial_write(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_OBJECTS_H */
