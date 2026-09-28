/**
 * native_host_main.cpp - BCA182 Laboratory Activity 1
 *
 * Link stub for the one thing the host environment is not: an application.
 *
 * Why this file exists
 * --------------------
 * Section 44 asks the student to run a bare `pio test`.  PlatformIO picks the
 * environments a bare `pio test` processes from `default_envs` - the same
 * list `pio run` uses (platformio/test/helpers.py, list_test_suites(): a
 * suite is skipped when `default_envs` is set and its environment is not in
 * it), and `test_filter` / `test_ignore` only narrow the test *suites* inside
 * an environment, never the environment itself.  So for `native` to be
 * reached by a bare `pio test` it has to be in `default_envs`, which also
 * makes a bare `pio run` visit it - and a bare `pio run` must keep building
 * the firmware (section 41).  This translation unit is what `pio run -e
 * native` links when that happens.
 *
 * What it is
 * ----------
 * Nothing but the four link-time stand-ins the test suites already define
 * for themselves (alarmQueue, modeQueue, systemEvents, serial_write) plus an
 * empty main().  It is never flashed, never executed and never part of the
 * firmware: it lives in scripts/, not src/, is added only by
 * scripts/native_host_main.py and only outside a test build, and the
 * firmware environment never sees that script at all.  Its practical side
 * effect is a host-side compile-and-link of src/alarm.cpp and src/input.cpp
 * on every `pio run`, which is a free check that those two translation units
 * still build.
 *
 * The stand-ins are declared here rather than by including rtos_objects.h
 * because a pre: script's objects are built before piobuild.ProcessProjectDeps()
 * has put the project include directory on the include path; the types are
 * spelled out exactly as test/stubs/ spells them (void * for every handle),
 * so the linker is still the thing that catches a mismatch.
 */

typedef void *QueueHandle_t;
typedef void *EventGroupHandle_t;

extern "C" {

QueueHandle_t alarmQueue;

QueueHandle_t modeQueue;

EventGroupHandle_t systemEvents;

void serial_write( const char *text )
{
    ( void )text;
}

}

/** Empty entry point for `pio run -e native`; test suites bring their own. */
int main( void )
{
    return 0;
}
