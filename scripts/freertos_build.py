"""
freertos_build.py - BCA182 Laboratory Activity 1

PlatformIO's stm32cube framework builder compiles HAL + CMSIS only: it never
touches Middlewares/Third_Party/FreeRTOS (there is not a single reference to
"freertos" anywhere in the ststm32 platform builder).  Section 6 of the
laboratory specification makes FreeRTOS mandatory, so this pre-script wires
the bundled kernel into the build.

Design notes
------------
* The kernel is compiled straight out of the framework package, so it is not
  vendored into the repository (keeps the commit history readable) and it is
  always the exact version that ships with framework-stm32cubef1.
* Only the files this application actually links are compiled: the core
  kernel, the Cortex-M3 GCC port and heap_4.  Timers, co-routines, stream
  buffers and every foreign port (IAR/Keil/RVDS/Tasking) are excluded, which
  keeps flash usage down on the 64 KB part.
"""

Import("env")

import os

platform = env.PioPlatform()
framework_dir = platform.get_package_dir("framework-stm32cubef1")

if not framework_dir:
    print("ERROR: framework-stm32cubef1 package not found - cannot build FreeRTOS.")
    env.Exit(1)

freertos_dir = os.path.join(
    framework_dir, "Middlewares", "Third_Party", "FreeRTOS", "Source"
)

if not os.path.isdir(freertos_dir):
    print("ERROR: FreeRTOS sources not found at %s" % freertos_dir)
    env.Exit(1)

# FreeRTOS.h must be able to find FreeRTOSConfig.h (in include/) and the
# Cortex-M3 port must be able to find portmacro.h.
env.Append(
    CPPPATH=[
        os.path.join(freertos_dir, "include"),
        os.path.join(freertos_dir, "portable", "GCC", "ARM_CM3"),
    ]
)

env.BuildSources(
    os.path.join("$BUILD_DIR", "FreeRTOS"),
    freertos_dir,
    src_filter=[
        "-<*>",
        "+<tasks.c>",
        "+<queue.c>",
        "+<list.c>",
        "+<event_groups.c>",
        "+<portable/GCC/ARM_CM3/port.c>",
        "+<portable/MemMang/heap_4.c>",
    ],
)
