# Deliberate FreeRTOS fault experiments — sections 49–51

Each experiment followed the same discipline:

1. change **one** thing in production source,
2. `pio run`, then one headless `wokwi-cli` run of the stated length,
3. count every serial message shape against the reference run,
4. `git checkout -- <file>`, prove `git diff -- src include` is empty,
5. rebuild the baseline firmware.

Only three files were ever touched: `src/input.cpp`, `src/main.cpp`,
`src/rtos_objects.cpp`. No experiment result is inferred from reading the
code — every number below comes from a serial log or a VCD.

**Reference run** (60 s, no stimulus, restored firmware), produced twice during
this milestone — once as the starting baseline and once after all three
experiments were undone:

| Quantity | Reference |
| --- | --- |
| total lines | 216 |
| `Task A running` / `Task B running` | 60 / 60 |
| `Temperature:` / `Humidity:` / `Light level:` | 30 / 30 / 30 |
| `Page:` / `Alarm:` | 1 / 1 |
| `State: ACTIVE` / `State: INACTIVE` | 1 / 1 |
| `ERR` / stack overflow / assert | 0 / 0 / 0 |
| longest line / empty lines | 27 / 0 |
| host wall clock for 60 s of simulated time | 77 s |

Both runs produced byte-identical counts; the wall clock is from the second
run (`elapsedMs = 76953`).

Task map for reference (`src/main.cpp:216–223`, `configMAX_PRIORITIES = 7`,
`configUSE_TIME_SLICING = 1`):

| Task | Priority | Period |
| --- | --- | --- |
| TaskA, TaskB, DisplayTask | 1 | 1000 ms, 1000 ms, `DISPLAY_POLL_MS` 50 ms bounded wait |
| SensorTask, AlarmTask | 2 | `SENSOR_SAMPLE_PERIOD_MS` 2000 ms, queue-driven |
| InputTask, MotionTask, StateTask | 3 | `INPUT_POLL_PERIOD_MS` 2 ms, 10 ms, queue-driven + 15 s timeout |

---

## 1. Experiment 1 — remove blocking (section 49)

### 1.1 The change

`src/input.cpp:297`, the delay at the end of `InputTask`'s loop, was deleted:

```c
vTaskDelay(pdMS_TO_TICKS(INPUT_POLL_PERIOD_MS));   /* removed */
```

`INPUT_POLL_PERIOD_MS` is `2U` (`src/input.cpp:83`), so the task went from
blocking for 2 ms after every poll to never blocking at all.

**Why this task.** Section 39 defends InputTask's priority 3 with exactly one
property: *"it sleeps after every read"*. Removing the delay is the smallest
edit that invalidates the defence while leaving the priority untouched, so the
result isolates the effect of blocking rather than the effect of priority —
which is what section 50 varies separately.

### 1.2 Observation

Full serial log of the 60 s run — five lines, nothing else was ever written:

```
BCA182 FreeRTOS Multisensor
System starting...
State: ACTIVE
Page: Temperature
State: INACTIVE
```

| Serial message | Reference | Experiment 1 |
| --- | --- | --- |
| total lines | **216** | **5** |
| `Task A running` | 60 | **0** |
| `Task B running` | 60 | **0** |
| `Temperature:` | 30 | **0** |
| `Humidity:` | 30 | **0** |
| `Light level:` | 30 | **0** |
| `Alarm:` | 1 | **0** |
| `Page:` | 1 | 1 |
| `State: ACTIVE` | 1 | 1 |
| `State: INACTIVE` | 1 | 1 |
| `ERR` / stack overflow / assert | 0 / 0 / 0 | 0 / 0 / 0 |
| host wall clock, 60 s simulated | 77 s | 115 s |

### 1.3 Analysis

**Scheduling.** `InputTask` runs at priority 3 and now never leaves the
ready list. Everything at priority 1 and 2 — TaskA, TaskB, DisplayTask,
SensorTask, AlarmTask — is therefore *permanently* preempted: a ready
higher-priority task always wins, so the scheduler never selects them and
they make **zero** progress for the whole run. The counts say it plainly:
60 → 0, 30 → 0, 1 → 0.

**Why two lines survived.**

* `Page: Temperature` is published by `publish_page()` **before** the
  `for(;;)` loop starts, so it runs once while the task still behaves.
* `State: ACTIVE` and `State: INACTIVE` come from `StateTask`, which shares
  priority 3 with the spinning task. `configUSE_TIME_SLICING = 1`
  (`include/FreeRTOSConfig.h:34`) rotates the ready tasks of equal priority
  on every tick, so StateTask still gets one 1 ms slot per tick — plenty for
  a queue check and a print. MotionTask survives for the same reason (it
  produced no lines because there was no stimulus).

**CPU usage implications.** The task now consumes the processor for the whole
2 ms cycle instead of a few microseconds of GPIO reads, so the idle task never
runs and there is no slack left for anything below priority 3. The host needed
115 s of wall clock to advance the same 60 s of simulated time (77 s for the
reference), consistent with the emulator having to execute the spin loop cycle
by cycle instead of reaching a sleep.

**Starvation risk.** This is the failure mode section 19 forbids: a task that
does not block. It is *worse* than a crash, because nothing reports it — there
is no `ERR:` line, no `configASSERT`, no stack overflow (all zero). The system
looks alive (the two priority-3 tasks keep printing) while eight tenths of its
functionality has silently stopped: no sensing, no display, no alarm.

**Effect on other tasks.** Not "slower" — *stopped*. An output task that never
gets scheduled cannot be late, it simply never runs, and a consumer waiting on
`displayQueue` or `alarmQueue` waits forever because the producer is among the
starved.

### 1.4 Restoration

`git checkout -- src/input.cpp`; `git diff -- src include` → empty.

---

## 2. Experiment 2 — change priority (section 50)

### 2.1 The change

`src/main.cpp:219`:

```c
xTaskCreate(DisplayTask, "Display", 256, nullptr, 1, nullptr);
                                         /* became: configMAX_PRIORITIES - 1  → 6 */
```

**Why this task.** Section 39 calls DisplayTask's *"~100 ms I2C flush … the
largest unprivileged stretch of work in the system, which is exactly why it
must sit below the producers"*. It is the task whose work per activation is by
far the largest, so promoting it is the worst case for everyone else — and it
is the experiment section 50 asks for: an unnecessarily high priority on a
task that performs frequent work (a flush on every 2 s sample, every page turn
and every alarm edge).

### 2.2 Observation

Controlled comparison: the *same* scenario (`ft-alarm.yaml`), the *same*
harness diagram and the *same* logic-analyser setup as the reference alarm
run, with only the priority word differing.

| Quantity | Reference | Experiment 2 |
| --- | --- | --- |
| `BUZZER` (PB0) rise | **6.252 s** | **6.363 s** (+111 ms) |
| `BUZZER` (PB0) fall | **12.252 s** | **12.364 s** (+112 ms) |
| `BUZZER` high time | 6.000 s | 6.001 s |
| `BUZZER` transitions in the run | 3 | 3 |
| total lines | 84 | 84 |
| `Task A running` / `Task B running` | 22 / 21 | 22 / 21 |
| `Temperature:` / `Humidity:` / `Light level:` | 11 / 11 / 11 | 11 / 11 / 11 |
| `Alarm:` / `Page:` / `State:` | 3 / 1 / 1+1 | 3 / 1 / 1+1 |
| `expect-pin buzzer:2 == 1` / `== 0` | passed / passed | passed / passed |
| errors / stack overflow / assert | 0 / 0 / 0 | 0 / 0 / 0 |

Serial excerpt (identical in both runs):

```
27: Temperature: 31.00 C
30: Alarm: HIGH_TEMPERATURE
49: Temperature: 25.40 C
52: Alarm: NORMAL
62: State: INACTIVE
```

### 2.3 Analysis

**Why the alarm got 111 ms later.** `SensorTask` hands the sample to the
display **first** and to the alarm **second**:

```c
src/sensors.cpp:445    xQueueOverwrite(displayQueue, &sample);
src/sensors.cpp:446    xQueueOverwrite(alarmQueue,   &sample);
```

At priority 1 the queue write at line 445 makes `DisplayTask` ready but does
not preempt anybody, so `AlarmTask` (priority 2) runs immediately after line
446 and the buzzer edge follows the sample within ~2 ms. At priority 6 the
queue write preempts `SensorTask` on the spot; `DisplayTask` then holds the
CPU for its whole ~100 ms I2C flush, and `AlarmTask` — now *below* it — cannot
run until the flush ends. The measured shift (111 ms / 112 ms) is that flush
plus the queue hand-off. The same mechanism explains the new boot order: `Page: Temperature` now
precedes `State: ACTIVE` once `DisplayTask` no longer sits at the bottom; both
lines still appear exactly once.

**Why the line counts did not move.** Every task in this system has a duty
cycle far below the 100 ms block: the serial producers are well under 1 %,
`DisplayTask` itself is ~5 % (100 ms per 2 s sample), and every period is at
least 1 s. A 100 ms stall is absorbed entirely by the slack that sections 12
and 40 build in, so no message is missed and no count changes.

That is the actual answer to section 50's *"observe whether other tasks become
less responsive"*: **they became less responsive, but not starved.**
Responsiveness has to be measured in *time*, not counted in lines — a line
count is blind to a 111 ms delay. The task that lost margin is exactly the
one that can least afford it: the alarm output of section 7, whose freshness
budget is the reason `AlarmTask` sits at priority 2 in the first place.

**What section 50's priority change does *not* do.** Promoting a task cannot
starve anything unless that task actually uses the CPU. Here the promoted task
still blocks on `DISPLAY_POLL_MS` and on the I2C transfer itself, so it only
borrows the processor in 100 ms bursts — which is why the damage appears as
delay rather than as lost output. Removing the block (experiment 1) is what
turns delay into starvation; the two experiments are complementary halves of
the same lesson.

### 2.4 Restoration

`git checkout -- src/main.cpp`; `git diff -- src include` → empty.

---

## 3. Experiment 3 — remove the serial mutex (section 51)

### 3.1 The change

`src/rtos_objects.cpp:140–160` — the `serialMutex` take/give pair was removed
from `serial_write()`, leaving the bare transmit:

```c
void serial_write( const char *text )
{
    HAL_UART_Transmit(&huart1, (uint8_t *)text, (uint16_t)strlen(text), 1000U);
}
```

### 3.2 Observation

Two independent 60 s runs of the experiment, against the reference:

| Quantity | Reference | Exp. 3 run 1 | Exp. 3 run 2 |
| --- | --- | --- | --- |
| total lines | 216 | **215** | **215** |
| `Page:` | 1 | **0** | **0** |
| `Task A running` / `Task B running` | 60 / 60 | 60 / 60 | 60 / 60 |
| `Temperature:` / `Humidity:` / `Light level:` | 30 / 30 / 30 | 30 / 30 / 30 | 30 / 30 / 30 |
| `Alarm:` / `State:` | 1 / 1+1 | 1 / 1+1 | 1 / 1+1 |
| **malformed / merged / truncated lines** | 0 | **0** | **0** |
| longest line / empty lines | 27 / 0 | 27 / 0 | 27 / 0 |
| errors / stack overflow / assert | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |

Every one of the 215 surviving lines was matched against a pattern covering
all nine message shapes the firmware can emit; none failed. **The output did
not become interleaved. One line disappeared — silently, and reproducibly:
`Page: Temperature`.**

### 3.3 Analysis

**Why a lost line instead of garbled bytes.** `HAL_UART_Transmit()` is
guarded by the handle state, `stm32f1xx_hal_uart.c:1145`:

```c
if (huart->gState == HAL_UART_STATE_READY) { ... gState = HAL_UART_STATE_BUSY_TX; ... }
else                                      { return HAL_BUSY; }        /* line 1208 */
```

The first caller sets `gState = BUSY_TX` for the entire ~1.3 ms transfer
(15 bytes at 115200 baud). Any second caller arriving inside that window is
rejected at line 1208 **without writing a single byte to the data register**,
so the two transfers can never interleave — the HAL refuses the loser outright.
And `serial_write()` ignores that return value on purpose
(`src/rtos_objects.cpp:150` documents it as a condition the caller could not
act on), so without the mutex the loss is **completely undetectable**: no
error line, no garbling, no CRC — the message is simply not there.

**Why the same line every run.** Task phases are fixed from boot (TaskA and
TaskB 1000 ms apart by 500 ms, SensorTask 2000 ms), so the collision points do
not drift. The one collision that exists in 60 s happens at the very start:
`StateTask` (priority 3) begins transmitting `State: ACTIVE`, the 1 ms tick
time-slices to `InputTask` (also priority 3) which then calls
`publish_page()` → `serial_write("Page: Temperature\r\n")`, finds `gState`
busy, and loses the whole line. The reference runs, where the mutex serialises
those two transmits, print both.

**Garbling is still possible — the mutex is what rules it out.** The HAL's
guard is a *check-then-set* with nothing between `gState == READY` (line 1145)
and `gState = BUSY_TX` (line 1153). Two tasks that entered on the same tick
could both pass the check and then alternate bytes into `TDR`. That window is
a handful of instructions wide, which is why 120 s of simulated time without
the mutex produced no visible interleaving; a mutex closes it *deterministically*
for every schedule, including the ones a test run does not happen to hit. This
is precisely the argument in section 37, and `src/main.cpp:258–264` already
records that before the mutex existed the 500 ms phase offset alone was not
enough: *"a colliding task still lost its whole line to HAL_BUSY"* — which is
exactly the behaviour measured here.

**Result versus section 51's expectation.** Section 51 asks to observe whether
output becomes interleaved. It did not: the failure mode on this hardware and
this HAL is **silent single-line loss**, which is arguably worse than
interleaving, because interleaving is at least visible on inspection while a
dropped line looks exactly like a task that never printed.

Side effect worth recording: with the mutex gone there is also no priority
inheritance on the serial path (`src/main.cpp` / section 14), so a priority-1
task holding the line would be able to delay a priority-2 producer by the full
transmit time. Not observed here — no such overlap occurred — but it is the
second half of what the mutex buys.

### 3.4 Restoration

`git checkout -- src/rtos_objects.cpp`; `git diff -- src include` → empty.

---

## 4. Restoration and final verification (Part 3)

After all three experiments the working tree was restored and the firmware
rebuilt from source.

| Check | Command | Result |
| --- | --- | --- |
| source untouched | `git diff -- src include` | **empty** |
| working tree | `git status --porcelain` | only `M .gitignore` (+6 lines) and untracked `.vscode/` |
| flash / RAM | `pio run` | **48.2 %** (31588 / 65536), **55.4 %** (11348 / 20480) — identical to baseline |
| unit tests | `pio test` | **29 / 29** passed |
| static analysis | `pio check` | **0 high / 0 medium / 35 low** |
| diagram lint | `wokwi-cli lint diagram.json` | *No issues found* |
| 60 s headless run | `wokwi-cli . --timeout 60000` | **216 lines**, every count identical to the reference run |
| encoder / PIR stimulus | scenario runs | harness only; `diagram.json` never modified |

The three faults were observable only while they were present, and all three
are gone from the source: this milestone's commit contains documentation and a
`.gitignore` rule, not code.

---

## 5. Reproducing these experiments

```console
$ pio run                                     # after each edit
$ wokwi-cli . --timeout 60000 --timeout-exit-code 0 \
      --serial-log-file <name>.log            # 60 s reference / E1 / E3
$ wokwi-cli . --scenario ft-alarm.yaml --diagram-file diagram-harness.json \
      --vcd-file <name>.vcd \
      --serial-log-file <name>.log            # reference / E2 buzzer timing
$ git checkout -- <file>                      # restore, then
$ git diff -- src include                     # must be empty
```

Expected headline numbers:

| Experiment | Expected |
| --- | --- |
| reference | 216 lines; 60 / 60 / 30 / 30 / 30 |
| §49 — delay removed from `InputTask` | **5 lines**; Task A, Task B and all sensor output at 0 |
| §50 — `DisplayTask` at priority 6 | 84 lines (unchanged); buzzer rise **6.363 s**, fall **12.364 s** instead of 6.252 s / 12.252 s |
| §51 — `serial_write()` mutex removed | **215 lines**; `Page: Temperature` missing; no malformed lines |
