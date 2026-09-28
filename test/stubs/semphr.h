/**
 * semphr.h - host stub for the unit tests (sections 42-44)
 *
 * rtos_objects.h declares serialMutex (the section 36 mutex added in
 * milestone 11), so every translation unit that includes it - alarm.cpp in
 * the native environment included - needs the SemaphoreHandle_t name to
 * resolve.  Like the queue and event-group stubs there is no scheduler
 * behind these calls and no test reaches serial_write(); the stub exists
 * only so the translation units compile and link on the host.  The firmware
 * build never sees this file: test/stubs is on the include path of
 * [env:native] only.
 */

#ifndef SEMPHR_H
#define SEMPHR_H

#include "FreeRTOS.h"

typedef void * SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateMutex( void )
{
    return ( SemaphoreHandle_t )0;
}

static inline BaseType_t xSemaphoreTake( SemaphoreHandle_t semaphore,
                                         TickType_t ticks )
{
    ( void )semaphore;
    ( void )ticks;
    return pdTRUE;
}

static inline BaseType_t xSemaphoreGive( SemaphoreHandle_t semaphore )
{
    ( void )semaphore;
    return pdTRUE;
}

#endif /* SEMPHR_H */
