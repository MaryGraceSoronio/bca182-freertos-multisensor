# Static code analysis — sections 45–46

**Tool:** cppcheck 2.11, driven by PlatformIO's `pio check`
**Configuration:** the `[env]` section of `platformio.ini` (`check_tool`, `check_flags`)

```console
$ pio check
```

---

## 1. Tool choice and flags

`check_tool = cppcheck` is stated explicitly rather than left to the
PlatformIO default for two reasons: cppcheck is the only analyzer PlatformIO
has to download for this project (`tool-cppcheck`), and clang-tidy was
rejected because it needs a compilation database for the STM32Cube framework,
which this project does not generate — its findings would describe the build
setup instead of the application.

`check_flags` repeats PlatformIO's defaults verbatim. That is not
redundancy: PlatformIO appends `--inline-suppr` *only* when no flags are given
at all, so listing any flag switches it off and it has to be written down.

| Flag | Why it is there |
| --- | --- |
| `--inline-suppr` | lets the source silence a documented false positive with `// cppcheck-suppress <id>` instead of restructuring correct code |
| `--enable=warning,style,performance,portability,unusedFunction` | the defect classes section 46 asks a student to read and interpret, and the set this table was produced with |

Both live in the common `[env]` section, so the firmware and the host
environment are analysed with identical settings.

---

## 2. Two passes, and how to read the table

`pio check` is environment-scoped: PlatformIO resolves the environments from
`default_envs`, exactly as `pio run` does, so one command analyses the project
twice.

| Pass | Environment | Findings |
| --- | --- | --- |
| 1 | `bluepill_f103c8` | 21 |
| 2 | `native` | 14 |

Both passes examine the **same file set** — `check_src_filters` defaults to
`+<src> +<include>` and is independent of `build_src_filter`, so every file
under `src/` and `include/` is checked in both. What differs is the include
path and the `-D` defines:

* `bluepill_f103c8` sees the real STM32 HAL / CMSIS / FreeRTOS headers.
* `native` sees `test/stubs/` instead (`build_flags = -I test/stubs`), and
  those stubs declare only the handful of names the unit tests link against.

That single difference explains every line in the report: a finding that needs
a vendor identifier (`I2C1`, `DWT`, `configASSERT`, a real HAL prototype) can
only be produced by the firmware pass, and a finding that needs a *constant*
stub (`xEventGroupGetBits()` returning `0`, `HAL_GPIO_ReadPin()` returning
`GPIO_PIN_RESET`) can only be produced by the host pass.

**The bluepill pass is authoritative** for anything about vendor code — it is
the environment the firmware is built in.

---

## 3. Summary

| | high | medium | low | total |
| --- | --- | --- | --- | --- |
| Before the resolutions in section 4 | 0 | 0 | 38 | 38 |
| bluepill pass before → after | | | | 23 → 21 |
| native pass before → after | | | | 15 → 14 |
| **After (this run)** | **0** | **0** | **35** | **35** |

No high- or medium-severity finding was ever produced, and nothing was
suppressed: 3 were resolved by editing production code (section 4), the
remaining 32 are each justified row by row in section 5.

---

## 4. Findings resolved by code changes (3)

| # | File:Line (as reported before the fix) | Check id | Cause | Resolution |
| --- | --- | --- | --- | --- |
| 1 | `src/display.cpp:223` | `shadowFunction` | `oled_char()` declared `uint8_t index`, shadowing the POSIX function `index()` from `<strings.h>` — which the real Cube headers pull in, which is why this appeared in the bluepill pass only | the local was renamed `glyph_index` (and the one use reflowed onto a second line); the expression is otherwise unchanged, so generated code is identical |
| 2 | `src/input.cpp:171` (bluepill) | `constParameterPointer` | `publish_page( DisplayMode *mode )` only reads the page and hands it to the queue | the parameter became `const DisplayMode *`, and the doc comment above it now states that contract |
| 3 | `src/input.cpp:171` (native) | `constParameterPointer` | the same defect, reported a second time because `input.cpp` is in both passes' file set | fixed by the same edit |

Both edits are behaviour-neutral — no value on any execution path changes —
which is why the regression check still shows the M12 flash and RAM figures.

---

## 5. Full findings table (section 46)

All 35 lines of the report, each with its cause and its resolution.
"Accepted" never means "ignored": every row records why the line must stay as
it is. Resolution classes:

* **Fixed** — production code was changed (section 4)
* **Accepted — vendor/macro** — the C-style cast lives inside a CMSIS or
  FreeRTOS macro that must also compile as C
* **Accepted — signature** — the parameter type is fixed by a vendor or
  kernel prototype the file must match to link
* **Accepted — stub artifact** — constant-folded against `test/stubs/`,
  host pass only, not present in the firmware pass
* **Accepted — declaration visible** — the symbol's real prototype is in the
  include set, so cppcheck does not treat the definition as file-local

### 5.1 Fixed — 3 findings

| File:Line | Env | Check | Cause | Resolution |
| --- | --- | --- | --- | --- |
| `src/display.cpp:223` | bluepill | `shadowFunction` | local `index` hides POSIX `index()` | **Fixed** — renamed `glyph_index` |
| `src/input.cpp:171` | bluepill | `constParameterPointer` | `publish_page` reads but never writes `mode` | **Fixed** — `const DisplayMode *` |
| `src/input.cpp:171` | native | `constParameterPointer` | same, host pass | **Fixed** — same edit |

### 5.2 Accepted — vendor / macro-mandated casts — 19 findings

Every row is the same root cause: the C-style cast is written **inside a
vendor macro**, not in project code. CMSIS register macros and FreeRTOS's
`configASSERT` are written in C because their own translation units are C; a
C++ `static_cast` cannot be substituted into a macro the C kernel also
expands.

| File:Line | Env | Check | Cause | Resolution |
| --- | --- | --- | --- | --- |
| `src/display.cpp:86` | bluepill | `cstyleCast` | `hi2c1.Instance = I2C1;` — `I2C1` is a CMSIS macro whose expansion *is* a cast of a peripheral address to `I2C_TypeDef *` | **Accepted** — rewriting it means hand-expanding a CMSIS register address |
| `src/display.cpp:147` | bluepill | `cstyleCast` | `i2c_handle->Instance != I2C1` — same macro | **Accepted** — as above |
| `src/rtos_objects.cpp:81` | bluepill | `cstyleCast` | `configASSERT( … )` expands, at `include/FreeRTOSConfig.h:146–147`, to `vAssertCalled( (const char *)__FILE__, (uint32_t)__LINE__ )` | **Accepted** — `configASSERT` must compile as C for the kernel |
| `src/rtos_objects.cpp:82` | bluepill | `cstyleCast` | same macro, `alarmQueue` | **Accepted** — as above |
| `src/rtos_objects.cpp:83` | bluepill | `cstyleCast` | same macro, `modeQueue` | **Accepted** — as above |
| `src/rtos_objects.cpp:84` | bluepill | `cstyleCast` | same macro, `systemEvents` | **Accepted** — as above |
| `src/rtos_objects.cpp:85` | bluepill | `cstyleCast` | same macro, `serialMutex` | **Accepted** — as above |
| `src/rtos_objects.cpp:154` | bluepill | `cstyleCast` | `HAL_UART_Transmit( &huart1, (uint8_t *)text, (uint16_t)strlen(text), 1000U )` — the HAL prototype takes `uint8_t *`, `text` is a `const char *` | **Accepted** — signature-mandated; the length argument bounds the read |
| `src/rtos_objects.cpp:158` | bluepill | `cstyleCast` | `( void )xSemaphoreGive( serialMutex );` — deliberate discard of a return the task cannot act on | **Accepted** — the cast-to-void idiom is what marks "value intentionally ignored"; a named variable would imply it is used |
| `src/sensors.cpp:232` | bluepill | `cstyleCast` | `CoreDebug->DEMCR \|= CoreDebug_DEMCR_TRCENA_Msk;` | **Accepted** — CMSIS macro |
| `src/sensors.cpp:233` | bluepill | `cstyleCast` | `DWT->CYCCNT = 0U;` | **Accepted** — CMSIS peripheral pointer |
| `src/sensors.cpp:234` | bluepill | `cstyleCast` | `DWT->CTRL \|= DWT_CTRL_CYCCNTENA_Msk;` | **Accepted** — CMSIS bit mask |
| `src/sensors.cpp:239` | bluepill | `cstyleCast` | `uint32_t start = DWT->CYCCNT;` (delay_begin) | **Accepted** — CMSIS peripheral pointer |
| `src/sensors.cpp:242` | bluepill | `cstyleCast` | `while( ( DWT->CYCCNT - start ) < cycles )` | **Accepted** — CMSIS peripheral pointer |
| `src/sensors.cpp:251` | bluepill | `cstyleCast` | `uint32_t start = DWT->CYCCNT;` (delay_repeat) | **Accepted** — CMSIS peripheral pointer |
| `src/sensors.cpp:256` | bluepill | `cstyleCast` | `if( ( DWT->CYCCNT - start ) >= cycles )` | **Accepted** — CMSIS peripheral pointer |
| `src/sensors.cpp:305` | bluepill | `cstyleCast` | `uint32_t high_start = DWT->CYCCNT;` (DHT22 bit timing) | **Accepted** — CMSIS peripheral pointer |
| `src/sensors.cpp:313` | bluepill | `cstyleCast` | `uint32_t high_cycles = DWT->CYCCNT - high_start;` | **Accepted** — CMSIS peripheral pointer |
| `src/rtos_objects.cpp:154` | native | `cstyleCast` | the same `HAL_UART_Transmit` cast; `text`'s type comes from the function definition itself, so it survives the header swap | **Accepted** — as above |

Note the pattern: the *host* pass reports none of the `I2C1` / `DWT` /
`configASSERT` casts because `test/stubs/stm32f1xx_hal.h` and
`test/stubs/FreeRTOS.h` do not declare `I2C1`, `DWT`, `CoreDebug`, or
`configASSERT` — with the macro gone, cppcheck has no cast to look at. That
is exactly why the firmware pass, which has the real headers, is the one that
counts for these rows.

### 5.3 Accepted — signature-mandated parameters — 6 findings

| File:Line | Env | Check | Cause | Resolution |
| --- | --- | --- | --- | --- |
| `src/display.cpp:143` | bluepill | `constParameterPointer` | `HAL_I2C_MspInit( I2C_HandleTypeDef *i2c_handle )` never writes through the handle | **Accepted** — the type is fixed by ST's `stm32f1xx_hal_i2c.h`; the HAL calls `HAL_I2C_MspInit` *by name* from `HAL_I2C_Init()`, so the parameter cannot become `const` without breaking the match |
| `src/display.cpp:143` | native | `constParameterPointer` | same, host pass | **Accepted** — as above |
| `src/sensors.cpp:168` | bluepill | `constParameterPointer` | `HAL_ADC_MspInit( ADC_HandleTypeDef *adc_handle )` never writes through the handle | **Accepted** — fixed by `stm32f1xx_hal_adc.h` for the same by-name callback reason |
| `src/sensors.cpp:168` | native | `constParameterPointer` | same, host pass | **Accepted** — as above |
| `src/rtos_objects.cpp:210` | bluepill | `constParameterPointer` | `vApplicationStackOverflowHook( TaskHandle_t, char *pcTaskName )` reads the task name only | **Accepted** — called by name from the kernel's `tasks.c`, whose prototype is `char *`; a `const` parameter would not match and the kernel would fail to link |
| `src/rtos_objects.cpp:210` | native | `constParameterPointer` | same, host pass | **Accepted** — as above |

### 5.4 Accepted — constant-folding against the test stubs — 8 findings

**Host pass only.** In `native`, two stubs return compile-time constants, and
cppcheck inlines them:

* `test/stubs/event_groups.h` — `xEventGroupGetBits()` always returns `0U`
* `test/stubs/stm32f1xx_hal.h` — `HAL_GPIO_ReadPin()` always returns `GPIO_PIN_RESET`

so control flow that depends on them is provably constant *to a tool that sees
only the stubs*. On the target both are real, data-dependent API calls, and
the firmware pass reports none of these lines.

| File:Line | Check | Cause | Resolution |
| --- | --- | --- | --- |
| `src/display.cpp:395` | `knownConditionTrueFalse` | `if( active )` — `active` is initialised from `xEventGroupGetBits( systemEvents ) & EVENT_ACTIVE` | **Accepted** — stub artifact; genuinely variable in the firmware |
| `src/display.cpp:423` | `knownConditionTrueFalse` | `if( now_alarmed != alarmed )` — both sides from `xEventGroupGetBits( systemEvents ) & EVENT_ALARM` | **Accepted** — stub artifact |
| `src/display.cpp:430` | `knownConditionTrueFalse` | `if( now_active != active )` — same source | **Accepted** — stub artifact |
| `src/display.cpp:434` | `knownConditionTrueFalse` | `if( active )` in the reactivation branch | **Accepted** — stub artifact |
| `src/display.cpp:451` | `knownConditionTrueFalse` | `if( active && redraw )` at the render gate | **Accepted** — stub artifact |
| `src/input.cpp:117` | `knownConditionTrueFalse` | `return ( HAL_GPIO_ReadPin( port, pin ) == GPIO_PIN_SET );` in `encoder_pin_high()` — the stub always returns `GPIO_PIN_RESET` | **Accepted** — stub artifact; the encoder CLK/DT lines are real GPIOs on the target |
| `src/input.cpp:253` | `knownConditionTrueFalse` | `if( raw_dt != candidate_dt )` in the two-sample debounce filter — `raw_dt` comes from the same constant stub, and `candidate_dt` starts equal to it | **Accepted** — stub artifact; contact bounce is precisely what this filter exists to absorb |
| `src/motion.cpp:151` | `knownConditionTrueFalse` | `if( level )` — `level` comes from `pir_output_high()`, i.e. the same constant stub GPIO read | **Accepted** — stub artifact; the PIR output is a real GPIO on the target |

### 5.5 Accepted — definitions whose prototype is visible — 2 findings

| File:Line | Env | Check | Cause | Resolution |
| --- | --- | --- | --- | --- |
| `src/display.cpp:143` | native | `unusedFunction` | `HAL_I2C_MspInit` is called *by name* from `HAL_I2C_Init()`, which lives in the framework package and is outside `check_src_filters`. In the firmware pass ST's `stm32f1xx_hal_i2c.h` also **declares** the function (line 543), so cppcheck treats it as part of the HAL's public interface and stays quiet; the host stub header does not declare it, so there the definition looks file-local and unreferenced | **Accepted** — not dead code: it is the routine that enables the I2C GPIO clocks in every firmware build |
| `src/sensors.cpp:168` | native | `unusedFunction` | `HAL_ADC_MspInit`, same mechanism — declared by `stm32f1xx_hal_adc.h` (line 897) in the firmware pass, undeclared in the stub | **Accepted** — not dead code; it configures the ADC GPIO clocks |

---

## 6. Reproducing this table

```console
$ pio check
```

* tool: `tool-cppcheck`, cppcheck 2.11
* flags: exactly `[env] check_flags` in `platformio.ini`
* scopes: `default_envs = bluepill_f103c8, native`; both passes check
  `+<src> +<include>`
* expected totals: **0 high / 0 medium / 35 low**, `2 succeeded`

Writing the flags down in `platformio.ini` is what keeps this reproducible:
without them a PlatformIO upgrade could change its own defaults and quietly
change what "passing" means for this table.
