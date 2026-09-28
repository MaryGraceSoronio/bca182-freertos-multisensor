/**
 * queue.h - host stub for the unit tests (sections 42-44)
 *
 * Declares the one queue API AlarmTask() calls.  xQueueReceive() cannot
 * block on a host build - there is no scheduler - so it simply reports
 * "nothing received"; AlarmTask() is never called by the tests, the stub
 * exists only so the translation unit links.
 */

#ifndef QUEUE_H
#define QUEUE_H

#include "FreeRTOS.h"

typedef void * QueueHandle_t;

static inline BaseType_t xQueueReceive( QueueHandle_t queue, void *buffer,
                                        TickType_t ticks )
{
    ( void )queue;
    ( void )buffer;
    ( void )ticks;
    return pdFALSE;
}

#endif /* QUEUE_H */
