/**
 * event_groups.h - host stub for the unit tests (sections 42-44)
 *
 * Declares the event-group API the alarm sources use (alarm.cpp is the one
 * src file the native environment compiles).  Like the queue stub there is
 * no scheduler behind these calls, and AlarmTask()/StateTask() are never
 * called by the tests - the stubs exist only so the translation units link.
 * The bit macros themselves are NOT stubbed: rtos_objects.h defines them
 * for every build, firmware and host alike, so the test exercises the same
 * names the firmware uses.
 */

#ifndef EVENT_GROUPS_H
#define EVENT_GROUPS_H

#include "FreeRTOS.h"

typedef void *       EventGroupHandle_t;
typedef uint32_t     EventBits_t;

static inline EventGroupHandle_t xEventGroupCreate( void )
{
    return ( EventGroupHandle_t )0;
}

static inline EventBits_t xEventGroupSetBits( EventGroupHandle_t group,
                                              EventBits_t bits )
{
    ( void )group;
    ( void )bits;
    return 0U;
}

static inline EventBits_t xEventGroupClearBits( EventGroupHandle_t group,
                                                EventBits_t bits )
{
    ( void )group;
    ( void )bits;
    return 0U;
}

static inline EventBits_t xEventGroupGetBits( EventGroupHandle_t group )
{
    ( void )group;
    return 0U;
}

#endif /* EVENT_GROUPS_H */
