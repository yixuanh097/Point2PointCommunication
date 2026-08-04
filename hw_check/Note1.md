# Does the VS1838B supply filter actually matter? — test protocol

## **Fairuz**  
I'm not asking anyone to take my word for this. Below is a test that produces
numbers, and the numbers decide. **If the readings come out the same with and
without the filter, then the filter isn't our problem and I'll drop it.** I've
tried to design it so that outcome is genuinely possible, because a test that
can only confirm what I already think isn't worth running.

Sketch: `filter_proof/filter_proof.ino`. Serial Monitor at 115200.

---

## What we're arguing about - the supply filter

The VS1838B datasheet specifies a 100 Ω series resistor into VCC and a 10 µF
capacitor across the supply, right at the detector. We built without either.

My claim is that this matters *in our specific layout*, not in general. Three
things about our build stack the odds:

- Our own IR LEDs switch at 38 kHz — the exact frequency the detector is built
  to look for — a few inches from it
- The stepper pulls around 240 mA in switched bursts
- All three share one 5 V rail off one Arduino regulator

The VS1838B has enormous gain, because it has to recover a weak carrier at
several metres. That gain doesn't distinguish between signal arriving at the
photodiode and noise arriving on the supply pin. So the concern isn't abstract
EMC hygiene — it's that we may be feeding our own transmitter's switching
frequency into our own receiver's front end.

The reason this is worth 20 minutes: the symptom is **random dropped frames
that get worse with distance**, which is exactly what a weak optical signal
looks like. So every time we hit it, we moved the units closer and blamed the
encoding. If it's the supply, we've been chasing the wrong variable.

---

## Wiring

Detectors, LEDs and stepper exactly as `module.ino` expects. One extra part,
only for Measurement 2:

```
VS1838B VCC pin ──[ 10 kΩ ]──┬── A2
                             │
                          [ 2.2 kΩ ]
                             │
                            GND
```

**Why a divider instead of just reading the pin.** `analogRead` compares
against AREF, which by default *is* the 5 V rail. Measuring the 5 V rail
against a 5 V reference always returns 1023 — the reading is ratiometric, so
the thing you're trying to measure cancels itself out. The sketch switches to
the ATmega's internal 1.1 V bandgap reference, which is independent of the
supply and can therefore see it move. 10 k / 2.2 k scales a 5 V node down to
about 0.90 V, comfortably inside that window. Resolution works out to ~6 mV
per count.

---

## Procedure

Run all three measurements **without** the filter first, write the numbers
down, then solder the 100 Ω and 10 µF in and run the identical sequence again.
Change nothing else between the two rounds — same room, same lighting, same
distance, same aim.

### Measurement 1 — spurious edges *(one board, 40 s)*

Cover both detectors with tape so no IR reaches them, and kill any fluorescent
or LED room lighting that might flicker. We're trying to measure what *our*
circuit does to the detector, so every external source has to go.

Press `1`. The sketch runs four 10-second phases and counts every transition on
each detector output.

The null hypothesis is unusually clean here: **a covered detector should sit
HIGH and never move.** Zero edges, every phase, both rounds. Any edge at all is
one the detector manufactured out of noise — and in a pulse-distance frame, a
manufactured edge *is* a corrupted bit. This measures the failure mechanism
directly rather than by proxy.

| Phase | Aggressor | Edges det A | Edges det B |
|---|---|---|---|
| A | quiet (baseline) | | |
| B | stepper running | | |
| C | IR LEDs pulsing | | |
| D | stepper + LEDs | | |

*(fill in twice — no filter, then filter)*

### Measurement 2 — the supply rail *(one board, 20 s)*

Press `2`. Reports min, max and ripple in mV on the detector's VCC node under
the same four phases. This measures the *cause* where Measurement 1 measures
the *effect*.

**Honest limitation, and I'd rather state it than have it found:** `analogRead`
takes ~104 µs, so this samples at roughly 9.6 kHz. It can see sag and droop but
not the fast switching spikes. The 10 µF helps with both, so this measurement
*understates* the filter's benefit. It's supporting evidence for Measurement 1,
not a substitute.

| Phase | Aggressor | min mV | max mV | ripple mV |
|---|---|---|---|---|
| A | quiet | | | |
| B | stepper | | | |
| C | LEDs | | | |
| D | both | | | |

### Measurement 3 — frame error rate *(both boards, 60 s)*

This is the one that actually matters, because it's measured on the metric we
care about under conditions closest to the real system.

Put the far Uno at 3 m running `module.ino`, type a message, press `#` so it
transmits continuously. Uncover this board's detectors. Press `3`.

The sketch decodes for 30 s with the stepper idle, then 30 s with the stepper
running. **Nothing else changes between the two halves.** The stepper is driven
from a Timer1 interrupt in the background at ~340 Hz — the same step rate the
real sweep uses at 10 rpm — so decoding and motor switching genuinely overlap,
which they wouldn't if I just called `stepper.step()` between frames.

| Round | Stepper idle | Stepper running |
|---|---|---|
| No filter | ____ % | ____ % |
| With filter | ____ % | ____ % |

---

## How to read the result

**The filter is doing real work if:** phases B/C/D in Measurement 1 show
substantial edge counts without it and drop to near zero with it, *and* the
Measurement 3 gap between stepper-idle and stepper-running narrows once the
filter is in.

**The filter is not our problem if:** edge counts are comparable in both
rounds, and Measurement 3 shows the same success rate with the stepper idle and
running. In that case the dropped frames are coming from somewhere else —
most likely AGC blanking from the old level-encoded protocol's long carrier
runs, which is a completely separate mechanism — and I'll say so in the report.

**Partial result is also informative.** If the stepper is a strong aggressor
but the LEDs aren't, that points at the shared regulator rather than at
radiated coupling, and the fix is a separate 5 V feed for the ULN2003 rather
than (or as well as) the filter.

---

## Why I think this is worth the 20 minutes even if I'm wrong

Right now "the filter matters" and "the filter is irrelevant" are both just
opinions, and we can't put either in a report. After this we have a table.

The parts cost under a dollar and the datasheet asks for them regardless, so
the downside of installing them is essentially zero. But that's an argument for
*installing* the filter, not an argument for *believing it fixed our bug* — and
those are different claims. Bifano runs the Photonics Center; if we write "the
supply filter resolved our dropped frames" without having measured before and
after, that's the kind of claim he'll ask us to support and we won't be able to.

So: numbers first, conclusion second.