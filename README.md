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

    pio run                 # build
    pio run -t upload       # flash real hardware (not used in the lab)

The Wokwi simulation is started from the VS Code command palette
(`Wokwi: Start Simulator`) and uses the firmware built by `pio run`
(`wokwi.toml` points `elf` at `.pio/build/bluepill_f103c8/firmware.elf`).

## Layout

    include/   public headers (FreeRTOSConfig.h, rtos_objects.h, sensors.h, ...)
    src/       main.cpp, rtos_objects.cpp, sensors.cpp
    scripts/   freertos_build.py - compiles the FreeRTOS kernel that the
               PlatformIO STM32Cube builder does not include
    test/      native unit tests (pio test -e native)

Full documentation is added as the project is built; see the laboratory
specification in `../Laboratory 1/BCA182 - Laboratory Activity 1.md`.
