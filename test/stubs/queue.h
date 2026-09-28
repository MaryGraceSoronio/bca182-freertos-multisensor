/**
 * queue.h - host stub for the unit tests (sections 42-44)
 *
 * Declares the queue APIs the application sources call: xQueueReceive() from
 * AlarmTask() and xQueueOverwrite() from InputTask().  Neither can block on a
 * host build - there is no scheduler - so the first reports "nothing received"
 * and the second reports "accepted"; neither task is ever called by the tests,
 * the stubs exist only so the translation units link.
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

/** Length-one "latest wins" write used for modeQueue (section 28).  The host
 *  has no queue to write to; the call is reported as accepted. */
static inline BaseType_t xQueueOverwrite( QueueHandle_t queue,
                                          const void *buffer )
{
    ( void )queue;
    ( void )buffer;
    return pdTRUE;
}

#endif /* QUEUE_H */
