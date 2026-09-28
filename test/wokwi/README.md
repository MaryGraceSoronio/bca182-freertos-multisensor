# Wokwi functional-test harness — sections 47 / 48

These six files make the ten required functional tests (FT-01 … FT-10,
spec §47) re-runnable by anyone with the repository, a PlatformIO install and
their own Wokwi CLI token. The observed results they produced are recorded in
`docs/functional-verification.md`; this file only explains *what the files are*
and *how to run them again*.

Nothing here is required to build or run the project — the firmware, the unit
tests and the canonical simulation all work without it.

---

## 1. What each file is

| File | Purpose |
| --- | --- |
| `diagram-harness.json` | The production `diagram.json` (same 8 parts, same 24 firmware wires) **plus** three `wokwi-pushbutton` parts and one `wokwi-logic-analyzer`. Used only through `--diagram-file`, so the canonical diagram is never touched. |
| `ft-sensors.yaml` | FT-01 / FT-02 / FT-03 in one run: steps the DHT22 temperature 25.4 → 28.0 → 31.0 → 25.4 °C, the humidity 61.2 → 45.0 → 61.2 %, and the LDR 500 → 50 → 500 lux. |
| `ft-alarm.yaml` | FT-06 / FT-07 with machine-checkable assertions: raises the temperature to 31.0 °C, then `expect-pin buzzer:2 == 1`, restores 25.4 °C, then `expect-pin buzzer:2 == 0`. |
| `ft-display-hum.yaml` | FT-02 again, but observed on the OLED *Humidity* page: one clockwise detent selects the page, then the humidity changes. Needs a screenshot (see §5). |
| `ft-display-lt.yaml` | FT-03 again, observed on the OLED *Light* page: two clockwise detents, then the lux value changes. Needs a screenshot. |
| `ft-io.yaml` | FT-04 / FT-05 (encoder: 4 clockwise then 4 counter-clockwise detents, with wrap at both ends) and FT-08 / FT-09 / FT-10 (PIR: a 1.2 s pulse while ACTIVE, a 15 s inactivity timeout, a 6 s pulse while INACTIVE). |

The scenarios use only three step kinds — `delay`, `set-control` and
`expect-pin`. `wait-serial` is deliberately **not** used: it inserts a pause in
the runner that strips the CRLF after the matched bytes and corrupts the serial
log (see `docs/functional-verification.md` §1). Observation is therefore always
done on the complete `--serial-log-file` **after** the run.

---

## 2. Harness wiring (`diagram-harness.json`)

Wokwi exposes **no automation control** for `wokwi-ky-040` or
`wokwi-pir-motion-sensor`, so the harness stimulates those two inputs
electrically, with pushbuttons:

| Part | Pin 1 | Pin 2 | Effect when pressed |
| --- | --- | --- | --- |
| `btn_cw` | `stm32:B12` (encoder CLK) | `stm32:GND.1` | pulls CLK low — one clockwise detent |
| `btn_ccw` | `stm32:B13` (encoder DT) | `stm32:GND.1` | pulls DT low — one counter-clockwise detent |
| `btn_pir` | `stm32:3V3.1` | `stm32:B8` | drives the PIR input pin high |

**Honest caveat about the PIR:** in this harness diagram the
`wokwi-pir-motion-sensor` part keeps **only** its `VCC` and `GND` wires — its
`OUT` pin is **not connected**. The `B8` net is driven by `btn_pir` instead, so
in a harness run it is the button, not the PIR part, that produces the motion
edge. The firmware cannot tell the difference (it sees the same rising edge on
the same GPIO), but the *element model* of the sensor is not exercised — see §5.

The `la` logic analyser taps `D0 = BUZZER (PB0)`, `D1 = ENC_CLK (PB12)`,
`D2 = ENC_DT (PB13)`, `D3 = PIR_STIM (PB8)`; `channelNames` labels those four
channels in the VCD, which is where the waveform evidence in
`docs/functional-verification.md` §3.2/§3.3 came from.

**The production `diagram.json` is unchanged by all of this.** It still contains
`["pir:OUT", "stm32:B8", ...]` and none of the buttons or the analyser. The
harness wiring is selected per run by `--diagram-file`, which only overrides
the diagram for that one invocation.

---

## 3. Running it

**Prerequisites**

* PlatformIO Core (`pio`)
* `wokwi-cli` — https://github.com/wokwi/wokwi-cli/releases
* A Wokwi CLI token from **your own** Wokwi account
  (https://wokwi.com → your account → CLI token). Put it in the environment for
  the session — never write it into a file or a commit:

```powershell
$env:WOKWI_CLI_TOKEN = "<token from your Wokwi account>"     # your token, not a committed value
```

**Build first** — `wokwi.toml` points at `.pio/build/bluepill_f103c8/firmware.bin`,
so a scenario run against a stale build tests stale firmware:

```powershell
pio run        # expected: Flash 48.2% (31588/65536), RAM 55.4% (11348/20480)
```

**Then run a scenario from the repository root.** The `--timeout` below is the
hard cap for the whole run; each is computed from the sum of that file's
`delay:` values plus margin (a run ends as soon as its scenario finishes):

| Scenario | Sum of `delay:` | `--timeout` |
| --- | --- | --- |
| `ft-sensors.yaml` | 5500 + 6000 + 6000 + 14000 = **31 500 ms** | 60000 |
| `ft-alarm.yaml` | 5500 + 6000 + 6000 + 4000 = **21 500 ms** | 45000 |
| `ft-display-hum.yaml` | 5500 + 500 + 400 + 5000 + 3000 = **14 400 ms** | 45000 |
| `ft-display-lt.yaml` | 5500 + 500 + 400 + 400 + 400 + 4600 + 3000 = **14 800 ms** | 45000 |
| `ft-io.yaml` | 30 `delay:` steps (2000 + 1200 + 800 + 7×400 + 800 + 16×400 + 8000 + 6000 + 3000) = **31 000 ms** | 60000 |

```powershell
# FT-06 / FT-07  (expect-pin on the buzzer — has its own pass/fail signal)
$env:WOKWI_CLI_TOKEN = "<token from your Wokwi account>"
pio run
& "C:\path\to\wokwi-cli.exe" . `
    --diagram-file test/wokwi/diagram-harness.json `
    --scenario test/wokwi/ft-alarm.yaml `
    --timeout 45000 --timeout-exit-code 0 `
    --serial-log-file "$env:TEMP\ft-alarm.log"
$LASTEXITCODE
```

```powershell
# FT-01 / FT-02 / FT-03  (no expect-* steps — read the serial log afterwards)
& "C:\path\to\wokwi-cli.exe" . `
    --diagram-file test/wokwi/diagram-harness.json `
    --scenario test/wokwi/ft-sensors.yaml `
    --timeout 60000 --timeout-exit-code 0 `
    --serial-log-file "$env:TEMP\ft-sensors.log"
```

```powershell
# FT-04 / FT-05 / FT-08 / FT-09 / FT-10
& "C:\path\to\wokwi-cli.exe" . `
    --diagram-file test/wokwi/diagram-harness.json `
    --scenario test/wokwi/ft-io.yaml `
    --timeout 60000 --timeout-exit-code 0 `
    --serial-log-file "$env:TEMP\ft-io.log" `
    --vcd-file "$env:TEMP\ft-io.vcd"
```

```powershell
# FT-02 / FT-03 on the OLED (add a screenshot to see the page content)
& "C:\path\to\wokwi-cli.exe" . `
    --diagram-file test/wokwi/diagram-harness.json `
    --scenario test/wokwi/ft-display-hum.yaml `
    --timeout 45000 --timeout-exit-code 0 `
    --serial-log-file "$env:TEMP\disp-hum.log" `
    --screenshot-file "$env:TEMP\disp-hum.png" `
    --screenshot-part oled --screenshot-time 9000
```

`ft-display-lt.yaml` is the same command with `ft-display-lt.yaml`,
`disp-lt.log` and `disp-lt.png`.

Notes:

* `--timeout-exit-code 0` makes a timeout non-fatal, so an exit code of 0 means
  *the run finished within the window*, not necessarily that the scenario
  passed. The real pass signal is printed on stdout —
  `[<scenario-name>] Scenario completed successfully` — or, for
  `ft-alarm.yaml`, the two `expect-pin` steps: if either assertion fails the
  runner aborts and that final line never appears.
* Scenarios **without** `expect-*` steps (everything except `ft-alarm.yaml`)
  have no built-in pass/fail: you must assert the serial content yourself, e.g.
  `Select-String -Path "$env:TEMP\ft-sensors.log" -Pattern "Temperature|Humidity|Light level"`.
* Read the log file itself rather than the CLI's console output — see §1.
* A transient `API Error ... code 1006` with an empty log is a Wokwi-side flake;
  re-run once or twice before believing it.

---

## 4. Which scenario proves which FT row

Cross-reference: `docs/functional-verification.md` §2 carries the full
section-48 record (stimulus, expected, observed line numbers, PASS/FAIL).

| FT | §47 requirement | Scenario | What to look for |
| --- | --- | --- | --- |
| FT-01 | Change temperature → displayed temperature updates | `ft-sensors.yaml` | Serial `Temperature: 25.40 C` → `28.00` → `31.00` → `25.40` |
| FT-02 | Change humidity → displayed humidity updates | `ft-sensors.yaml` (serial) / `ft-display-hum.yaml` (OLED) | `Humidity: 61.20 %` → `45.00 %` → `61.20 %`; OLED shot of the Humidity page |
| FT-03 | Change light input → light value changes | `ft-sensors.yaml` (serial) / `ft-display-lt.yaml` (OLED) | `Light level: 76 %` → `38 %` → `76 %`; OLED shot of the Light page |
| FT-04 | Rotate encoder CW → next page | `ft-io.yaml` | `Page: Temperature → Humidity → Light → Motion → Temperature` (wrap) |
| FT-05 | Rotate encoder CCW → previous page | `ft-io.yaml` | `Page: Motion → Light → Humidity → Temperature` (wrap) |
| FT-06 | > 30 °C → alarm activates | `ft-alarm.yaml` | `expect-pin buzzer:2 == 1` and `Alarm: HIGH_TEMPERATURE` |
| FT-07 | Back to normal → alarm stops | `ft-alarm.yaml` | `expect-pin buzzer:2 == 0` and `Alarm: NORMAL` |
| FT-08 | Trigger PIR → system is ACTIVE | `ft-io.yaml` | `Motion: detected` with **no** following `State:` line; page turns still work |
| FT-09 | Inactivity → system becomes INACTIVE | `ft-io.yaml` (also any 60 s baseline run) | `State: ACTIVE` → `State: INACTIVE` after 15 s |
| FT-10 | PIR while INACTIVE → returns to ACTIVE | `ft-io.yaml` | `Motion: detected` immediately followed by `State: ACTIVE` |

`ft-io.yaml` covers FT-04, FT-05, FT-08, FT-09 and FT-10 in a single run — its
first PIR pulse arrives while the system is ACTIVE (FT-08), the run then idles
past the 15 s timeout (FT-09), and its second, longer pulse arrives while
INACTIVE (FT-10).

---

## 5. What is automated, and what still needs a human in the web UI

Automated end-to-end by these files: FT-01, FT-02, FT-03, FT-06, FT-07
(sensor values, alarm threshold, buzzer level), plus FT-09 (the 15 s
state-machine timeout, which needs no stimulus at all).

Still **manual**:

1. **FT-04 / FT-05 — the knob.** The detents are synthesised by pulling PB12 /
   PB13 low with `btn_cw` / `btn_ccw`, which produces the identical edge
   sequence a turn produces (debounce → edge decode → `modeQueue` → display).
   The firmware path is proven; the `wokwi-ky-040` element model is not, because
   Wokwi offers no automation control for it. **Do it once by hand:** turn the
   actual knob in each direction in the Wokwi web UI and confirm the page still
   steps.
2. **FT-08 / FT-10 — the PIR element.** Same reason: `btn_pir` drives PB8 high
   and the harness leaves `pir:OUT` unconnected. **Do it by hand:** click
   *Simulate Motion* on the PIR part once while ACTIVE and once while INACTIVE.
3. **FT-02 / FT-03 on the OLED.** The serial line is the primary record; the
   OLED screenshots from `ft-display-hum.yaml` / `ft-display-lt.yaml` cover the
   display path directly and must be captured with `--screenshot-*`.

These limits are stated in-row in `docs/functional-verification.md` §2 and
expanded in its §4.

---

## 6. Known tool quirk

`wokwi-cli lint .` on the project is clean. When the CLI also lints
`diagram-harness.json` (it does this automatically on a run that passes
`--diagram-file`) it prints:

```text
⚠ [invalid-attribute] Unknown attribute "channelNames" for part "la"
```

That warning is a **false positive** in the CLI's attribute table:
`channelNames` is a documented attribute of `wokwi-logic-analyzer`
(https://docs.wokwi.com/parts/wokwi-logic-analyzer) and it demonstrably works —
the VCD written by these runs declares `$var ... BUZZER / ENC_CLK / ENC_DT /
PIR_STIM`. It is a warning, not an error: the run exits 0 and the scenario still
executes. Leave the attribute in; removing it would silently rename the four
channels back to `D0..D3` in the evidence waveforms.
