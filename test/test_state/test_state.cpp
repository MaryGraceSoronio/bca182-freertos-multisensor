/**
 * test_state.cpp - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Unit tests for the section 32 system state machine (sections 42-44).
 *
 * evaluateSystemState() is pure - the next state is a function of the
 * current state and the two events the diagram's edges carry - so these
 * tests need no Wokwi, no STM32 and no FreeRTOS.  They include the
 * header-only rule directly (include/system_state.h); the FreeRTOS half of
 * the module, StateTask in src/system_state.cpp, is deliberately not linked,
 * because everything it does beyond the rule is print-and-flip the bits the
 * producers/consumers table in rtos_objects.h documents.
 *
 * Each case corresponds to an edge or self-loop of the diagram:
 *
 *   - FR-09 / FT-09: ACTIVE + inactivity timeout -> INACTIVE
 *   - FR-10 / FT-10: INACTIVE + motion            -> ACTIVE
 *   - FR-10 / FT-08: ACTIVE + motion              -> stays ACTIVE
 *   - the self-loops the diagram leaves implicit: neither event means the
 *     state is unchanged, and a timeout cannot cancel a motion that is
 *     present in the same evaluation (motion always wins).
 *
 * The timeout constant gets its own test because sections 31/43 treat 15
 * seconds as part of the specification: if the task and the test ever
 * disagree about the window, FR-09 would silently break.
 *
 * Like test_alarm, [env:native] in platformio.ini compiles src/alarm.cpp
 * into this suite too, so the same three link-time stand-ins are provided.
 *
 * Run with:  pio test -e native
 */

#include <unity.h>

#include "system_state.h"
#include "rtos_objects.h"

extern "C" {

QueueHandle_t alarmQueue = nullptr;

EventGroupHandle_t systemEvents = nullptr;

void serial_write( const char *text )
{
    ( void )text;
}

}

void setUp( void )
{
}

void tearDown( void )
{
}

/** Compare one evaluation against the expected state, labelling any failure. */
static void assert_next( SystemState current, bool motion, bool timed_out,
                         SystemState expected, const char *label )
{
    TEST_ASSERT_EQUAL_INT_MESSAGE( ( int )expected,
                                   ( int )evaluateSystemState(current, motion,
                                                              timed_out),
                                   label );
}

/** Section 31 fixes the laboratory window at 15 seconds (FR-09). */
void test_timeout_is_the_section31_fifteen_seconds( void )
{
    TEST_ASSERT_EQUAL_UINT32( 15000U, SYSTEM_INACTIVE_TIMEOUT_MS );
}

/** The diagram's timeout edge (FR-09 / FT-09). */
void test_active_with_timeout_goes_inactive( void )
{
    assert_next(SystemState::ACTIVE, false, true, SystemState::INACTIVE,
                "ACTIVE must fall to INACTIVE on the inactivity timeout");
}

/** The diagram's motion edge (FR-10 / FT-10). */
void test_inactive_with_motion_goes_active( void )
{
    assert_next(SystemState::INACTIVE, true, false, SystemState::ACTIVE,
                "INACTIVE must return to ACTIVE on motion (FR-10)");
}

/** FT-08: motion on an already-active system is a self-loop, not a reset. */
void test_active_with_motion_stays_active( void )
{
    assert_next(SystemState::ACTIVE, true, false, SystemState::ACTIVE,
                "motion while ACTIVE must keep the system ACTIVE");
}

/** The INACTIVE self-loop: a further timeout with no motion changes nothing. */
void test_inactive_with_timeout_stays_inactive( void )
{
    assert_next(SystemState::INACTIVE, false, true, SystemState::INACTIVE,
                "a timeout while already INACTIVE must not change the state");
}

/** The "neither edge fired" self-loops, both states. */
void test_no_event_keeps_the_state( void )
{
    assert_next(SystemState::ACTIVE, false, false, SystemState::ACTIVE,
                "no event must leave an ACTIVE system ACTIVE");
    assert_next(SystemState::INACTIVE, false, false, SystemState::INACTIVE,
                "no event must leave an INACTIVE system INACTIVE");
}

/** FR-10 has no exceptions: motion outranks a timeout in the same turn, so
 *  a continuously re-triggered sensor can never time out (see motion.cpp). */
void test_motion_wins_over_timeout( void )
{
    assert_next(SystemState::ACTIVE, true, true, SystemState::ACTIVE,
                "motion must win over a timeout in the same evaluation");
    assert_next(SystemState::INACTIVE, true, true, SystemState::ACTIVE,
                "motion must win over a timeout from INACTIVE too");
}

/** Walk the diagram as a sequence - the composition the single-turn cases
 *  cannot show: ACTIVE --(timeout)--> INACTIVE --(motion)--> ACTIVE. */
void test_diagram_walk_inactivity_then_motion( void )
{
    SystemState state = SystemState::ACTIVE;

    state = evaluateSystemState(state, false, true);
    TEST_ASSERT_EQUAL_INT( ( int )SystemState::INACTIVE, ( int )state );

    state = evaluateSystemState(state, true, false);
    TEST_ASSERT_EQUAL_INT( ( int )SystemState::ACTIVE, ( int )state );
}

/** Section 44 runs this suite with `pio test -e native`.  The suite carries
 *  its own main(): PlatformIO did not generate a runner for it (the build
 *  fails with an undefined reference to WinMain without one), and listing
 *  the cases explicitly keeps the order in the output stable. */
int main( void )
{
    UNITY_BEGIN();

    RUN_TEST(test_timeout_is_the_section31_fifteen_seconds);
    RUN_TEST(test_active_with_timeout_goes_inactive);
    RUN_TEST(test_inactive_with_motion_goes_active);
    RUN_TEST(test_active_with_motion_stays_active);
    RUN_TEST(test_inactive_with_timeout_stays_inactive);
    RUN_TEST(test_no_event_keeps_the_state);
    RUN_TEST(test_motion_wins_over_timeout);
    RUN_TEST(test_diagram_walk_inactivity_then_motion);

    return UNITY_END();
}
