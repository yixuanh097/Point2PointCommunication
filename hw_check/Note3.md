# WIRING — two identical modules, 3 m apart

Both modules are carbon copies. Same parts, same pinout, **same firmware
file** (`module/module.ino`). Whichever one you type a message on becomes the
transmitter for that exchange; the other listens. Nothing is hard-coded as
"the transmitter," so you cannot lose a demo by flashing the wrong sketch to
the wrong box.

Build **module 1 completely and prove it works before you start module 2.**
If there's a design mistake and you build both in parallel, you've made it
twice and you'll debug it twice.

---

## Parts per module

| Qty | Part | Pins |
|-----|------|------|
| 1 | Arduino Uno | — |
| 2 | VS1838B IR detector | D2, D4 |
| 2 | IR LED (940 nm) | both on D3 |
| 1 | Logic-level MOSFET (2N7000 / IRLZ44N) | gate D3 |
| 1 | 20x4 I2C LCD | A4 / A5 |
| 1 | 3x4 keypad | D5 D6 D7 D12 / D13 A0 A1 |
| 1 | 28BYJ-48 stepper + ULN2003 | D8 D9 D10 D11 |
| 2 | 100 Ω (detector supply filter) | — |
| 2 | 10 µF electrolytic (detector supply filter) | — |
| 2 | 100 Ω (LED current limit) | — |
| 1 | 220 Ω (MOSFET gate) | — |
| 1 | 10 kΩ (MOSFET gate pulldown) | — |

## Two design points worth knowing before you wire

**Both IR LEDs share D3, and that isn't a shortcut.** `tone()` generates the
38 kHz carrier on Timer2, and an Uno has exactly one Timer2. Two LEDs
therefore *cannot* be modulated independently. So they aren't — one MOSFET
drives both, and you aim them a few degrees apart to widen the transmit beam.
Two emitters, one carrier, one timer.

**The two detectors are combined by selection, not by OR-ing.** A VS1838B
falls to half sensitivity around ±45°, so splaying two of them roughly ±30°
widens the module's field of view to nearer ±75° — which directly cuts how far
the stepper has to sweep before it finds the other unit. When a frame starts,
the firmware latches whichever detector saw it first and decodes the whole
frame from that one pin. Wiring their outputs together would be simpler but
worse: it would OR their *noise* too, dragging the quiet detector down with
whatever the other one is picking up.

Both detectors sit on PORTD (D2 and D4) so a single port read samples them
together in one instruction, instead of two ~4 µs `digitalRead()` calls inside
a loop that's timing a 1000 µs pulse.

---

## Full pin map

```
                    Arduino Uno
        ┌──────────────────────────────┐
   D0   │ serial RX — LEAVE FREE       │
   D1   │ serial TX — LEAVE FREE       │
   D2   │ VS1838B A  OUT               │
   D3   │ BOTH IR LEDs (via MOSFET)    │
   D4   │ VS1838B B  OUT               │
   D5   │ keypad row 1                 │
   D6   │ keypad row 2                 │
   D7   │ keypad row 3                 │
   D8   │ ULN2003 IN1                  │
   D9   │ ULN2003 IN2                  │
   D10  │ ULN2003 IN3                  │
   D11  │ ULN2003 IN4                  │
   D12  │ keypad row 4                 │
   D13  │ keypad col 1                 │
   A0   │ keypad col 2                 │
   A1   │ keypad col 3                 │
   A2   │ free (supply-filter test tap)│
   A3   │ free                         │
   A4   │ LCD SDA                      │
   A5   │ LCD SCL                      │
        └──────────────────────────────┘
```

Every usable pin is spoken for except A2 and A3. A2 is where the supply-rail
divider goes if you run `filter_proof`.

---

## Build order — verify at every stage

Upload `hw_check/hw_check.ino` to a bare board first. Then add one subsystem,
run its test, and only move on when it passes. Serial Monitor at **115200**.

**Unplug USB before changing wiring.** Hot-wiring a 5 V rail into a signal pin
is the one mistake that ends the day early.

### Stage 0 — power rails (5 min)

Arduino 5V → breadboard red rail. Arduino GND → blue rail. If your board has
rails on both sides, jumper red-to-red and blue-to-blue across the top — half
the "it works when I press on it" faults are an unpowered second rail.

**Verify:** press `8`. Readable text means board and baud rate are right.

### Stage 1 — LCD (5 min)

| LCD backpack | to |
|---|---|
| GND | blue rail |
| VCC | red rail |
| SDA | **A4** |
| SCL | **A5** |

A4/A5 are the Uno's hardware I2C lines and the only pins that work — which is
exactly why the keypad isn't allowed to use them.

**Verify:** press `1` → an address appears, almost always `0x27` or `0x3F`.
Put it in `module.ino` as `LCD_ADDR`. Then press `2` → text on all four rows.

- *Scan finds nothing* → SDA/SCL swapped, or LCD unpowered.
- *Backlight on, no characters* → **contrast**, not address. Turn the blue pot
  on the back of the backpack. This wastes more student-hours than any other
  single fault on this project.

### Stage 2 — both VS1838B detectors (10 min)

Hold each part with the **domed face toward you**, legs down. Pins left to
right are **OUT, GND, VCC**.

| Detector | OUT | GND | VCC |
|---|---|---|---|
| A | **D2** | blue rail | red rail **via 100 Ω** |
| B | **D4** | blue rail | red rail **via 100 Ω** |

Then one **10 µF electrolytic** per detector, from its VCC leg to the blue
rail, **striped leg to ground**. Electrolytics are polarised and will vent if
reversed.

That 100 Ω + 10 µF pair per detector is the supply filter — the thing we're
testing in `SUPPLY_FILTER_TEST.md`. Fit it, then measure whether it matters.

Mount the two detectors splayed roughly ±30° from straight ahead.

**Verify:** press `3`. Both should idle HIGH. Point any TV remote at each in
turn — a remote is a known-good 38 kHz emitter with no connection to your
circuit, so a response proves that detector entirely on its own.

- *Idle reads LOW* → OUT and VCC swapped on that detector.
- *Only one responds* → the other's OUT wire. Worth catching now: because the
  firmware picks whichever detector hears the frame first, a dead second
  detector doesn't break the link, it just silently halves your field of view.

### Stage 3 — both IR LEDs + MOSFET (10 min)

**2N7000** (flat face toward you, legs down: **S, G, D**) or **IRLZ44N**
(tab away from you: **G, D, S**).

| Wire | From | To |
|---|---|---|
| 1 | **D3** | 220 Ω → MOSFET **GATE** |
| 2 | MOSFET **GATE** | 10 kΩ → blue rail |
| 3 | MOSFET **SOURCE** | blue rail |
| 4 | red rail | 100 Ω → LED 1 **anode (long leg)** |
| 5 | LED 1 **cathode** | MOSFET **DRAIN** |
| 6 | red rail | 100 Ω → LED 2 **anode** |
| 7 | LED 2 **cathode** | MOSFET **DRAIN** |

Both LEDs hang off the same drain, each with **its own** 100 Ω. Don't share
one resistor between them — LEDs don't split current evenly, and the brighter
one steals from the dimmer one.

The 10 kΩ gate pulldown matters: while the Arduino resets, D3 floats, and a
floating gate leaves the LEDs in an undefined state.

Aim the two LEDs a few degrees apart to widen the beam.

**Verify:** press `4`, **point a phone camera at the LEDs**. Both should show a
faint purple-white flicker, 1 s on, 1 s off. 940 nm is invisible to your eye,
so this is the only practical check — and a backwards LED, once sealed in an
enclosure, presents exactly like a broken protocol.

**Then press `5`.** This fires the board's own LEDs and watches both its own
detectors. You want 5/5 on both. It proves the entire analogue chain end to end
— MOSFET switching, LEDs emitting, detectors demodulating, pins reading. If
tests 3 and 4 pass individually but 5 fails, the fault is between them, and
it's almost always a ground that never reached the blue rail.

### Stage 4 — 3x4 keypad (5 min)

Seven pins. Viewed from the **back**, keys facing away, left to right: the
first four are **rows**, the last three are **columns**.

| Keypad pin | to |
|---|---|
| 1 (R1) | **D5** |
| 2 (R2) | **D6** |
| 3 (R3) | **D7** |
| 4 (R4) | **D12** |
| 5 (C1) | **D13** |
| 6 (C2) | **A0** |
| 7 (C3) | **A1** |

No power, no ground, no resistors — it's twelve bare switches, and the internal
pull-ups hold the columns HIGH.

**Verify:** press `6`, then press keys.

- *Whole row dead* → that row's wire.
- *Whole column dead* → that column's wire.
- *Pressing `1` reports `3`* → row and column groups swapped.

### Stage 5 — stepper (7 min)

| ULN2003 | to |
|---|---|
| IN1 | **D8** |
| IN2 | **D9** |
| IN3 | **D10** |
| IN4 | **D11** |
| + | red rail (but see below) |
| − | blue rail |

The motor's white plug only fits the ULN2003 socket one way.

**The firmware says `Stepper(2048, 8, 10, 9, 11)` — 8, 10, 9, 11, not
sequential.** That's deliberate. A 28BYJ-48's coils aren't wired in the order
its connector implies, and sequential order gives you a motor that buzzes and
vibrates without rotating. Everyone hits this once.

**Verify:** press `7`. Quarter turn out, pause, quarter turn back.

- *Buzzes, doesn't turn* → pin order.
- *Turns then stalls, or the board resets mid-move* → current. The motor pulls
  ~240 mA. On USB it's usually fine; on battery, give the ULN2003 **its own
  5 V supply** and tie that supply's ground to the Arduino ground. Sharing the
  regulator browns it out mid-sweep, which presents as "it randomly restarts
  during the search."

---

## Final — flash both modules and run loopback

Upload `module/module.ino` to both boards, with `LCD_ADDR` set to whatever
stage 1 found.

Before pointing them at each other across 3 m, run the jumper test. Set
`#define WIRED_LOOPBACK 1` in `module.ino`, flash both, and connect:

```
Module 1  D3  ──────────►  Module 2  D2
Module 2  D3  ──────────►  Module 1  D2
Module 1  GND ───────────  Module 2  GND
```

In loopback the TX pin drives the demodulated envelope directly — active-low,
exactly what a VS1838B outputs — so the decoder needs no changes at all.

Type ten digits on module 1, press `#`. Module 2's LCD should show them and
module 1 should read LINKED.

This is worth the fifteen minutes because it separates *protocol logic* from
*everything optical* with total certainty. If loopback fails, no amount of
re-aiming will help. If loopback passes and 3 m fails, stop touching the code.

Note the jumper only reaches D2, so loopback exercises detector A's pin only.
Detector B is proven separately by `hw_check` tests 3 and 5.

Then set `WIRED_LOOPBACK 0`, re-flash both, and go to 3 m.

---

## Which files are current

| File | Status |
|---|---|
| `module/module.ino` | **the firmware** — flash to both |
| `hw_check/hw_check.ino` | wiring verification |
| `filter_proof/filter_proof.ino` | supply filter experiment |
| `tx_level/`, `rx_level/` | keep — level-encoded comparison data for the report |
| `tx_pulse/`, `rx_pulse/`, `injector/`, `ir_capture/` | superseded by `module.ino`; ignore |

## Fast diagnosis

| Symptom | Almost always |
|---|---|
| LCD backlight on, no characters | Contrast — the blue pot |
| LCD dark | Wrong address; try `0x3F` |
| Nothing decodes ever | An IR LED in backwards. Phone camera |
| Works on USB, dead on battery | Missing common ground, or stepper browning out the regulator |
| Stepper buzzes, doesn't turn | Pin order — 8, 10, 9, 11 |
| Random rejects at close range | Supply filter — run `filter_proof` |
| Serial prints garbage | Monitor on 9600; these sketches use **115200** |
| One reject right after every good frame | Module seeing its own ACK; lengthen the blank |
| Link works but field of view feels narrow | Detector B not actually wired. `hw_check` test 5 |