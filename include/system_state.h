/**
 * system_state.h - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * System state machine (sections 32-34: ACTIVE and INACTIVE, FR-08/FR-09/
 * FR-10) - the pure decision plus the FreeRTOS task that runs it.
 *
 * The section 32 diagram has two states and two transitions:
 *
 *     ACTIVE    --(inactivity timeout)-->  INACTIVE
 *     INACTIVE  --(motion detected)---->   ACTIVE
 *
 * The self-loops the diagram leaves implicit are part of the machine too:
 * motion while ACTIVE keeps it ACTIVE, and a timeout while INACTIVE keeps it
 * INACTIVE (nothing happens until motion arrives).
 *
 * Boot state: the diagram does not show one, and the system must display and
 * respond from the first second, so start-up is ACTIVE.  Section 33 is what
 * the system does immediately; section 34 is only ever entered through the
 * drawn timeout transition.
 *
 * evaluateSystemState() lives in this header, header-only, for the same
 * reason alarm.h carries the section 30 decision: sections 42-43 test the
 * pure transition rule on the host (test/test_state) with no Wokwi, no STM32
 * and no FreeRTOS.  Only the task half - StateTask - needs the kernel, so
 * only that half lives in src/system_state.cpp (section 40).
 *
 * Section 31 fixes the laboratory inactivity timeout at 15 seconds; the
 * constant is here so the test and the task can only disagree if one of them
 * stops including this header.
 */

#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdbool.h>
#include <stdint.h>

/** Section 31: "use a short inactivity timeout such as 15 seconds" (FR-09). */
#define SYSTEM_INACTIVE_TIMEOUT_MS 15000U

/**
 * C++ only, as in alarm.h: enum class is not valid C, and a C translation
 * unit could not parse it.  Only .cpp files include system_state.h.
 */
#ifdef __cplusplus

enum class SystemState
{
    ACTIVE,
    INACTIVE
};

/**
 * The section 32 transition rule, pure by construction (section 42).
 *
 * The result depends on the arguments alone - no peripheral, no queue, no
 * clock - so the unit test and the firmware cannot drift apart.
 *
 * Motion wins whenever it is present (FR-10: any PIR activity returns the
 * system to ACTIVE, and it also keeps an ACTIVE system active even if the
 * caller reports a timeout in the same evaluation).  Otherwise an ACTIVE
 * system that has seen no motion for a full timeout window goes INACTIVE
 * (FR-09), and every other combination - including "neither event, nothing
 * to decide" - leaves the state untouched.
 *
 * @param current    the state the machine is in now
 * @param motion     the wait ended with EVENT_MOTION present
 * @param timed_out  the wait ended without motion (inactivity timeout)
 * @return the next state
 */
inline SystemState evaluateSystemState(SystemState current, bool motion,
                                        bool timed_out)
{
    if( motion )
    {
        return SystemState::ACTIVE;
    }

    if( timed_out && ( current == SystemState::ACTIVE ) )
    {
        return SystemState::INACTIVE;
    }

    return current;
}

#endif /* __cplusplus */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * FreeRTOS task: runs the section 32 state machine (sections 7 and 32).
 *
 * Created in main.cpp.  It blocks on the event group for up to
 * SYSTEM_INACTIVE_TIMEOUT_MS waiting for EVENT_MOTION, evaluates the pure
 * rule above, and applies a transition by printing the new state and
 * flipping EVENT_ACTIVE - the bit DisplayTask and InputTask gate on
 * (sections 33-34).
 */
void StateTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_STATE_H */
