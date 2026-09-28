# Functional verification in Wokwi — sections 47–48

**Firmware under test:** `pio run`, `bluepill_f103c8`
**Runner:** `wokwi-cli` v0.27.1, headless, `--serial-log-file`
**Reference run:** 60 s with no stimulus (the Part-3 baseline), reproduced at the
end of this milestone and unchanged

```console
$ pio run
RAM:   [======    ]  55.4% (used 11348 bytes from 20480 bytes)
Flash: [=====     ]  48.2% (used 31588 bytes from 65536 bytes)
```

---

## 1. How the runs were produced

Every row of section 48 was measured on an unmodified build. The stimulus
files and the harness diagram live **outside the repository**; `diagram.json`
in the working tree was never edited (`git status` shows only `.gitignore`).

| Run | Diagram | Scenario | Serial log | Lines |
| --- | --- | --- | --- | --- |
| A | `diagram.json` (canonical) | `ft-sensors.yaml` | `ft-a.log` | 119 |
| B | harness | `ft-io.yaml` | `ft-b.log` | 127 |
| A2 | `diagram.json` (canonical) | `ft-alarm.yaml` | `ft-a2.log` | 84 |
| A3 | harness + logic analyser | `ft-alarm.yaml` | `ft-a3.log` | 84 |
| D1 | harness | `ft-display-hum.yaml` | `disp-hum.log` | OLED shot |
| D2 | harness | `ft-display-lt.yaml` | `disp-lt.log` | OLED shot |

The scenario files contain only three step kinds: `delay`, `set-control` and
`expect-pin`. `wait-serial` was deliberately **not** used for observation: it
creates a pause point in the runner, and the pause strips the CRLF that
follows the matched bytes, which corrupts the serial log (lines merge, e.g.
`Page: TemperatureTemperature: 25.40 C`). Observation is therefore always
performed *after* the run, on the complete log file.

### 1.1 What Wokwi will automate, and what it will not

| Element | `set-control` name(s) tried | Result |
| --- | --- | --- |
| `wokwi-dht22` | `temperature`, `humidity` | **works** |
| `wokwi-photoresistor` | `lux` | **works** |
| `wokwi-pushbutton` | `pressed` (1 / 0) | **works** |
| `wokwi-ky-040` | `rotate`, `rotation`, `step`, `clockwise`, `angle` | **not supported** — silently ignored |
| `wokwi-pir-motion-sensor` | `motion`, `simulate-motion`, `trigger` | **not supported** — silently ignored |

The encoder and the PIR therefore had to be driven electrically. A temporary
`diagram-harness.json` was written as the canonical diagram plus:

* `btn_cw` — pushbutton, `1.l → stm32:B12`, `2.l → GND`
* `btn_ccw` — pushbutton, `1.l → stm32:B13`, `2.l → GND`
* `btn_pir` — pushbutton, `1.l → 3V3.1`, `2.l → stm32:B8`, with the normal
  `pir:OUT → stm32:B8` wire removed so the button owns that net
* `la` — a `wokwi-logic-analyzer` on `BUZZER(PB0)`, `ENC_CLK(PB12)`,
  `ENC_DT(PB13)`, `PIR_STIM(PB8)`

The button signals are the *same electrical event* the real parts produce, so
they exercise the whole firmware path (GPIO → debounce → edge decode → event
group → queue → output). What they do **not** exercise is the Wokwi element
model of the knob and the PIR, so rows FT-04, FT-05, FT-08 and FT-10 carry an
explicit note that the physical interaction should also be performed by hand
in the Wokwi web UI.

**A detent** is a 400 ms low pulse on CLK with DT idle high (clockwise), or
with DT held low first (counter-clockwise) — `src/input.cpp:292` takes the
direction from the DT level at the CLK falling edge, which is exactly what the
quadrature encoder presents.

---

## 2. Verification record (section 48)

| Test ID | Input / Stimulus | Expected | Actual (observed) | Result |
| --- | --- | --- | --- | --- |
| FT-01 | `set-control dht22 temperature`: 25.4 → **28.0** (t=5.5 s) → **31.0** (t=11.5 s) → **25.4** (t=17.5 s) | Displayed temperature updates | Run A: `Temperature: 25.40 C` (l.5) → `28.00` (l.27) → `31.00` (l.48) → `25.40` (l.71). OLED run A3 at t=9 s shows `Temperature / 31.00 C ALARM` | **PASS** |
| FT-02 | `set-control dht22 humidity`: 61.2 → **45.0** → **61.2** | Displayed humidity updates | Run A: `Humidity: 61.20 %` (l.6) → `45.00 %` (l.28) → `61.20 %` (l.72). OLED run D1 at t=9 s shows `Humidity / 45.00 %` | **PASS** |
| FT-03 | `set-control ldr lux`: 500 → **50** → **500** | Light value changes | Run A: `Light level: 76 %` (l.7) → `38 %` (l.29) → `76 %` (l.73). OLED run D2 at t=9 s shows `Light / 38 %` | **PASS** |
| FT-04 | 4 clockwise detents on PB12/PB13, t=4.0 / 4.8 / 5.6 / 6.4 s | Next page is selected | Run B: `Page: Humidity` (l.21) → `Page: Light` (l.27) → `Page: Motion` (l.29) → `Page: Temperature` (l.35, wrap). VCD: ENC_CLK edges from 4.000 s. OLED at t=5 s shows the `Humidity` page | **PASS** (synthetic detents — see §4) |
| FT-05 | 4 counter-clockwise detents, t=8.0 / 9.6 / 11.2 / 12.8 s (DT held low, CLK pulsed) | Previous page is selected | Run B: `Page: Motion` (l.39) → `Page: Light` (l.46) → `Page: Humidity` (l.53) → `Page: Temperature` (l.61, wrap). VCD: ENC_DT edges from 7.600 s | **PASS** (synthetic detents — see §4) |
| FT-06 | `set-control dht22 temperature 31.0` (> 30 °C threshold) | Alarm activates | Run A: `Temperature: 31.00 C` (l.48) then `Alarm: HIGH_TEMPERATURE` (l.51). Run A2/A3: `expect-pin buzzer:2 == 1` passed; VCD `vcd-alarm.vcd` shows PB0 rise at **6.252 s**; OLED shows `31.00 C ALARM` | **PASS** |
| FT-07 | `set-control dht22 temperature 25.4` (back to normal) | Alarm stops | Run A: `Temperature: 25.40 C` (l.71) then `Alarm: NORMAL` (l.74). Run A3: `expect-pin buzzer:2 == 0` passed; VCD shows PB0 fall at **12.252 s** (high exactly 6.000 s) | **PASS** |
| FT-08 | PB8 (PIR output net) driven high t=2.0 → 3.2 s while the system is ACTIVE | System is ACTIVE | Run B: `Motion: detected` (l.13); no `State:` line follows — the system stays ACTIVE, and the four page turns at l.21–35 immediately afterwards still take effect, which proves `EVENT_ACTIVE` is set (`src/input.cpp:290` gates on it). VCD: `PIR_STIM` high for 1.200 s | **PASS** (synthetic PIR edge — see §4) |
| FT-09 | No input for the 15 s inactivity timeout (`SYSTEM_INACTIVE_TIMEOUT_MS`) | System becomes INACTIVE | Run A: `State: ACTIVE` (l.3) → `State: INACTIVE` (l.61). Run B: l.3 → l.78. Run A3: l.3 → l.62 | **PASS** |
| FT-10 | PB8 driven high t=22.0 → 28.0 s while the system is INACTIVE | System returns to ACTIVE | Run B: `Motion: detected` (l.93) immediately followed by `State: ACTIVE` (l.94); the following sensor burst (l.95–97) confirms sampling resumed. VCD: second `PIR_STIM` pulse, 6.000 s high | **PASS** (synthetic PIR edge — see §4) |

All ten rows were observed on real output; none is inferred from source
reading.

---

## 3. Evidence detail

### 3.1 Sensor path (runs A and D1/D2)

```
 5: Temperature: 25.40 C        ← 25.4 °C
 6: Humidity: 61.20 %
 7: Light level: 76 %           ← lux 500
27: Temperature: 28.00 C        ← 28.0 °C
28: Humidity: 45.00 %           ← 45.0 %
29: Light level: 38 %           ← lux 50
48: Temperature: 31.00 C        ← 31.0 °C
71: Temperature: 25.40 C        ← restored
72: Humidity: 61.20 %
73: Light level: 76 %
```

Run A produced 3 × `28.00 C`, 3 × `31.00 C`, 10 × `25.40 C`,
6 × `45.00 %`, 10 × `61.20 %`, 6 × `38 %`, 10 × `76 %` — the changed values
appear exactly for the window the scenario held them (28.0 °C for 5.5–11.5 s
= 3 samples; 45.0 % / lux 50 for 5.5–17.5 s = 6 samples).

`ldr_to_light_percent()` maps the lux input non-linearly
(`src/sensors.cpp:215`), which is why 500 → 76 % and 50 → 38 % rather than a
10:1 ratio.

### 3.2 Encoder path (run B)

```
 4: Page: Temperature     (boot)
21: Page: Humidity        CW 1
27: Page: Light           CW 2
29: Page: Motion          CW 3
35: Page: Temperature     CW 4 — wrap (FT-04)
39: Page: Motion          CCW 1
46: Page: Light           CCW 2
53: Page: Humidity        CCW 3
61: Page: Temperature     CCW 4 — wrap (FT-05)
```

Eight detents produced exactly eight page changes, in both directions, with
the wrap handled at both ends of the four-page list — no lost detents and no
invented detents. The run contained 9 `Page:` lines in total (8 + the boot
line), `maxLineLen` 27, 0 empty lines and 0 consecutive identical pairs.

VCD (`vcd-io.vcd`, logic analyser): `ENC_CLK` first edge at **4.000 s**, last
at 13.202 s; `ENC_DT` first edge at **7.600 s**, last at 13.602 s — matching
the scenario schedule (first CW press at 4.0 s, DT first pulled low at 7.6 s).

### 3.3 Alarm path (runs A2 / A3)

```
48: Temperature: 31.00 C
49: Humidity: 61.20 %
50: Light level: 76 %
51: Alarm: HIGH_TEMPERATURE      ← FT-06
...
71: Temperature: 25.40 C
72: Humidity: 61.20 %
73: Light level: 76 %
74: Alarm: NORMAL                ← FT-07
```

The logic analyser gives the same two events as a waveform:

| Signal `BUZZER` (PB0) | Run A3 (baseline) |
| --- | --- |
| rise 0 → 1 | **6.252 s** |
| fall 1 → 0 | **12.252 s** |
| total high time | **6.000 s** |
| transitions in the whole run | 3 (initial 0, rise, fall) |

Two transitions in a 21.5 s run is also the §49-style proof that the alarm
output is a static DC level, not PWM.

Scenario assertions (run A2 and A3):

```
[ft-alarm-buzzer] expect-pin buzzer:2 == 1     ← passed
[ft-alarm-buzzer] expect-pin buzzer:2 == 0     ← passed
[ft-alarm-buzzer] Scenario completed successfully
```

### 3.4 State-machine path (runs A and B)

```
 3: State: ACTIVE      (boot)
61: State: INACTIVE    (run A, after the 15 s timeout)      ← FT-09
93: Motion: detected
94: State: ACTIVE      (run B, PIR while INACTIVE)          ← FT-10
```

FT-08 and FT-10 are two halves of the same edge: the first PIR pulse arrives
while `EVENT_ACTIVE` is already set (no state line, but `Motion: detected` is
reported and the gated encoder keeps working), the second arrives while it is
clear and produces the `INACTIVE → ACTIVE` transition.

---

## 4. Honest limits of this record

1. **FT-04 / FT-05 — the knob itself.** Wokwi exposes no automation control
   for `wokwi-ky-040`. The detents above were synthesised by driving PB12 and
   PB13 with momentary buttons, producing the identical edge sequence a turn
   would produce. The firmware path is proven; the *element* is not.
   **Manual follow-up:** turn the actual knob in the Wokwi web UI once in
   each direction and confirm the page still steps.
2. **FT-08 / FT-10 — the PIR element.** Same situation: no automation control
   for `wokwi-pir-motion-sensor`, so PB8 was driven to 3V3 by a button.
   **Manual follow-up:** click *Simulate Motion* on the PIR part once while
   ACTIVE and once while INACTIVE.
3. **FT-01…FT-03 observation point.** The primary record is the serial line;
   the OLED is fed from the same `SensorData` struct through `displayQueue`
   (`src/sensors.cpp:445`), and three screenshots were taken to cover the
   display path directly: `31.00 C ALARM` (temperature), `45.00 %`
   (humidity), `38 %` (light).
4. **DHT22 quantisation.** Run B contained 2 × `Temperature: 25.39 C`
   (l.79, l.86) alongside 14 × `25.40 C` — the simulator's DHT22 model
   occasionally returns a value 0.01 °C below the set point. No FT row depends
   on that resolution, and the canonical baseline run shows 30 × `25.40 C`
   with no such sample.

---

## 5. Reproducing this record

```console
$ pio run                                    # 48.2% flash / 55.4% RAM
$ wokwi-cli . --timeout 60000 --timeout-exit-code 0 \
      --serial-log-file <run>.log            # baseline: 216 lines
$ wokwi-cli . --scenario <scenario>.yaml --diagram-file <diagram>.json \
      --serial-log-file <run>.log --vcd-file <run>.vcd \
      --screenshot-file shot.png --screenshot-part oled --screenshot-time 9000
```

Expected counts for the 60 s reference run:

| | value |
| --- | --- |
| total lines | 216 |
| `Task A running` / `Task B running` | 60 / 60 |
| `Temperature:` / `Humidity:` / `Light level:` | 30 / 30 / 30 |
| `Page:` / `Alarm:` | 1 / 1 |
| `State: ACTIVE` / `State: INACTIVE` | 1 / 1 |
| `ERR`, `stack overflow`, `assert` | 0 / 0 / 0 |
| longest line / empty lines | 27 / 0 |
