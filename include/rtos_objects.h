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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create every FreeRTOS object the application uses: the sensor queues, the
 * serial-output mutex and the system event group.
 *
 * Must be called after hardware initialisation and before the first
 * xTaskCreate(), so that no task can observe a half-built object.
 *
 * Milestone 3: no application objects yet - the two diagnostic tasks only
 * need the kernel itself.  Queues appear in milestone 6, the event group in
 * milestone 11 and the mutex in milestone 12.
 */
void rtos_objects_create(void);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_OBJECTS_H */
