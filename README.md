# BCA182 FreeRTOS Multisensor

Real-Time Multisensor Room Monitoring System - BCA182 Laboratory Activity 1.

* **Target:** STM32 Blue Pill (STM32F103C8T6) simulated in Wokwi
* **Framework:** STM32Cube (HAL + CMSIS) with native FreeRTOS APIs - no Arduino
* **Toolchain:** PlatformIO + Git

## Pin Configuration

| Pin    | Signal                          | Peripheral        |
| ------ | ------------------------------- | ----------------- |
| PA9    | USART1 TX                       | serial monitor    |
| PA10   | USART1 RX                       | serial monitor    |
| PA0    | LDR / photoresistor AO          | ADC1_IN0 (M5)     |
| PA1    | DHT22 SDA (10 k pull-up, 3.3 V) | GPIO open drain   |
| PB6    | I2C1 SCL -> OLED SCL            | I2C (AF open drain, pull-up) |
| PB7    | I2C1 SDA -> OLED SDA            | I2C (AF open drain, pull-up) |
| PC13   | onboard LED                     | GPIO (M2)         |

## Building

    pio run                 # build every environment in default_envs
    pio run -t upload       # flash real hardware (not used in the lab)
    pio test                # run the unit tests (host suites, sections 42-44)
    pio check               # static code analysis (section 45, cppcheck)

A bare `pio test` walks `default_envs`, so it builds the firmware environment
first and then runs the host suites under `test/` in `native`.  The firmware
environment is marked `test_ignore = *` because the `stm32cube` framework has
no Unity configuration for the embedded runner; see `platformio.ini`.

`pio check` analyses the project once per environment (21 findings under
`bluepill_f103c8`, 14 under `native`).  Every finding, its cause and its
resolution are in [`docs/static-analysis.md`](docs/static-analysis.md).

The Wokwi simulation is started from the VS Code command palette
(`Wokwi: Start Simulator`) and uses the firmware built by `pio run`
(`wokwi.toml` points `elf` at `.pio/build/bluepill_f103c8/firmware.elf`).

## Layout

    include/   public headers (FreeRTOSConfig.h, rtos_objects.h, sensors.h, ...)
    src/       main.cpp, rtos_objects.cpp, sensors.cpp, display.cpp, ...
    scripts/   freertos_build.py     - compiles the FreeRTOS kernel that the
                                       PlatformIO STM32Cube builder does not include
               native_host_main.py   - stub main() so the host environment
                                       survives a bare `pio run`
    test/      host unit tests (test_alarm, test_navigation, test_state) and
               the `stubs/` headers the host build compiles against
    docs/      static-analysis.md - the section 46 findings table

Full documentation is added as the project is built; see the laboratory
specification in `../Laboratory 1/BCA182 - Laboratory Activity 1.md`.
