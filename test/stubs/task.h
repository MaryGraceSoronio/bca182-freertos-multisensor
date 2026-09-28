/**
 * task.h - host stub for the unit tests (sections 42-44)
 *
 * src/input.cpp is compiled by [env:native] (see platformio.ini) so that
 * test_navigation exercises the very same nextDisplayMode()/
 * previousDisplayMode() objects InputTask calls.  That file includes task.h
 * for the one blocking delay InputTask() performs at the bottom of its poll
 * loop.  InputTask() itself is never called by the tests - there is no
 * scheduler on the host - so the stub simply drops the delay and exists only
 * so the translation unit compiles and links.
 */

#ifndef TASK_H
#define TASK_H

#include "FreeRTOS.h"

/** Section 19: a blocking delay.  A no-op on the host - no scheduler, and
 *  no test ever reaches the call. */
static inline void vTaskDelay( TickType_t ticks )
{
    ( void )ticks;
}

/** Millisecond-to-tick conversion used by the poll period in input.cpp.  The
 *  host tick rate is irrelevant: nothing sleeps. */
#define pdMS_TO_TICKS( ms ) ( ( TickType_t )( ms ) )

#endif /* TASK_H */
