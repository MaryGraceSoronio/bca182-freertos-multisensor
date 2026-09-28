"""
native_host_main.py - BCA182 Laboratory Activity 1

PlatformIO pre-script for [env:native] only.

Section 44 wants a bare `pio test` to run the host unit tests, and
PlatformIO resolves the environments for a bare `pio test` from
`default_envs` - the same list a bare `pio run` uses - while
`test_filter`/`test_ignore` only select suites *within* an environment
(see platformio/test/helpers.py).  native therefore has to appear in
default_envs, which also means `pio run` visits it.  When that visit is a
test build the suites bring their own main() and their own link-time
stand-ins, so this script must add nothing; when it is an ordinary
`pio run` it links scripts/native_host_main.cpp instead, so the command
still reports SUCCESS without touching the firmware.

The guard is PlatformIO's own `__test` SCons target marker - the same one
piobuild.GetBuildType() uses to decide that it is building a test.
"""

Import("env")

from SCons.Script import COMMAND_LINE_TARGETS

if "__test" in COMMAND_LINE_TARGETS:
    # `pio test` - the suites under test/ are the program; nothing to add.
    pass
elif "clean" in COMMAND_LINE_TARGETS:
    # `pio run --target clean` must not rebuild anything first.
    pass
else:
    # `pio run` (or any other ordinary build target): link the stub.  The
    # stub deliberately includes no project header, so the only include path
    # it needs is the one build_flags already put there.
    env.BuildSources(
        "$BUILD_DIR/_host_main",
        "$PROJECT_DIR/scripts",
        "+<native_host_main.cpp>",
    )
