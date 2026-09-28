/**
 * system_state.cpp - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * StateTask - the task half of the section 32 state machine.  The transition
 * rule itself is evaluateSystemState() in include/system_state.h, kept pure
 * and header-only so sections 42-43 can unit test it on the host.
 *
 * How the wait maps onto the diagram
 * -----------------------------------
 * One loop turn = one evaluation of the machine, and every turn starts with
 * a single xEventGroupWaitBits() on EVENT_MOTION:
 *
 *   - MotionTask sets EVENT_MOTION while PIR OUT is high, so the wait ends
 *     early whenever there is motion evidence;
 *   - otherwise the wait ends at the section 31 inactivity timeout (15 s),
 *     which is exactly the "inactivity timeout" edge of the diagram.
 *
 * The wait clears EVENT_MOTION on exit (clear-on-exit), so every evaluation
 * consumes the evidence that woke it: no motion that was already published
 * can be counted twice.  While PIR OUT stays high MotionTask re-sets the bit
 * on its next 10 ms poll, which is what keeps a continuously triggered sensor
 * from ever timing out - the re-triggered PIR never produces a fresh edge,
 * so edge-only signalling would freeze the machine after the first detection.
 *
 * Transition side effects are printed and applied only when the state really
 * changes, so a steady room prints exactly one "State: ACTIVE" at boot (the
 * first line after the banner doubles as proof the machine came up in its
 * documented boot state) and then nothing until the diagram's edges fire.
 * EVENT_ACTIVE is the published result: it mirrors the current state and is
 * what DisplayTask (blank/unblank the OLED, section 34) and InputTask
 * (encoder active only while ACTIVE, section 33) gate on.
 *
 * Priority 3 (sections 38-39): tied with MotionTask and InputTask, because
 * the machine consumes the motion evidence those two produce and nothing
 * below it should delay a state change - a late INACTIVE would keep the
 * OLED lit after 15 s of an empty room, and a late ACTIVE would leave the
 * display blank while someone is standing in front of it.  The cost at that
 * priority is one blocked event-group wait and a handful of comparisons per
 * wakeup - it runs at most once per MotionTask poll (10 ms) while the sensor
 * is high and once per 15 s while the room is quiet - so it cannot starve
 * SensorTask, AlarmTask or DisplayTask.
 *
 * Stack: 192 words.  There is no snprintf() on any path (the state lines are
 * literals), so the 256-word figure the formatting tasks need is overkill,
 * while MotionTask's 128 words leave less headroom than this task's event
 * group call, blocking UART transmit and nested wait path want; 192 sits
 * between the two with margin and the stack-overflow hook checks it.
 */

#include "system_state.h"

#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

#include "rtos_objects.h"   /* systemEvents, EVENT_ACTIVE/EVENT_MOTION */

/**
 * Run the section 32 machine until the scheduler stops it.
 *
 * Section 19 holds by construction: the loop's only runnable body is the
 * evaluation and - on a transition - two literal serial lines; everything
 * else is one blocking xEventGroupWaitBits().
 */
void StateTask( void *argument )
{
    SystemState state = SystemState::ACTIVE;

    ( void )argument;

    /* Boot state: the section 32 diagram does not specify one, so the
     * system starts ACTIVE (section 33 behaviour from the first sample) and
     * publishes it before any consumer can read a stale all-clear group. */
    serial_write("State: ACTIVE\r\n");
    ( void )xEventGroupSetBits(systemEvents, EVENT_ACTIVE);

    for( ;; )
    {
        EventBits_t bits = xEventGroupWaitBits(systemEvents,
                                               EVENT_MOTION,
                                               pdTRUE,  /* clear on exit   */
                                               pdFALSE, /* any bit is fine */
                                               pdMS_TO_TICKS(SYSTEM_INACTIVE_TIMEOUT_MS));
        bool motion  = ( ( bits & EVENT_MOTION ) != 0U );
        bool timeout = ( !motion );
        SystemState next = evaluateSystemState(state, motion, timeout);

        if( next != state )
        {
            state = next;

            if( state == SystemState::ACTIVE )
            {
                ( void )xEventGroupSetBits(systemEvents, EVENT_ACTIVE);
                serial_write("State: ACTIVE\r\n");
            }
            else
            {
                ( void )xEventGroupClearBits(systemEvents, EVENT_ACTIVE);
                serial_write("State: INACTIVE\r\n");
            }
        }
    }
}
