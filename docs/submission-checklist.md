# Submission checklist audit — section 65

**Scope.** Every row of the section 65 table, verbatim, plus a status and the
evidence (or the exact action still outstanding) for that row. Followed by a
self-audit against the section 66 grounds for rejection and a cross-check of
the deliverables against sections 55, 56, 57, 59 and 38/39.

**Basis.** Repository state after the milestone commits that followed this
document's first draft — `Commit functional test harness` (adds `test/wokwi/`)
and `Resolve repository URL placeholders` (replaces the placeholder URLs) —
which sit on top of `dff9c82` (`docs/hackster-article.md` + the README
cross-link). Between the first push of this audit and its second, the remote
also received two README commits made outside this session (`ce8a617`,
`6131c3e`, title/description only); the two milestone commits of this audit
were replayed on top of them, so no published history was rewritten.
Verification re-run for this audit:

| Command | Result |
| --- | --- |
| `pio run` | SUCCESS — Flash 48.2% (31588/65536), RAM 55.4% (11348/20480) — unchanged |
| `pio test` | 29/29 passed |
| `pio check` | 0 high / 0 medium / 35 low |
| `wokwi-cli lint` | no issues |
| `wokwi-cli . --timeout 60000` | exit 0, 216 serial lines, all M13 criteria intact |
| `wokwi-cli . --diagram-file test/wokwi/diagram-harness.json --scenario test/wokwi/ft-alarm.yaml --timeout 45000` | exit 0, `Scenario completed successfully`, both `expect-pin` assertions reached, 84 serial lines |
| `wokwi-cli . --diagram-file test/wokwi/diagram-harness.json --scenario test/wokwi/ft-sensors.yaml --timeout 60000` | exit 0, `Scenario completed successfully`, 119 serial lines, 28.00/31.00/25.40 °C, 45.00/61.20 %, 38/76 % all observed |
| `wokwi-cli . --diagram-file test/wokwi/diagram-harness.json --scenario test/wokwi/ft-io.yaml --timeout 60000` | exit 0, `Scenario completed successfully`, 127 serial lines — `Page:` 21/27/29/35/39/46/53/61, `Motion: detected` l.13, `State: INACTIVE` l.78, `Motion: detected` l.93 → `State: ACTIVE` l.94, identical to run B |
| `git status --short` | `?? .vscode/` only — untracked by design, never staged |

---

## 1. Section 65 checklist

| # | Category | Requirement | Status | Evidence / next action |
| --- | --- | --- | --- | --- |
| 1 | Build | ☐ pio run succeeds | **DONE** | `pio run` → `SUCCESS`, both envs (`bluepill_f103c8`, `native`); Flash 48.2% (31588/65536), RAM 55.4% (11348/20480). Re-run for this audit. |
| 2 | FreeRTOS | ☐ At least five meaningful tasks | **DONE** | 8 tasks: `SensorTask`, `DisplayTask`, `InputTask`, `AlarmTask`, `MotionTask`, `StateTask`, `TaskA`, `TaskB`. Created at `src/main.cpp:216-223`; each defined in its own module (`src/sensors.cpp:388`, `src/display.cpp:385`, `src/input.cpp:207`, `src/alarm.cpp:182`, `src/motion.cpp:137`, `src/system_state.cpp:66`, `src/main.cpp:266/277`). |
| 3 | FreeRTOS | ☐ Explicit priorities | **DONE** | Priorities 3/3/3/2/2/1/1/1 passed literally to `xTaskCreate` (`src/main.cpp:216-223`) with a full justification table (too-high / too-low / latency budget) at `src/main.cpp:104-215`; commit `79dab45` "Document and justify task priorities". |
| 4 | FreeRTOS | ☐ Queue | **DONE** | Three length-1 queues with `xQueueOverwrite`: `displayQueue`, `alarmQueue`, `modeQueue` — created `src/rtos_objects.cpp:66-68`, declared `include/rtos_objects.h:48-63`. Exercised by FT-01..FT-05. |
| 5 | FreeRTOS | ☐ Mutex | **DONE** | `serialMutex = xSemaphoreCreateMutex()` at `src/rtos_objects.cpp:79`, taken/given around the whole `HAL_UART_Transmit()` (`src/rtos_objects.cpp:147`). Shared resource, competing tasks and failure mode documented at `include/rtos_objects.h:113-175`. Commit `b5fb6b8`. |
| 6 | FreeRTOS | ☐ Event group or task notification | **DONE** | `systemEvents = xEventGroupCreate()` (`src/rtos_objects.cpp:69`) with `EVENT_ACTIVE`/`EVENT_MOTION`/`EVENT_ALARM` = BIT0/BIT1/BIT2 (`include/rtos_objects.h:82-84`). Commit `c67538a`. |
| 7 | FreeRTOS | ☐ vTaskDelayUntil() used appropriately | **DONE** | `src/sensors.cpp:448` — `SensorTask` on a fixed 2000 ms period; rationale at `src/sensors.cpp:365-368`. |
| 8 | FreeRTOS | ☐ ACTIVE/INACTIVE state machine | **DONE** | `src/system_state.cpp` + pure `evaluateSystemState()` in `include/system_state.h`; 8 unit tests in `test/test_state/`; FT-09/FT-10 observed (`docs/functional-verification.md` §3.4). Commits `80eea57`, `e9d5d4b`. |
| 9 | Simulation | ☐ DHT22, LDR, PIR, rotary encoder, OLED, buzzer | **DONE** | `diagram.json`: 8 parts, 24 connections — `board-stm32-bluepill`, `wokwi-dht22`, `wokwi-resistor` (10 k pull-up), `wokwi-photoresistor-sensor`, `board-ssd1306`, `wokwi-ky-040`, `wokwi-buzzer`, `wokwi-pir-motion-sensor`. All six required elements present plus the pull-up. |
| 10 | Simulation | ☐ Wokwi simulation operational | **DONE** | Headless 60 s run re-run for this audit: exit 0, 216 lines, banner once, 30/30/30 samples, 60/60 diagnostics with 0 consecutive identical pairs, ACTIVE→INACTIVE at ~15 s (line 3 → line 60, 15 `Task A` lines between), 0 errors, max line 27 chars. `wokwi-cli lint`: no issues. |
| 11 | Testing | ☐ At least 13 meaningful unit tests | **DONE** | 29 tests over three decision functions: `test_alarm` 10, `test_navigation` 11, `test_state` 8. Boundary cases (18.0/30.0 inclusive), both wrap directions, all four state combinations — well above the 13 minimum and non-trivial. |
| 12 | Testing | ☐ pio test succeeds | **DONE** | `pio test` → `29 test cases: 29 succeeded`. Production sources compiled in via `test_build_src = yes`; commit `ee5c79c`. |
| 13 | Testing | ☐ Functional verification table completed | **DONE** | `docs/functional-verification.md` §2: FT-01..FT-10, each with Input/Stimulus, Expected, **observed** Actual (serial line numbers / VCD timestamps / OLED shots) and PASS. **Reproducibility:** the five scenario files and the harness diagram are now committed at `test/wokwi/` with copy-paste commands, per-scenario timeouts and the scenario→FT mapping in `test/wokwi/README.md`; three of them (`ft-alarm.yaml`, `ft-sensors.yaml`, `ft-io.yaml`) were re-run end-to-end for this audit with exit 0 and the expected observations. **Remaining caveat (honest):** FT-04, FT-05, FT-08, FT-10 are still driven by **button stand-ins** — `btn_cw`/`btn_ccw`/`btn_pir` inject the edges electrically because Wokwi exposes no automation for the encoder or the PIR, and in the harness the `wokwi-pir-motion-sensor`'s `OUT` is deliberately left unconnected so `btn_pir` owns the `B8` net. The firmware path is proven; the two element models are not (stated in-row and in §4 of that file). *Next action (human, optional but recommended):* turn the actual knob and click *Simulate Motion* once each in the Wokwi web UI to confirm the element models, and note it in the record. |
| 14 | Testing | ☐ Fault experiments completed | **DONE** | `docs/fault-experiments.md`: §49 remove blocking, §50 change priority, §51 remove serial mutex — each with the change, the observed serial/VCD symptom, the analysis and the verified restoration (`git diff -- src include` empty afterwards). Commit `4973758`. |
| 15 | Quality | ☐ pio check completed | **DONE** | `pio check` → **0 high / 0 medium / 35 low** (21 firmware pass + 14 native pass), reproduced for this audit. |
| 16 | Quality | ☐ Findings analyzed and significant warnings addressed | **DONE** | `docs/static-analysis.md`: §3 summary, §4 the 3 findings resolved by code changes, §5 full §46-style table (Finding / File-Line / Cause / Resolution) for all 35, §6 reproduction. Commit `8342448` + `3269dfe`. |
| 17 | GitHub | ☐ Public repository | **DONE** | Repository is published: remote `origin` = `https://github.com/MaryGraceSoronio/bca182-freertos-multisensor`, visibility **Public**, `main` pushed. Evidence: `git status -sb` → `main...origin/main` with no ahead/behind and `git ls-remote --heads origin` returning the local tip, both after the push of this audit. The remote had stood at `13d6e34` when this audit began and at `6131c3e` (two concurrent README edits) at push time; the two milestone commits of this audit were rebased onto that tip rather than force-pushed over it. History is milestone-per-commit, nothing pushed as a bulk commit. |
| 18 | GitHub | ☐ Meaningful commit history | **DONE** | One commit per engineering step — 30 of them for M0–M16, followed by the M17 documentation commits; nothing is a bulk commit and no §53-banned message (`update`, `changes`, `working`, `final`, `final2`, `finalfinal`) appears as a subject. Examples: `c82b31b Initialize STM32 PlatformIO project`, `043308a Add sensor data queue`, `b5fb6b8 Protect serial output with a mutex`, `4973758 Run deliberate FreeRTOS fault experiments`, `7bd389d Write laboratory report`, `dff9c82 Draft Hackster.io portfolio post`. |
| 19 | GitHub | ☐ Professional README | **DONE** | `README.md` (587 lines) contains **all 21 §55 headings in order**, all 5 §56 visuals with `*Figure N - …*` captions, build/test/analysis/verification sections, and no unfinished-work markers. Audience is the engineer/evaluator required by §54, not an academic worksheet. Commit `b60b50d`. |
| 20 | GitHub | ☐ Architecture diagrams | **DONE** | 5 visuals exist in `docs/images/` as PNG **and** editable SVG: `wokwi-circuit`, `architecture`, `freertos-tasks`, `state-machine`, `finished-system`. Cross-checked against source for this audit (see §3 below): priorities, stack depths, periods, event-bit numbers, pin names and part/connection counts all agree. |
| 21 | GitHub | ☐ No unnecessary binaries/build artifacts | **DONE** | Every tracked file was scanned: no `.pio/`, `*.elf`, `*.bin`, `*.o`, `*.hex`, `*.map`, `*.vcd`, `*.log`, screenshots or `.exe` anywhere in the index. The only non-text tracked files are the 5 PNG figures, their 4 SVG sources and `docs/laboratory-report.pdf` (§57 requires that one). `.gitignore` covers `.pio/`, `*.vcd`, `screenshot*.png`, `*.serial.log`, `.vscode/*`. |
| 22 | Report | ☐ Laboratory report included | **DONE** | `docs/laboratory-report.pdf` exists at **exactly** the §57 path (996 407 bytes, tracked). Editable source alongside at `docs/laboratory-report.md`. |
| 23 | Report | ☐ FreeRTOS task table | **DONE** | Report §3.2 (`docs/laboratory-report.md:239-256`) uses the exact six §59 columns and all 8 tasks, followed by per-row justification (§3.3) and a note on where we intentionally differ from the §59 example (§3.4). Agrees with the README and `src/main.cpp` (see §3). |
| 24 | Report | ☐ Requirements traceability matrix | **DONE** | Report Appendix A (`docs/laboratory-report.md:962+`): §60 rows FR-01/FR-05/FR-06/FR-07/FR-09 → implementation → verification, plus A.1 functional requirements and A.2 required FreeRTOS concepts. |
| 25 | Report | ☐ Test results and static-analysis discussion | **DONE** | Report §5 (unit tests, FT-01..FT-10, fault experiments, reproduction) and §6 (findings, severity, interpretation, corrective actions); backed by `docs/functional-verification.md`, `docs/fault-experiments.md`, `docs/static-analysis.md`. |
| 26 | Report | ☐ Limitations documented | **DONE** | Report §7.6 "Limitations"; README *Limitations* (6 bullets); `docs/functional-verification.md` §4 "Honest limits of this record"; the Hackster draft's *Limitations*. |
| 27 | Portfolio | ☐ Hackster.io article | **PARTIAL** | Draft complete and committed: `docs/hackster-article.md`, all 16 §63 sections present, in order, none empty, with the §61 final verification recorded. The GitHub side it depended on is now done (row 17): the repository is public and every URL in the article points at it. **Not published** — publication requires a Hackster account and a human. *Next action (human):* create the Hackster project, paste the sections, upload the 5 figures at the marked positions (the article's own publication checklist lists exactly what remains). |
| 28 | Portfolio | ☐ GitHub link and attribution included | **DONE** | Attribution was already complete and committed: article *References* (lab spec + instructor, FreeRTOS, STM32Cube HAL, PlatformIO, Wokwi, cppcheck, Adafruit GFX font with `assets/LICENSE-glcdfont.txt`) and README *References and Acknowledgments*. The GitHub link is now the real one everywhere — `README.md` clone URL and `docs/hackster-article.md` *Source code* line both read `https://github.com/MaryGraceSoronio/bca182-freertos-multisensor`, and a grep for the old angle-bracket username / fork-URL placeholders over every tracked file returns nothing. |

**Totals: 27 DONE · 1 PARTIAL · 0 NOT DONE.**
Row 27 is the only row not yet DONE, and its single blocker is the one action
no repository can perform: publishing the post on Hackster.io. Rows 17 and 28
were closed by pushing the repository and replacing the URL placeholders.

---

## 2. Section 66 self-audit — grounds for rejection

| # | Ground for rejection (§66) | Applies? | Reasoning |
| --- | --- | --- | --- |
| 1 | The project does not compile | **No** | `pio run` → SUCCESS for both environments, re-run for this audit; Flash/RAM unchanged at 48.2% / 55.4%. |
| 2 | The Wokwi simulation does not run | **No** | 60 s headless run completed with exit 0 and the full 216-line baseline; `wokwi-cli lint` reports no issues. |
| 3 | Most functionality is placed in one task | **No** | 8 tasks, one module per subsystem; `main.cpp` contains only startup and the two §17 diagnostics. Verified: every application feature lives in `src/<subsystem>.cpp`. |
| 4 | FreeRTOS objects exist only to satisfy the checklist | **No** | Every object sits on a real data path with a named producer and consumer (README *Inter-Task Communication*, `include/rtos_objects.h`) and each is exercised by a functional test: 3 queues by FT-01..FT-05, the event group by FT-08..FT-10, the mutex by the §51 fault experiment. |
| 5 | Task priorities cannot be justified | **No** | `src/main.cpp:104-215` gives, per task, the budget, the acceptable latency and the concrete failure if the level is too high or too low, plus a per-resource inversion analysis. Commit `79dab45`. |
| 6 | A task contains an uncontrolled busy loop | **No** | Every task loop contains a blocking call: `vTaskDelayUntil` (`sensors.cpp:448`), bounded `xQueueReceive` (`display.cpp:406`), `portMAX_DELAY` `xQueueReceive` (`alarm.cpp:192`), `vTaskDelay` (`input.cpp:297`, `motion.cpp:165`, `main.cpp:273/285`), `xEventGroupWaitBits` with a 15 s timeout (`system_state.cpp:80`). |
| 7 | Shared resources are accessed unsafely | **No** | UART: single `serial_write()` choke point under `serialMutex`. I2C1/OLED: single owner (`DisplayTask`, zero `serial_write` calls in `display.cpp`). `EVENT_MOTION`: exactly one clearer (`StateTask`). Queues: length-1 + `xQueueOverwrite`, so no producer ever blocks. |
| 8 | The student cannot explain the queue, mutex, event group, or task notification | **Cannot be assessed from the repository** | This is the §64 technical-defense gate; it is a live human action, not a document. The material to explain with is committed (`include/rtos_objects.h`, report §3.6, README *Inter-Task Communication*). **Action: rehearse the §64 questions 1–15 before the defense.** |
| 9 | Unit tests are trivial or unrelated to requirements | **No** | 29 tests targeting exactly the §42 recommended functions (`evaluateTemperature`, `nextDisplayMode`/`previousDisplayMode`, `evaluateSystemState`), with §43-required boundary and wraparound cases; they compile the production sources, not copies. |
| 10 | `pio check` output is shown without interpretation | **No** | `docs/static-analysis.md` explains the tool choice, the two environment passes and why each class of finding appears, then classifies all 35 with cause and disposition. |
| 11 | Screenshots are substituted for analysis | **No** | Every claim rests on a serial log line number, a VCD timestamp, a test case name or a command output; the single figure that is a screenshot (`finished-system.png`) is captioned as evidence of the display path, not as analysis. |
| 12 | The README is incomplete or written merely as an academic worksheet | **No** | All 21 §55 headings present in order; engineering voice, no "Question / Answer" framing, no marks-oriented language. |
| 13 | Git history contains only a final bulk commit | **No** | One commit per engineering step, 30 of them covering M0–M16 plus the M17 documentation commits, all with specific messages; §53's banned words (`update`, `changes`, `working`, `final`, `final2`, `finalfinal`) do not appear as commit subjects. |
| 14 | Architecture diagrams disagree with source code | **No** | Checked for this audit — see §3. Priorities, stack depths, periods, event-bit numbering, pins, part and connection counts all match `src/` and `diagram.json`. |
| 15 | Test results cannot be reproduced | **No longer applies — cleared** | The baseline run has always been reproducible from the repository (commands in `docs/functional-verification.md` §5: exit 0, 216 lines). The gap this row flagged — that the **scenario files and the harness diagram** for FT-01..FT-10 lived outside the repository — is closed: all six are committed under `test/wokwi/` with copy-paste commands, timeouts computed from each scenario's `delay:` sums, and the scenario→FT mapping in `test/wokwi/README.md`. Re-verified for this audit: `ft-alarm.yaml` (both `expect-pin` assertions reached, `Scenario completed successfully`, exit 0, 84 lines), `ft-sensors.yaml` (exit 0, 119 lines, 28.00/31.00/25.40 °C, 45.00/61.20 %, 38/76 % all observed) and `ft-io.yaml` (exit 0, 127 lines — the eight `Page:` changes, both `Motion: detected` edges and the `INACTIVE → ACTIVE` transition landed on exactly the same log lines as run B in `docs/functional-verification.md`). **Retained, disclosed limit:** the FT-04/05/08/10 stimuli are button-driven stand-ins and the harness leaves `pir:OUT` unconnected, so those four rows still need the one-time manual confirmation in the Wokwi web UI — that is an element-model caveat, not a reproducibility gap, and it is documented in `test/wokwi/README.md` §5 and `docs/functional-verification.md` §4. |
| 16 | The Hackster.io article makes unsupported technical or performance claims | **No** | `docs/hackster-article.md` claims only measured things (29/29 tests, 0/0/35 findings, 216-line run, 48.2%/55.4% footprint) and explicitly states that CPU utilisation, power and latency were **not** measured and are therefore not claimed. |
| 17 | External material is used without attribution | **No** | Article *References* and README *References and Acknowledgments* attribute the lab specification and instructor, FreeRTOS, STM32Cube HAL/CMSIS, PlatformIO, Wokwi, cppcheck, and the Adafruit GFX 5×7 font with its BSD licence committed at `assets/LICENSE-glcdfont.txt`. |
| 18 | The student cannot explain the submitted implementation during technical checkoff | **Cannot be assessed from the repository** | Same §64 gate as row 8. **Action: rehearse §64 questions 1–15; inability to do so caps the FreeRTOS marks regardless of the simulation.** |

---

## 3. Deliverable cross-checks

| Check | Result |
| --- | --- |
| `docs/laboratory-report.pdf` at exactly the §57 path | **Match.** `docs/laboratory-report.pdf` exists and is tracked (996 407 bytes). |
| README contains all 21 §55 headings | **Match.** All present, in the specified order: Project Overview · Features · Learning Objectives · System Architecture · FreeRTOS Architecture · Hardware / Simulated Components · Pin Configuration · Task Design · Inter-Task Communication · State Machine · Repository Structure · Getting Started · Building the Project · Running the Wokwi Simulation · Unit Testing · Static Code Analysis · Functional Verification · Engineering Decisions · Limitations · Future Improvements · References and Acknowledgments. |
| The 5 §56 visuals exist and are captioned | **Match.** (1) `wokwi-circuit.png` Fig. 1, (2) `architecture.png` Fig. 2, (3) `freertos-tasks.png` Fig. 3, (4) `state-machine.png` Fig. 4, (5) `finished-system.png` Fig. 5 — each with an italic `*Figure N - …*` caption stating the technical point it supports. SVG sources exist for four of the five (`finished-system` is a screenshot, so PNG only). |
| README task table = report §59 table = `src/main.cpp` §38/§39 table | **Match on all three.** Priorities `Input 3 / Motion 3 / State 3 / Sensor 2 / Alarm 2 / Display 1 / TaskA 1 / TaskB 1`; stacks `256/128/192/256/256/256/128/128` words; periods `2 ms / 10 ms / 15 s event / 2000 ms vTaskDelayUntil / portMAX_DELAY / ≤50 ms / 1000 ms / 1000 ms`. The five §38/§59 rows also match the specification's own suggested values exactly. |
| Diagrams vs. source (§66 item 14) | **Match.** `freertos-tasks.svg`: priorities 2/1/3/3/2, `displayQueue`/`alarmQueue`/`modeQueue` length 1 + `xQueueOverwrite`, `EVENT_MOTION (BIT1)`/`EVENT_ACTIVE (BIT0)`/`EVENT_ALARM (BIT2)` — identical to `include/rtos_objects.h:82-84` and `src/rtos_objects.cpp:66-68`. `architecture.svg`: per-task priority **and** stack word counts identical to `src/main.cpp:216-223`. `wokwi-circuit.svg`: pins identical to the README pin table; 8 parts / 24 connections = actual `diagram.json` counts. `state-machine.svg`: two states, 15 s timeout, motion edge, pure `evaluateSystemState()` — matches `src/system_state.cpp`. |
| Unfinished-work markers / placeholder text in committed files | **None.** No unfinished-work markers of any kind anywhere in the tree (the scan command returns nothing). The two deliberate placeholders that used to be listed here — the `git clone` URL in `README.md` (line 336 after this edit) and the account-name URL in `docs/hackster-article.md` — are **gone**, replaced by the real URL `https://github.com/MaryGraceSoronio/bca182-freertos-multisensor`; a grep for those angle-bracket placeholders over every tracked file returns nothing. What remains is only the ordinary English word "placeholder" at `README.md:483` and `src/display.cpp:262`, which are prose, not markers. |
| Wokwi token / secrets in committed files | **Clean.** The CLI token scan over every tracked file (plus the two files added by these commits) returns no match; the token is only ever supplied through the environment, never written to disk. |
| Untracked noise | `.vscode/` only — intentionally left untracked (`.gitignore` keeps the shared `extensions.json`/`settings.json` exceptions but the directory is currently untracked). |

**Mismatches found: none.** The only outstanding items are the one human
action in §1 row 27 (publish on Hackster.io), the optional manual element
check behind §2 row 15 and the §64 defense rehearsal in §2 rows 8/18.

---

## 4. Ordered manual actions still required

1. Create the Hackster.io project, paste the sections of
   `docs/hackster-article.md`, upload the five figures from `docs/images/`.
2. Add **Paul Rodolf P. Castor** (`paulrodolf.castor@g.msuiit.edu.ph`) as a
   collaborator on the Hackster project for BSCA accreditation.
3. Confirm the GitHub link on the published Hackster project points at
   `https://github.com/MaryGraceSoronio/bca182-freertos-multisensor`.
4. *(Optional)* Confirm the encoder knob and PIR *Simulate Motion* by hand in
   the Wokwi web UI and note it in `docs/functional-verification.md` §4 — the
   harness covers these electrically, not through the part models.
5. Rehearse the §64 technical-defense questions.

Already done, listed only for the record: the repository is pushed and Public
(rows 17), the URL placeholders are replaced everywhere (row 28), and the
functional-test harness is committed at `test/wokwi/` (§2 row 15).
