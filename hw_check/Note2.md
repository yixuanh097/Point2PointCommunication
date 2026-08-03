# WIRING — step by step, verify as you go
 
 Each stage adds at most seven wires, so a failure has few places to hide.
 
Serial Monitor at **115200** throughout.

---
 
## What goes on which board
 
| Board | Stages to build | Time |
|-------|---------------|------|
| **A — Transmitter** | 0 → 1 → 2 → 3 → 4 → 5 |
| **B — Receiver** | 0 → 1 → 2 → 3 |
| **C — Monitor** | 0 → 2 |      |
| **D — Injector** | 0 → 3 |     |
 
Build **board B first**, not A. It is the simpler of the two real units, and
finishing one working board early tells you your parts are good before you sink
an hour into the complicated one.
 
---
 
## Stage 0 — power rails (5 min, every board)
 
1. Arduino **5V** → breadboard **red (+) rail**
2. Arduino **GND** → breadboard **blue (−) rail**
3. If your breadboard has rails on both sides, jumper red-to-red and blue-to-blue
   across the top. Half the "it works when I press on it" faults are an unpowered
   second rail.
**Verify:** upload `hw_check`, open Serial Monitor at 115200, press `8` + Enter.
Readable text means the board and your baud rate are right.
 
---
 
## Stage 1 — 20x4 LCD (5 min · boards A, B)
 
Four wires from the I2C backpack soldered to the back of the display:
 
| LCD backpack | goes to |
|--------------|---------|
| GND | blue (−) rail |
| VCC | red (+) rail |
| SDA | **A4** |
| SCL | **A5** |
 
A4 and A5 are the only pins that work — they are the Uno's hardware I2C lines.
This is exactly why the keypad columns are not allowed to use them.
 
**Verify:** press `1` → an address appears, almost always `0x27` or `0x3F`.
Write it down; it goes in the firmware. Then press `2` → text on all four rows.
 
- *Nothing found by the scan* → SDA/SCL swapped, or the LCD has no power.
- *Backlight on, no characters* → **contrast**, not address. Turn the small blue
  potentiometer on the back of the backpack until text appears. This one wastes
  more student-hours than any other single fault on this project.
---
 
## Stage 2 — VS1838B detector (5 min · boards A, B, C)
 
Hold the part with the **domed face toward you**, legs down. Pins left to right
are **OUT, GND, VCC**.
 
| VS1838B | goes to |
|---------|---------|
| OUT | **D2** |
| GND | blue (−) rail |
| VCC | red (+) rail, **through a 100 Ω resistor** |
 
Then one **10 µF electrolytic** from the VS1838B's VCC leg to the blue rail —
**striped leg to ground**. Electrolytics are polarised and will vent if reversed.
 
That 100 Ω + 10 µF pair is the supply filter the datasheet asks for, and it is
not optional in this build. Your own IR LED and a stepper motor are switching
inches away; without the filter their noise reaches the detector and you get
random rejected frames that look identical to a range problem. Two cheap parts,
several hours of misdiagnosis avoided.
 
**Verify:** press `3`. Idle should read HIGH. Point **any TV or air-conditioner
remote** at it and press buttons — the edge counter should jump.
 
A remote is the ideal test source: it is a known-good 38 kHz emitter with no
connection to your circuit, so a response proves the detector and its wiring
entirely on their own.
 
- *Idle reads LOW* → OUT and VCC are swapped. Fix before powering again.
- *No response to a remote* → check the 100 Ω isn't open, and that GND reaches
  the blue rail.
---
 
## Stage 3 — IR LED + MOSFET (8 min · boards A, B, D)
 
Using a **2N7000** (flat face toward you, legs down: **S, G, D** left to right)
or an **IRLZ44N** (flat metal tab away from you: **G, D, S**).
 
| Wire | From | To |
|------|------|-----|
| 1 | **D3** | 220 Ω resistor → MOSFET **GATE** |
| 2 | MOSFET **GATE** | 10 kΩ resistor → blue (−) rail |
| 3 | MOSFET **SOURCE** | blue (−) rail |
| 4 | red (+) rail | 100 Ω resistor → IR LED **anode (long leg)** |
| 5 | IR LED **cathode (short leg, flat edge)** | MOSFET **DRAIN** |
 
The 10 kΩ gate pulldown matters: while the Arduino resets, D3 floats, and a
floating gate leaves the LED in an undefined state. The pulldown holds it off.
 
**Verify:** press `4`, then **point a phone camera at the LED**. You should see a
faint purple-white flicker on the phone screen, 1 s on, 1 s off.
 
940 nm is invisible to your eye, so the camera is the only practical check — and
a backwards LED, once sealed inside an enclosure, presents exactly like a broken
protocol. Thirty seconds here, versus disassembling a box later.
 
**Then press `5`** (boards A and B, which have both parts). This fires the
board's own LED and watches its own detector. All five pulses should PASS.
 
This proves the entire analogue chain end to end — MOSFET switching, LED
emitting, detector demodulating, pin reading. If tests 3 and 4 pass individually
but 5 fails, the fault is between them, and it is almost always a ground that
never made it to the blue rail.
 
---
 
## Stage 4 — 3x4 keypad (5 min · board A only)
 
Seven pins. Viewed from the **back**, keys facing away, left to right: the first
four are **rows**, the last three are **columns**.
 
| Keypad pin | goes to |
|------------|---------|
| 1 (R1) | **D4** |
| 2 (R2) | **D5** |
| 3 (R3) | **D6** |
| 4 (R4) | **D7** |
| 5 (C1) | **D12** |
| 6 (C2) | **D13** |
| 7 (C3) | **A0** |
 
No power, no ground, no resistors. A matrix keypad is twelve bare switches; the
sketch drives one row LOW at a time and the internal pull-ups hold the columns
HIGH.
 
**Verify:** press `6`, then press keys. Each should print its own character.
 
- *A whole row dead* → that row's wire.
- *A whole column dead* → that column's wire.
- *Pressing `1` reports `3`* → row and column groups swapped. The first four
  pins are rows.
**Do not use D0, D1, A4 or A5 for the keypad.** D0/D1 are the serial port the
sketch prints to; A4/A5 are the LCD's I2C lines. The repo's transmitter used D0
and both I2C pins, which is why its keypad and its display fought each other.
 
---
 
## Stage 5 — 28BYJ-48 stepper + ULN2003 (7 min · board A only)
 
| ULN2003 | goes to |
|---------|---------|
| IN1 | **D8** |
| IN2 | **D9** |
| IN3 | **D10** |
| IN4 | **D11** |
| + (5V) | red (+) rail — see the power note below |
| − (GND) | blue (−) rail |
 
The motor's white 5-pin plug goes into the socket on the ULN2003 board. It only
fits one way.
 
**Note the firmware says `Stepper(2048, 8, 10, 9, 11)` — 8, 10, 9, 11, not
sequential.** That is deliberate. The coils inside a 28BYJ-48 are not wired in
the order the connector implies, and sequential order produces a motor that
buzzes and vibrates without rotating. Everyone hits this once.
 
**Verify:** press `7`. The shaft should turn a quarter turn one way, pause, and
return.
 
- *Buzzes, doesn't turn* → pin order, as above.
- *Turns then stalls, or the Arduino resets mid-move* → current. The motor pulls
  ~240 mA. On USB it is usually fine; on battery, give the ULN2003 its **own 5 V
  supply** and tie that supply's ground to the Arduino ground. Sharing the
  Arduino's regulator browns it out mid-sweep, which presents as "it randomly
  restarts during the search."
---
 
## Final step — load the real firmware
 
| Board | Sketch | Edit before uploading |
|-------|--------|-----------------------|
| A | `tx_pulse` | LCD address if test 1 said `0x3F` |
| B | `rx_pulse` | LCD address if test 1 said `0x3F` |
| C | `ir_capture` | none |
| D | `injector` | none |
 
Then set `#define WIRED_LOOPBACK 1` in tx_pulse **and** rx_pulse, run three
jumpers between A and B (`A D3→B D2`, `B D3→A D2`, `A GND—B GND`), and confirm a
decode before you go anywhere near optics.
 
---
 
## Full pin map, both units
 
```
                    Arduino Uno
        ┌──────────────────────────────┐
   D0   │ serial RX — LEAVE FREE       │
   D1   │ serial TX — LEAVE FREE       │
   D2   │ VS1838B OUT                  │  A B C
   D3   │ IR LED gate (via MOSFET)     │  A B D
   D4   │ keypad row 1                 │  A
   D5   │ keypad row 2                 │  A
   D6   │ keypad row 3                 │  A
   D7   │ keypad row 4                 │  A
   D8   │ ULN2003 IN1                  │  A
   D9   │ ULN2003 IN2                  │  A
   D10  │ ULN2003 IN3                  │  A
   D11  │ ULN2003 IN4                  │  A
   D12  │ keypad col 1                 │  A
   D13  │ keypad col 2                 │  A
   A0   │ keypad col 3                 │  A
   A1   │ free                         │
   A2   │ free                         │
   A3   │ free                         │
   A4   │ LCD SDA                      │  A B
   A5   │ LCD SCL                      │  A B
        └──────────────────────────────┘
```
 
## Parts per board
 
| Board | Uno | VS1838B | IR LED | MOSFET | LCD | Keypad | Stepper+ULN | 100 Ω | 220 Ω | 10 k | 10 µF |
|-------|-----|---------|--------|--------|-----|--------|-------------|-------|-------|------|-------|
| A TX | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 2 | 1 | 1 | 1 |
| B RX | 1 | 1 | 1 | 1 | 1 | — | — | 2 | 1 | 1 | 1 |
| C monitor | 1 | 1 | — | — | — | — | — | 1 | — | — | 1 |
| D injector | 1 | — | 1 | 1 | — | — | — | 1 | 1 | 1 | — |
 
That is 3 detectors and 3 emitters — exactly what you have. Both LCDs and one
keypad are used; the second keypad stays boxed as a spare, because a dead matrix
row mid-demo is a classic.
 