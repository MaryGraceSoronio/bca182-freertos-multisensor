# A FreeRTOS Multisensor Room Monitor on an STM32 Blue Pill

> **Draft status — read before publishing.** This is the complete, copy-paste-ready Hackster.io post required by sections 61–63 of the BCA182 laboratory specification. It has **not** been published anywhere: the repository is still local-only and every link below is either an in-repo file path or an explicit placeholder. Finish the *Publication checklist (manual steps)* at the very end before the post goes live.

An STM32 Blue Pill reads temperature and humidity, measures ambient light, watches a PIR motion sensor, shows all of it on a small OLED, buzzes when the room gets too warm, and puts the display to sleep when nobody has walked past for fifteen seconds. The parts list is ordinary; the interesting part is the structure — eight FreeRTOS tasks, three queues, one event group and one mutex, each with a stated job, a stated priority and a stated reason, backed by unit tests, static analysis, a serial log from a headless simulator run, and three deliberate "let's break it" experiments.

## Project Overview

The build is a room monitor, simulated end to end in Wokwi:

* a **DHT22** gives temperature and humidity, an **LDR** gives ambient light, and a **PIR** reports motion;
* an **SSD1306 OLED** shows the readings, paged by a **KY-040 rotary encoder**;
* a **buzzer** sounds when temperature leaves 18.0–30.0 °C;
* **USART1** prints everything at 115200 baud, which is where most of the evidence on this page comes from.

The firmware targets the STM32F103C8T6 ("Blue Pill") using the STM32Cube HAL and native FreeRTOS APIs — no Arduino layer — built with PlatformIO. The application is split one module per subsystem (`sensors`, `display`, `input`, `alarm`, `motion`, `system_state`, plus the shared RTOS objects), and `src/main.cpp` does nothing but the four-stage startup the lab specification asks for: initialise hardware, create objects, create tasks, start the scheduler.

*Image placement: `docs/images/wokwi-circuit.png` (Figure 1) goes in the Circuit section below; `docs/images/finished-system.png` (Figure 5) goes in Demonstration.*

## Motivation

Four sensors and a screen could be driven by one giant `loop()` full of `delay()` calls. That version is also impossible to reason about, because the four inputs do not care about each other: the encoder must not miss a detent, however briefly the knob moves; the PIR's output pulse is the only copy of the evidence that someone walked in; the sensor sample may legitimately be two seconds old; and the display may be as slow as it likes.

An RTOS turns each of those into its own task with its own period, its own blocking behaviour and its own priority, so "how urgent is this?" is answered once, in a table, instead of rediscovered every time a delay is added. The project was built for **BCA182 Laboratory Activity 1**, which requires that reasoning — plus queues, a mutex, an event group, unit tests and a static-analysis pass — to be documented rather than asserted.

## Features

* **Periodic sampling** — `SensorTask` reads DHT22 + LDR every 2000 ms on a `vTaskDelayUntil()` period; a bad DHT22 checksum is retried up to three times before the sample is dropped.
* **Temperature alarm** — `AlarmTask` compares each sample against the 18.0–30.0 °C window through a pure function and drives the buzzer pin.
* **Paged OLED display** — `DisplayTask` is the only task allowed to touch the I2C bus; four pages (Temperature, Humidity, Light, Motion), wrapping in both directions.
* **Encoder navigation** — `InputTask` polls and debounces the KY-040 every 2 ms and publishes the selected page.
* **ACTIVE / INACTIVE state machine** — 15 s with no motion blanks the OLED once and ignores the encoder; motion brings everything back, while sampling and the alarm keep running.
* **Atomic serial output** — every line is written under a mutex, so two tasks can never interleave or lose a line to a bus collision.
* **Diagnostics** — two small tasks print alternating lines once a second, demonstrating blocking waits with no busy loop anywhere in the system.

## Components

Every component below is a **simulated** Wokwi part; no hardware was purchased or flashed for this build.

| Component | Wokwi part | Interface | Role |
| --- | --- | --- | --- |
| MCU | `board-stm32-bluepill` (STM32F103C8T6) | 72 MHz, 64 KB flash, 20 KB RAM | runs the firmware and the FreeRTOS kernel |
| DHT22 | `wokwi-dht22` | 1-wire on PA1, external 10 kΩ pull-up | temperature + humidity |
| LDR | `wokwi-photoresistor-sensor` | ADC1_IN0 on PA0 | ambient light, 12-bit |
| PIR | `wokwi-pir-motion-sensor` | GPIO on PB8 | motion evidence |
| OLED | `board-ssd1306` (128×64) | I2C1 on PB6/PB7, address 0x3C | four paged readings |
| Rotary encoder | `wokwi-ky-040` | GPIO on PB12/PB13/PB14 | page selection |
| Buzzer | `wokwi-buzzer` | GPIO output on PB0, active high | temperature alarm |
| Pull-up | `wokwi-resistor`, 10 kΩ | PA1 → 3V3 | defined rise time for the 1-wire bus |
| Host PC | Wokwi serial monitor | USART1 PA9/PA10, 115200 8N1 | log output and verification evidence |

`diagram.json` declares all eight parts and all twenty-four connections, and it passes `wokwi-cli lint` with no issues.

## Circuit

![Wokwi circuit](docs/images/wokwi-circuit.png)

*Figure — the Wokwi circuit: eight parts and twenty-four connections, exactly as declared in `diagram.json`. Upload `docs/images/wokwi-circuit.png` (SVG source: `wokwi-circuit.svg`) here.*

The wiring is conventional for a Blue Pill: the LDR analogue output lands on PA0 (ADC1_IN0, 12-bit single conversion); the DHT22 data line is PA1, open drain with a discrete 10 kΩ pull-up to 3V3 so the one-wire handshake has a defined rise time; the buzzer is driven high on PB0; the SSD1306 sits on I2C1 at PB6/PB7 with external pull-ups; the PIR output is polled on PB8 every 10 ms; the encoder uses PB12/PB13/PB14 (the SW pin is wired but deliberately unmapped — rotation only); and USART1 transmits on PA9 with PA10 configured but unused. The board's own LED on PC13 is switched off during init and never used again.

Publishing the pin list next to the picture is deliberate: the diagram and `src/` are meant to be checked against each other line by line.

## System Architecture

![System architecture](docs/images/architecture.png)

*Figure — one column per subsystem: Wokwi part → driver module → FreeRTOS task → what it publishes or drives. Upload `docs/images/architecture.png` (SVG source: `architecture.svg`) here.*

The design rule is ownership. Each column is owned end to end by one module and one task: `DisplayTask` alone writes the OLED, `sensors.cpp` alone bit-bangs the DHT22, and every serial line in the whole system funnels through one function, `serial_write()` in `src/rtos_objects.cpp`. That last fact is what makes the mutex necessary — and sufficient.

`src/main.cpp` contains no application logic at all: hardware init → object creation → task creation → `vTaskStartScheduler()`, in that order, with the priority justification table sitting immediately above the `xTaskCreate` calls it applies to.

## FreeRTOS Architecture

![FreeRTOS task communication](docs/images/freertos-tasks.png)

*Figure — each row is one producer → kernel object → consumer path, with its message type and blocking behaviour. Upload `docs/images/freertos-tasks.png` (SVG source: `freertos-tasks.svg`) here.*

The kernel runs preemptive scheduling with round-robin time slicing, seven priority levels, a 1000 Hz tick, a 9 KB heap, mutexes enabled and stack-overflow checking turned on. Eight tasks occupy three levels:

| Level | Tasks | Why |
| --- | --- | --- |
| 3 | `InputTask` (2 ms), `MotionTask` (10 ms), `StateTask` (event wait) | one-shot, edge-like work — a missed knob detent or PIR pulse cannot be replayed |
| 2 | `SensorTask` (2000 ms), `AlarmTask` (queue-driven) | producer and consumer share a level, because the alarm can never be fresher than the sample feeding it |
| 1 | `DisplayTask` (≤ 50 ms wait), `TaskA`/`TaskB` (1000 ms) | presentation and diagnostics — nobody sets a deadline for a pixel or a log line |

Every priority has a written justification in `src/main.cpp` covering what breaks if it is too high and what breaks if it is too low: not "this task is important", but the actual latency consequence of getting it wrong.

Inter-task communication is deliberately mixed:

* **Three length-1 queues with `xQueueOverwrite`** (`displayQueue`, `alarmQueue`, `modeQueue`) — only the newest sample is worth rendering, and a length-1 overwrite queue means a producer never blocks on a slow consumer. `DisplayTask` can take its full ~100 ms OLED flush without stalling `SensorTask`.
* **One event group with three bits** — `EVENT_ACTIVE`, `EVENT_MOTION`, `EVENT_ALARM`. These are *levels*, so readers use `xEventGroupGetBits()` without blocking and without consuming the producer's evidence. Only `StateTask` ever clears `EVENT_MOTION`.
* **One mutex** (`serialMutex`) — the shared resource is a peripheral, not a datum, so a mutex with priority inheritance is the right tool.

There is no uncontrolled busy loop anywhere: every task sleeps on `vTaskDelay()`, `vTaskDelayUntil()`, a bounded queue receive, or a timeout-bounded event-group wait.

## How It Works

![System state machine](docs/images/state-machine.png)

*Figure — two states, one timeout edge, one motion edge, and the pure decision function that guards both. Upload `docs/images/state-machine.png` (SVG source: `state-machine.svg`) here.*

On boot the firmware prints its banner, creates the FreeRTOS objects and starts the scheduler. The system comes up **ACTIVE**: `State: ACTIVE` appears on the serial line before any consumer could read a stale event group, and the OLED settles on the Temperature page. Every two seconds a new sample arrives and the values on screen and on the serial port change together, because both come from the same `SensorData` struct.

Turning the encoder steps Temperature → Humidity → Light → Motion and wraps in both directions. Passing 30.0 °C raises `Alarm: HIGH_TEMPERATURE`, drives PB0 high and appends `ALARM` to the OLED value line; returning to the window clears it again.

Fifteen seconds with no motion and `StateTask` transitions to **INACTIVE**: the OLED is blanked once, encoder detents are consumed and ignored, `EVENT_ACTIVE` is cleared — while sampling, PIR polling and the alarm keep running, because the room is still being monitored, only the display has gone quiet. A PIR edge on PB8 sets `EVENT_MOTION`, `StateTask` wakes, restores the display and re-opens the encoder.

The decision itself lives in a pure function, `evaluateSystemState()` in `include/system_state.h`: motion always wins, a timeout while ACTIVE falls asleep, every other combination holds the current state. Keeping it pure — no HAL, no queue, no clock — is what lets it be unit tested on a desktop.

## Testing and Verification

**1. Unit tests — 29/29 pass.** `test_alarm` (10) locks down the `evaluateTemperature()` boundaries at 18.0/30.0 °C inclusive, `test_navigation` (11) covers both wrap directions, and `test_state` (8) covers motion-wins, timeout-only-from-ACTIVE and hold. They compile the *production* sources, not copies, so firmware and tests cannot drift. Run them with `pio test`.

**2. Static analysis — `pio check`: 0 high / 0 medium / 35 low.** Every finding is classified in `docs/static-analysis.md`: three were fixed in code, the rest documented as vendor casts, macro-mandated parameters or constant-folding against the test stubs. The point was to interpret the output, not to screenshot it.

**3. Functional verification — FT-01 … FT-10, all PASS**, each with observed serial line numbers, VCD timestamps or OLED screenshots in `docs/functional-verification.md`. Honest caveat: Wokwi exposes no automation control for the rotary encoder or the PIR, so four rows (FT-04, FT-05, FT-08, FT-10) were driven electrically with momentary buttons on the same nets. The firmware path is proven; the physical elements should also be clicked by hand in the Wokwi web UI.

**4. Deliberate fault experiments** (`docs/fault-experiments.md`) — three regressions introduced one at a time, observed, analysed and reverted: removing a blocking delay, changing a task priority, and deleting the serial mutex. The third is the convincing one: without the mutex, colliding writers lose whole lines to a `HAL_BUSY` collision.

**Final verification, re-run immediately before writing this draft:**

| Check | Result |
| --- | --- |
| `pio run` | SUCCESS — Flash 48.2% (31588/65536), RAM 55.4% (11348/20480) |
| `pio test` | 29/29 passed |
| `pio check` | 0 high / 0 medium / 35 low |
| `wokwi-cli lint` | no issues |
| 60 s headless run | 216 serial lines; 30/30/30 sensor samples, 60/60 diagnostics with no consecutive identical pair, banner once, ACTIVE → INACTIVE after ~15 s, 0 errors, longest line 27 characters |
| `git status --short` | clean — only the untracked `.vscode/` editor directory remains |

## Demonstration

![Finished system](docs/images/finished-system.png)

*Figure — the OLED after 9 seconds of a steady-state run, showing the temperature page rendering a live 25.40 °C sample. Upload `docs/images/finished-system.png` here.*

Demonstrable **without touching anything**: the live Wokwi simulation (`wokwi-cli .`, or the VS Code Wokwi extension) booting to the banner; the serial output — samples every two seconds, page changes, alarm transitions and the `State:` lines; the four OLED pages including the `ALARM` marker; the temperature alarm, driven by raising the simulated DHT22 temperature; and the ACTIVE → INACTIVE → ACTIVE cycle, which happens on its own within a minute of a stimulus-free run.

Requires **manual interaction** in the Wokwi web UI, because the simulator exposes no automation control for those parts:

* **rotating the encoder** — click the knob and turn it each way to watch the page step and wrap;
* **triggering the PIR** — click *Simulate Motion* once while ACTIVE and once while INACTIVE.

The four functional-test rows that depend on those interactions were verified with equivalent electrical stimulus on the same nets and are marked as such in `docs/functional-verification.md`. This page does not claim a video, a gallery or a hosted demo beyond the repository — none exists yet.

## Challenges Encountered

* **The Wokwi port froze before the first task ran.** Getting the FreeRTOS Cortex-M3 port to hand over correctly under the simulator took a debugging session written up in the laboratory report.
* **DHT22 timing on a 72 MHz part.** `HAL_GetTick()` is 1 ms and `HAL_Delay()` blocks, neither of which survives a one-wire frame. The driver counts CPU cycles with the DWT counter and suspends the scheduler only inside the ~5 ms frame, before anything is printed; read failures are retried rather than trusted.
* **Lost serial lines.** Before the mutex, a colliding writer lost its whole line to `HAL_BUSY`. A phase offset between the two diagnostics was not enough; one take/give around the transmit was.
* **A stack overflow on the first page turn.** `DisplayTask` started at 128 words; formatting plus the HAL transmit overflowed it. Stacks are now sized from actual call chains, and the failure is recorded rather than quietly fixed.
* **A priority that was too high, not too low.** `TaskA` sat at priority 2 until it was observed preempting an in-progress OLED flush to print a demo line — a cost with no benefit. It moved to 1, and the reasoning went into the priority table.
* **Wokwi will not automate the knob or the PIR.** Every control name for those parts is silently ignored, so the harness had to drive the nets electrically instead. Documented honestly rather than papered over.

## Lessons Learned

* **Put the decisions in pure functions.** `evaluateTemperature()` and `evaluateSystemState()` take values and return values — that one habit is why 29 tests run on a desktop against the exact objects the firmware uses, with no test guards anywhere in production code.
* **Ownership beats locking.** One task, one bus, zero mutexes around I2C1; the rule is simpler than a lock and impossible to forget at the call site.
* **Length-1 overwrite queues remove a class of bug.** If only the newest sample matters, "slow consumer" stops being a deadlock risk.
* **Priorities should be argued, not assigned.** Writing down what breaks if a level is too high *and* too low surfaces mistakes that "this task feels important" never would.
* **Verification belongs in the build.** The baseline run is reproducible from a two-line command, and the fault experiments only convinced because a clean baseline existed first.

## Limitations

* **Everything is simulated.** The DHT22, LDR, PIR and encoder are Wokwi models; timing the simulator tolerates should be re-measured on real silicon.
* **Four functional-test rows used synthetic stimulus** on the encoder and PIR nets — the firmware path is exercised, the element models are not.
* **No CPU-load figures.** Runtime statistics are compiled out (`configGENERATE_RUN_TIME_STATS = 0`), so per-task utilisation is argued from blocking budgets, not measured — and therefore not claimed here.
* **No power figures and no latency guarantees** — neither was measured, so neither is claimed.
* **No hardware-in-the-loop run.** The upload path exists but was never executed; the laboratory scope is simulation.
* **USART1 RX is configured but unused** — the system is output-only.

## Future Improvements

* Turn on runtime statistics with a timer, so the argued-but-unmeasured utilisation story becomes a measured one.
* A small command protocol on the already-initialised USART1 RX.
* A watchdog task asserting that each task reported in within its budget, making the blocking-budget table executable.
* Real lux calibration for the LDR, with the constants stored in flash.
* Hardware-in-the-loop validation on a physical Blue Pill once the simulation scope of the laboratory is finished.

## GitHub Repository

**Source code:** `https://github.com/<your-username>/bca182-freertos-multisensor`

> **The repository is not yet public.** It exists locally with its full commit history — one commit per engineering milestone, no bulk commits — and has not been pushed to GitHub. Replace `<your-username>` above with the real account name when the repository is published, then update this line before submitting the post.

What is in it:

```
include/  src/     firmware - one module per subsystem, main.cpp only starts things
test/     29 host unit tests + the stubs the host build resolves
docs/     laboratory report (md + pdf), static-analysis table,
          functional-verification record, fault experiments, this article
assets/   BSD licence text for the font table
diagram.json  wokwi.toml  platformio.ini   the simulation and build of record
```

## References

* **Laboratory specification** — *BCA182 – Laboratory Activity 1*, prepared by Asst. Prof. Paul Rodolf P. Castor, M.Sc., MSU-IIT, September 2026. The problem statement and the section numbering used throughout this project come from it.
* **FreeRTOS** — Richard Barry and the FreeRTOS authors; the kernel shipped inside PlatformIO's `framework-stm32cubef1`. https://www.freertos.org/
* **STM32Cube HAL / CMSIS** — STMicroelectronics; `stm32f1xx_hal` drivers (GPIO, ADC, I2C, UART) are used throughout.
* **PlatformIO** — build system, `pio test` Unity runner, `pio check` cppcheck integration. https://platformio.org/
* **Wokwi** — the browser-based simulator and its CLI, used for every run quoted on this page. https://wokwi.com/
* **cppcheck** — the static analyser behind the 0/0/35 finding count. https://cppcheck.sourceforge.io/
* **Adafruit GFX 5×7 font** — the glyph table in `include/glcdfont.h` is redistributed under its BSD licence; the full text is committed at `assets/LICENSE-glcdfont.txt`.

---

### Publication checklist (manual steps)

Actions for the person publishing this draft — none of this has been done automatically, and none of it can be done from inside the repository:

1. **Push the repository to GitHub** from the repository root: `git remote add origin https://github.com/<your-username>/bca182-freertos-multisensor.git`, then `git push -u origin main`.
2. **Set the GitHub repository visibility to Public.**
3. **Replace `<your-username>`** in the *GitHub Repository* section above with the real URL, and delete this checklist block before pasting the article into Hackster.
4. **Publish this article on Hackster.io** — create the project, paste the sections above, and upload the five figures from `docs/images/` (`wokwi-circuit.png`, `architecture.png`, `freertos-tasks.png`, `state-machine.png`, `finished-system.png`) at the marked positions.
5. **Add the collaborator** for BSCA accreditation: invite **Paul Rodolf P. Castor**, `paulrodolf.castor@g.msuiit.edu.ph`, as a collaborator on the Hackster project.
6. **Confirm the GitHub link** on the published Hackster project points at the now-public repository.
