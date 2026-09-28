/**
 * test_alarm.cpp - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Unit tests for the section 30 alarm decision (sections 42-44).
 *
 * evaluateTemperature() is pure, so these tests need no Wokwi, no STM32 and
 * no FreeRTOS - they link the very same src/alarm.cpp the firmware builds
 * (see [env:native] in platformio.ini).  What they pin down is the boundary
 * rule: section 4 fixes the limits at 18 and 30 degrees Celsius, and the
 * documented reading of FR-07 is that a value sitting exactly on a limit is
 * already outside the normal range.  Every threshold therefore gets its own
 * case - below, exactly on, just above - because the choice between < and <=
 * is invisible in ordinary readings and is exactly the kind of off-by-one a
 * test exists to catch.
 *
 * Run with:  pio test -e native
 */

#include <unity.h>

#include "alarm.h"
#include "rtos_objects.h"

/**
 * Link-time stand-ins for the objects AlarmTask() consumes.
 *
 * src/alarm.cpp is compiled whole so that the decision logic under test is
 * the exact code the firmware runs, which also drags in the task half of the
 * file.  AlarmTask() itself is never called here - it would block on a
 * queue no producer feeds - so these definitions exist only to satisfy the
 * linker, and serial_write() deliberately discards its output.  systemEvents
 * (section 35) is the group AlarmTask() writes its EVENT_ALARM bit into; it
 * stays null and the stubbed xEventGroupSet/ClearBits discard the write.
 */
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

/** Compare one reading against the expected state, labelling any failure. */
static void assert_temperature( float temperature, AlarmState expected,
                                const char *label )
{
    TEST_ASSERT_EQUAL_INT_MESSAGE( ( int )expected,
                                   ( int )evaluateTemperature(temperature),
                                   label );
}

/** Section 4: the limits live in the public header so that the test and the
 *  runtime cannot drift apart. */
void test_limits_are_the_section4_values( void )
{
    TEST_ASSERT_EQUAL_FLOAT( 18.0f, TEMP_LOW_LIMIT_C );
    TEST_ASSERT_EQUAL_FLOAT( 30.0f, TEMP_HIGH_LIMIT_C );
}

void test_below_low_limit_is_low( void )
{
    assert_temperature(17.9f, AlarmState::LOW_TEMPERATURE,
                       "17.9 C is below the 18 C low limit");
}

void test_exactly_low_limit_is_low( void )
{
    assert_temperature(18.0f, AlarmState::LOW_TEMPERATURE,
                       "the low limit itself must alarm (FR-07)");
}

void test_just_above_low_limit_is_normal( void )
{
    assert_temperature(18.1f, AlarmState::NORMAL,
                       "18.1 C is inside the normal range");
}

void test_normal_value_is_normal( void )
{
    assert_temperature(25.0f, AlarmState::NORMAL,
                       "25.0 C is comfortably inside the normal range");
}

void test_just_below_high_limit_is_normal( void )
{
    assert_temperature(29.9f, AlarmState::NORMAL,
                       "29.9 C is still inside the normal range");
}

void test_exactly_high_limit_is_high( void )
{
    assert_temperature(30.0f, AlarmState::HIGH_TEMPERATURE,
                       "the high limit itself must alarm (FR-07)");
}

void test_above_high_limit_is_high( void )
{
    assert_temperature(30.1f, AlarmState::HIGH_TEMPERATURE,
                       "30.1 C is above the 30 C high limit");
}

void test_far_below_low_limit_is_low( void )
{
    assert_temperature(-40.0f, AlarmState::LOW_TEMPERATURE,
                       "-40 C is far outside the normal range");
}

void test_far_above_high_limit_is_high( void )
{
    assert_temperature(85.0f, AlarmState::HIGH_TEMPERATURE,
                       "85 C is far outside the normal range");
}

/** Section 44 runs this suite with `pio test -e native`.  The suite carries
 *  its own main(): PlatformIO did not generate a runner for it (the build
 *  fails with an undefined reference to WinMain without one), and listing
 *  the cases explicitly keeps the order in the output stable. */
int main( void )
{
    UNITY_BEGIN();

    RUN_TEST(test_limits_are_the_section4_values);
    RUN_TEST(test_below_low_limit_is_low);
    RUN_TEST(test_exactly_low_limit_is_low);
    RUN_TEST(test_just_above_low_limit_is_normal);
    RUN_TEST(test_normal_value_is_normal);
    RUN_TEST(test_just_below_high_limit_is_normal);
    RUN_TEST(test_exactly_high_limit_is_high);
    RUN_TEST(test_above_high_limit_is_high);
    RUN_TEST(test_far_below_low_limit_is_low);
    RUN_TEST(test_far_above_high_limit_is_high);

    return UNITY_END();
}
