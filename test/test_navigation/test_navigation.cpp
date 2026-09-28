/**
 * test_navigation.cpp - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Unit tests for the section 29 page navigation (sections 42-44).
 *
 * nextDisplayMode()/previousDisplayMode() are pure - the next page is a
 * function of the current page alone - so these tests need no Wokwi, no STM32
 * and no FreeRTOS.  They link the very same src/input.cpp the firmware builds
 * (see [env:native] in platformio.ini), which is what makes them worth
 * having: the wraparound under test is the switch statement InputTask() walks
 * on every detent, not a re-typed copy of it.
 *
 * Section 29 fixes the order and both wraparounds:
 *
 *   clockwise         Temperature -> Humidity -> Light -> Motion -> Temperature
 *   counter-clockwise Temperature -> Motion -> Light -> Humidity -> Temperature
 *
 * so each of the eight single steps gets its own case with an exact enum
 * assertion - four forward (section 43's "forward transitions ... and
 * wraparound") and four reverse - and three further cases walk the whole ring
 * in both directions and check that a step one way followed by a step back is
 * the identity.  That is the part a single-step test cannot show: an
 * off-by-one that maps two different pages onto the same one survives any one
 * assertion and is caught immediately by the round trip.
 *
 * The link-time stand-ins below are needed because [env:native] compiles
 * src/alarm.cpp and src/input.cpp into every suite; none of the code that
 * touches them is reached here.
 *
 * Run with:  pio test -e native
 */

#include <unity.h>

#include "input.h"
#include "rtos_objects.h"

extern "C" {

QueueHandle_t alarmQueue = nullptr;

QueueHandle_t modeQueue = nullptr;

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

/** One clockwise step from `from`, labelling any failure. */
static void assert_cw( DisplayMode from, DisplayMode expected,
                       const char *label )
{
    TEST_ASSERT_EQUAL_INT_MESSAGE( ( int )expected,
                                   ( int )nextDisplayMode(from), label );
}

/** One counter-clockwise step from `from`, labelling any failure. */
static void assert_ccw( DisplayMode from, DisplayMode expected,
                        const char *label )
{
    TEST_ASSERT_EQUAL_INT_MESSAGE( ( int )expected,
                                   ( int )previousDisplayMode(from), label );
}

/*-----------------------------------------------------------
 * Forward transitions - section 29, clockwise
 *----------------------------------------------------------*/

void test_cw_temperature_advances_to_humidity( void )
{
    assert_cw(DisplayMode::TEMPERATURE, DisplayMode::HUMIDITY,
              "clockwise from Temperature must select Humidity");
}

void test_cw_humidity_advances_to_light( void )
{
    assert_cw(DisplayMode::HUMIDITY, DisplayMode::LIGHT,
              "clockwise from Humidity must select Light");
}

void test_cw_light_advances_to_motion( void )
{
    assert_cw(DisplayMode::LIGHT, DisplayMode::MOTION,
              "clockwise from Light must select Motion");
}

/** The forward wraparound: the ring has no last page. */
void test_cw_motion_wraps_to_temperature( void )
{
    assert_cw(DisplayMode::MOTION, DisplayMode::TEMPERATURE,
              "clockwise from Motion must wrap to Temperature (section 29)");
}

/*-----------------------------------------------------------
 * Reverse transitions - section 29, counter-clockwise
 *----------------------------------------------------------*/

void test_ccw_temperature_wraps_to_motion( void )
{
    assert_ccw(DisplayMode::TEMPERATURE, DisplayMode::MOTION,
               "counter-clockwise from Temperature must wrap to Motion");
}

void test_ccw_humidity_retreats_to_temperature( void )
{
    assert_ccw(DisplayMode::HUMIDITY, DisplayMode::TEMPERATURE,
               "counter-clockwise from Humidity must return to Temperature");
}

void test_ccw_light_retreats_to_humidity( void )
{
    assert_ccw(DisplayMode::LIGHT, DisplayMode::HUMIDITY,
               "counter-clockwise from Light must return to Humidity");
}

void test_ccw_motion_retreats_to_light( void )
{
    assert_ccw(DisplayMode::MOTION, DisplayMode::LIGHT,
               "counter-clockwise from Motion must return to Light");
}

/*-----------------------------------------------------------
 * Whole-ring walks - the composition the single steps cannot show
 *----------------------------------------------------------*/

/** Four clockwise detents must land back on Temperature, passing every other
 *  page exactly once on the way - a duplicate or a skipped page shows up as a
 *  failed intermediate assertion. */
void test_clockwise_walk_covers_the_whole_ring( void )
{
    DisplayMode mode = DisplayMode::TEMPERATURE;

    mode = nextDisplayMode(mode);
    TEST_ASSERT_EQUAL_INT( ( int )DisplayMode::HUMIDITY, ( int )mode );

    mode = nextDisplayMode(mode);
    TEST_ASSERT_EQUAL_INT( ( int )DisplayMode::LIGHT, ( int )mode );

    mode = nextDisplayMode(mode);
    TEST_ASSERT_EQUAL_INT( ( int )DisplayMode::MOTION, ( int )mode );

    mode = nextDisplayMode(mode);
    TEST_ASSERT_EQUAL_INT( ( int )DisplayMode::TEMPERATURE, ( int )mode );
}

/** The mirror image: four counter-clockwise detents must also land back on
 *  Temperature, walking the ring in the section 29 reverse order. */
void test_counter_clockwise_walk_covers_the_whole_ring( void )
{
    DisplayMode mode = DisplayMode::TEMPERATURE;

    mode = previousDisplayMode(mode);
    TEST_ASSERT_EQUAL_INT( ( int )DisplayMode::MOTION, ( int )mode );

    mode = previousDisplayMode(mode);
    TEST_ASSERT_EQUAL_INT( ( int )DisplayMode::LIGHT, ( int )mode );

    mode = previousDisplayMode(mode);
    TEST_ASSERT_EQUAL_INT( ( int )DisplayMode::HUMIDITY, ( int )mode );

    mode = previousDisplayMode(mode);
    TEST_ASSERT_EQUAL_INT( ( int )DisplayMode::TEMPERATURE, ( int )mode );
}

/** One detent forward and the same detent back must be a no-op from every
 *  starting page - the property that makes FT-04/FT-05 (turn the knob one way,
 *  then the other) leave the display where it started. */
void test_previous_undoes_next_from_every_page( void )
{
    DisplayMode modes[] = { DisplayMode::TEMPERATURE, DisplayMode::HUMIDITY,
                            DisplayMode::LIGHT, DisplayMode::MOTION };

    for( unsigned i = 0; i < 4U; i++ )
    {
        DisplayMode back = previousDisplayMode(nextDisplayMode(modes[i]));

        TEST_ASSERT_EQUAL_INT_MESSAGE( ( int )modes[i], ( int )back,
                                       "a clockwise step followed by a "
                                       "counter-clockwise step must return to "
                                       "the starting page" );
    }
}

/** Section 44 runs this suite with `pio test -e native`.  The suite carries
 *  its own main(): PlatformIO did not generate a runner for it (the build
 *  fails with an undefined reference to WinMain without one), and listing the
 *  cases explicitly keeps the order in the output stable. */
int main( void )
{
    UNITY_BEGIN();

    RUN_TEST(test_cw_temperature_advances_to_humidity);
    RUN_TEST(test_cw_humidity_advances_to_light);
    RUN_TEST(test_cw_light_advances_to_motion);
    RUN_TEST(test_cw_motion_wraps_to_temperature);
    RUN_TEST(test_ccw_temperature_wraps_to_motion);
    RUN_TEST(test_ccw_humidity_retreats_to_temperature);
    RUN_TEST(test_ccw_light_retreats_to_humidity);
    RUN_TEST(test_ccw_motion_retreats_to_light);
    RUN_TEST(test_clockwise_walk_covers_the_whole_ring);
    RUN_TEST(test_counter_clockwise_walk_covers_the_whole_ring);
    RUN_TEST(test_previous_undoes_next_from_every_page);

    return UNITY_END();
}
