/**
 * FreeRTOS.h - host stub for the unit tests (sections 42-44)
 *
 * See the note in stm32f1xx_hal.h: the native environment compiles
 * src/alarm.cpp unchanged, and rtos_objects.h (included by that file) pulls
 * in the real FreeRTOS names.  Only the types and constants the alarm
 * sources use are declared here; the test itself never starts a scheduler.
 */

#ifndef FREERTOS_H
#define FREERTOS_H

#include <stdint.h>

typedef uint32_t TickType_t;
typedef int     BaseType_t;
typedef unsigned int UBaseType_t;

#define portMAX_DELAY ( ( TickType_t )0xffffffffUL )

#define pdTRUE  ( ( BaseType_t )1 )
#define pdFALSE ( ( BaseType_t )0 )

#endif /* FREERTOS_H */
