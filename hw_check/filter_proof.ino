/* ===========================================================================
   FILTER_PROOF  --  does the VS1838B supply filter actually do anything?
   ===========================================================================

   THE CLAIM UNDER TEST
     The VS1838B datasheet asks for a 100 ohm series resistor into VCC and a
     10 uF capacitor across the supply at the detector. We built without either.
     The claim is that this omission injects switching noise from our own IR
     LEDs and stepper into the detector's supply and causes dropped frames.

   THIS TEST CAN COME OUT EITHER WAY, AND THAT IS THE POINT
     If the numbers are the same with and without the filter, the filter is not
     our problem and we should stop blaming it. Every measurement below is a
     count or a voltage, not a judgement, so there is nothing to argue about
     afterwards. Run it, write the numbers in the table, and the table decides.

   THREE INDEPENDENT MEASUREMENTS
     1  SPURIOUS EDGES   With no IR source present, a detector should sit HIGH
                         and never move. Every edge it produces is noise. Count
                         them while running the LED and the stepper as
                         aggressors. This measures the failure mechanism
                         directly - false edges are exactly what corrupts a
                         pulse-distance frame.

     2  SUPPLY RAIL      Measure the detector's own VCC node through a divider
                         and report min, max and ripple in millivolts. This
                         measures the cause rather than the effect.

     3  FRAME ERROR RATE With the OTHER module transmitting normally, decode
                         frames with the stepper idle, then with the stepper
                         running. This is the number that actually matters,
                         under conditions closest to the real system.

   Measurements 1 and 2 need only this one board. Measurement 3 needs the far
   module running module.ino and sending a message.

   Open Serial Monitor at 115200 and pick a test.
   =========================================================================== */

/* --- Wiring for this test -------------------------------------------------
   Detectors and LEDs exactly as in module.ino:
       VS1838B A output -> D2        VS1838B B output -> D4
       IR LEDs (both)   -> D3 via the MOSFET
       Stepper          -> D8 D9 D10 D11 via the ULN2003

   ONE EXTRA DIVIDER, only for measurement 2:
       VS1838B VCC pin --[ 10k ]--+-- A2
                                  |
                                 [ 2.2k ]
                                  |
                                 GND

   Why a divider: we read against the chip's internal 1.1 V bandgap reference,
   not against 5 V. Reading a 5 V rail with a 5 V reference always returns full
   scale - the reading is ratiometric, so the rail cancels itself out and you
   learn nothing. The 1.1 V reference is independent of the supply, so it can
   actually see the supply move. 10k/2.2k puts a 5 V node at about 0.90 V,
   comfortably inside the 1.1 V window.
--------------------------------------------------------------------------- */

const uint8_t RXA_PIN  = 2,  RXB_PIN = 4;
const uint8_t RXA_MASK = (1 << 2), RXB_MASK = (1 << 4);
const int     txPin    = 3;
const uint8_t railPin  = A2;

// Divider maths. Node volts = counts * (1.1 / 1023) * (10k + 2.2k) / 2.2k
const float VREF_INTERNAL = 1.1;
const float DIVIDER_GAIN  = (10.0 + 2.2) / 2.2;     // 5.545
const float MV_PER_COUNT  = (VREF_INTERNAL / 1023.0) * DIVIDER_GAIN * 1000.0;

const unsigned long PHASE_MS = 10000;   // 10 s per phase

// ---- Background stepper aggressor ----------------------------------------
// Timer1 fires ~340 times a second and advances the coil pattern on D8-D11
// (PORTB bits 0-3) straight from the ISR. That is the same step rate the real
// sweep uses at 10 rpm, so the electrical disturbance is representative.
//
// This is a NOISE GENERATOR, not a positioning routine. It uses a simple wave
// drive and does not care whether the shaft turns smoothly - we only need it to
// switch inductive current the way the real motor does.
volatile bool stepperOn = false;
volatile uint8_t coilPhase = 0;
const uint8_t wave[4] = { 0b0001, 0b0010, 0b0100, 0b1000 };

ISR(TIMER1_COMPA_vect) {
  if (stepperOn) {
    coilPhase = (coilPhase + 1) & 3;
    PORTB = (PORTB & 0xF0) | wave[coilPhase];
  } else {
    PORTB &= 0xF0;                       // all coils off
  }
}

void startTimer1() {
  noInterrupts();
  TCCR1A = 0;
  TCCR1B = (1 << WGM12) | (1 << CS12);   // CTC, prescaler 256 -> 62500 Hz
  OCR1A  = 183;                          // 62500 / 184 = ~340 Hz
  TIMSK1 = (1 << OCIE1A);
  interrupts();
}


void setup() {
  pinMode(RXA_PIN, INPUT_PULLUP);
  pinMode(RXB_PIN, INPUT_PULLUP);
  pinMode(txPin, OUTPUT);
  digitalWrite(txPin, LOW);
  noTone(txPin);
  for (int p = 8; p <= 11; p++) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }

  analogReference(INTERNAL);             // 1.1 V bandgap
  for (int i = 0; i < 10; i++) analogRead(railPin);   // let the ADC settle

  startTimer1();

  Serial.begin(115200);
  menu();
}


void menu() {
  Serial.println();
  Serial.println(F("===== FILTER_PROOF ====="));
  Serial.println(F(" 1  Spurious edge count   (cover the detectors first)"));
  Serial.println(F(" 2  Supply rail min/max   (needs the A2 divider)"));
  Serial.println(F(" 3  Frame error rate      (needs the far module sending)"));
  Serial.println(F(" 4  Run 1 and 2 back to back"));
  Serial.println();
  Serial.println(F("Record every number. Then add the filter and run again."));
}


void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  while (Serial.available()) Serial.read();

  switch (c) {
    case '1': edgeTest();  break;
    case '2': railTest();  break;
    case '3': frameTest(); break;
    case '4': edgeTest(); railTest(); break;
    case '\n': case '\r': return;
    default: Serial.println(F("Unknown option."));
  }
  menu();
}


/* ===========================================================================
   MEASUREMENT 1  --  spurious edges

   With no IR source in the room, a healthy detector holds its output HIGH and
   never moves. So the null hypothesis is simple and strong: zero edges under
   every condition. Any edge at all is noise that the detector manufactured, and
   in a pulse-distance frame a manufactured edge is a corrupted bit.

   COVER BOTH DETECTORS with electrical tape or your hand, and turn off any
   fluorescent or LED room lighting that might flicker. We are trying to measure
   what OUR circuit does to the detector, so everything external has to go.
   =========================================================================== */
void edgeTest() {
  Serial.println(F("\n--- MEASUREMENT 1: spurious edges ---"));
  Serial.println(F("COVER BOTH DETECTORS NOW. No IR should reach them."));
  Serial.println(F("Any edge counted below is noise the circuit produced.\n"));
  delay(3000);

  Serial.println(F("phase                    det A     det B"));
  Serial.println(F("-------------------------------------------"));

  countPhase(F("A  quiet (baseline)   "), false, false);
  countPhase(F("B  stepper running    "), true,  false);
  countPhase(F("C  IR LEDs pulsing    "), false, true );
  countPhase(F("D  stepper + LEDs     "), true,  true );

  Serial.println(F("-------------------------------------------"));
  Serial.println(F("Phase A should be 0 or very near it either way."));
  Serial.println(F("If B, C or D are large now and near 0 after you add the"));
  Serial.println(F("filter, the filter is doing real work. If they are the"));
  Serial.println(F("same both times, it is not our problem - say so."));
}

void countPhase(const __FlashStringHelper *label, bool stepper, bool led) {
  stepperOn = stepper;

  unsigned long edgesA = 0, edgesB = 0;
  uint8_t lastA = (PIND & RXA_MASK), lastB = (PIND & RXB_MASK);
  unsigned long t0 = millis();
  unsigned long nextToggle = 0;
  bool ledState = false;

  while (millis() - t0 < PHASE_MS) {
    // Pulse the LEDs at roughly the real frame's duty: 1 ms on, 2 ms off.
    if (led && millis() >= nextToggle) {
      ledState = !ledState;
      if (ledState) tone(txPin, 38000);
      else        { noTone(txPin); digitalWrite(txPin, LOW); }
      nextToggle = millis() + (ledState ? 1 : 2);
    }

    uint8_t p = PIND;
    uint8_t a = p & RXA_MASK, b = p & RXB_MASK;
    if (a != lastA) { edgesA++; lastA = a; }
    if (b != lastB) { edgesB++; lastB = b; }
  }

  stepperOn = false;
  noTone(txPin); digitalWrite(txPin, LOW);
  PORTB &= 0xF0;

  Serial.print(label);
  Serial.print(F("  "));
  printPadded(edgesA);
  Serial.print(F("     "));
  printPadded(edgesB);
  Serial.println();
  delay(500);
}

void printPadded(unsigned long v) {
  if (v < 10)     Serial.print(F("    "));
  else if (v < 100)   Serial.print(F("   "));
  else if (v < 1000)  Serial.print(F("  "));
  else if (v < 10000) Serial.print(F(" "));
  Serial.print(v);
}


/* ===========================================================================
   MEASUREMENT 2  --  the supply rail itself

   Measures the cause rather than the effect. Reports the minimum, maximum and
   peak-to-peak swing on the detector's VCC node under each aggressor.

   HONEST LIMITATION, state this when you present it: analogRead takes about
   104 us, so this samples at roughly 9.6 kHz and can only see sag and droop,
   not the fast switching spikes. The 10 uF capacitor works on both, so this
   measurement understates its benefit. It is supporting evidence for
   measurement 1, not a replacement for it.
   =========================================================================== */
void railTest() {
  Serial.println(F("\n--- MEASUREMENT 2: supply rail at the detector ---"));
  Serial.println(F("Needs 10k from VS1838B VCC to A2, and 2.2k from A2 to GND."));
  Serial.print(F("Resolution: "));
  Serial.print(MV_PER_COUNT, 1);
  Serial.println(F(" mV per ADC count.\n"));

  Serial.println(F("phase                   min mV   max mV   ripple mV"));
  Serial.println(F("----------------------------------------------------"));

  railPhase(F("A  quiet (baseline)   "), false, false);
  railPhase(F("B  stepper running    "), true,  false);
  railPhase(F("C  IR LEDs pulsing    "), false, true );
  railPhase(F("D  stepper + LEDs     "), true,  true );

  Serial.println(F("----------------------------------------------------"));
  Serial.println(F("Ripple is max minus min. A rail that moves by tens of mV"));
  Serial.println(F("under load is a rail the detector's gain stage can see."));
}

void railPhase(const __FlashStringHelper *label, bool stepper, bool led) {
  stepperOn = stepper;

  int lo = 1023, hi = 0;
  unsigned long t0 = millis(), nextToggle = 0;
  bool ledState = false;

  while (millis() - t0 < PHASE_MS / 2) {     // 5 s is plenty for min/max
    if (led && millis() >= nextToggle) {
      ledState = !ledState;
      if (ledState) tone(txPin, 38000);
      else        { noTone(txPin); digitalWrite(txPin, LOW); }
      nextToggle = millis() + (ledState ? 1 : 2);
    }
    int v = analogRead(railPin);
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }

  stepperOn = false;
  noTone(txPin); digitalWrite(txPin, LOW);
  PORTB &= 0xF0;

  Serial.print(label);
  Serial.print(F("   "));
  Serial.print(lo * MV_PER_COUNT, 0);
  Serial.print(F("     "));
  Serial.print(hi * MV_PER_COUNT, 0);
  Serial.print(F("      "));
  Serial.println((hi - lo) * MV_PER_COUNT, 0);
  delay(500);
}


/* ===========================================================================
   MEASUREMENT 3  --  frame error rate, the number that actually matters

   Set the far module transmitting a message continuously, aim it at this one,
   and leave it alone. This board decodes for 30 s with the stepper idle, then
   30 s with the stepper running. Nothing else changes between the two halves.

   This is the closest thing to the real system, and it converts the whole
   argument into a single comparison: does our own motor break our own link?
   =========================================================================== */
const unsigned int LEAD_MARK  = 8000, LEAD_SPACE = 4000;
const unsigned int BIT_MARK   = 1000, ZERO_SPACE = 1000, ONE_SPACE = 3000;
const int MSG_LEN = 10, BITS_PER_DIGIT = 4;
const unsigned long EDGE_TIMEOUT_US = 20000;

uint8_t activeMask = RXA_MASK;
char rxBuffer[MSG_LEN + 1];

void frameTest() {
  Serial.println(F("\n--- MEASUREMENT 3: frame error rate ---"));
  Serial.println(F("Point the FAR module at this one and start it sending."));
  Serial.println(F("Uncover the detectors. 30 s idle, then 30 s with stepper.\n"));
  delay(4000);

  unsigned long ok1, rej1, ok2, rej2;
  framePhase(F("stepper IDLE  "), false, ok1, rej1);
  framePhase(F("stepper RUNNING"), true,  ok2, rej2);

  Serial.println();
  Serial.println(F("If the second row is meaningfully worse than the first,"));
  Serial.println(F("our own motor is breaking our own link, and the filter is"));
  Serial.println(F("the fix. If both rows match, the motor is not the issue."));
}

void framePhase(const __FlashStringHelper *label, bool stepper,
                unsigned long &ok, unsigned long &rej) {
  stepperOn = stepper;
  ok = 0; rej = 0;

  unsigned long t0 = millis();
  while (millis() - t0 < 30000UL) {
    uint8_t p = PIND;
    if (!(p & RXA_MASK))      activeMask = RXA_MASK;
    else if (!(p & RXB_MASK)) activeMask = RXB_MASK;
    else continue;

    if (decodeMessage(rxBuffer) == 0) ok++; else rej++;
    waitForIdle(5000);
  }

  stepperOn = false;
  PORTB &= 0xF0;

  unsigned long tot = ok + rej;
  Serial.print(label);
  Serial.print(F("   OK ")); Serial.print(ok);
  Serial.print('/');         Serial.print(tot);
  Serial.print(F("  = "));   Serial.print(tot ? (100.0 * ok / tot) : 0.0, 1);
  Serial.println('%');
  delay(1000);
}

inline bool activeIsLow() { return (PIND & activeMask) == 0; }

long measureLevel(bool low, unsigned long timeoutUs) {
  unsigned long start = micros();
  while (activeIsLow() == low) {
    if ((micros() - start) > timeoutUs) return -1;
  }
  return (long)(micros() - start);
}

bool matches(long actual, long expected) {
  if (actual < 0) return false;
  long tol = expected / 3;
  return (actual > expected - tol) && (actual < expected + tol);
}

int decodeMessage(char *out) {
  if (!matches(measureLevel(true,  EDGE_TIMEOUT_US), LEAD_MARK))  return -1;
  if (!matches(measureLevel(false, EDGE_TIMEOUT_US), LEAD_SPACE)) return -2;
  for (int d = 0; d < MSG_LEN; d++) {
    int value = 0;
    for (int i = 0; i < BITS_PER_DIGIT; i++) {
      if (measureLevel(true, EDGE_TIMEOUT_US) < 0) return -3;
      long g = measureLevel(false, EDGE_TIMEOUT_US);
      if (g < 0) return -4;
      if (matches(g, ONE_SPACE))        value |= (1 << i);
      else if (!matches(g, ZERO_SPACE)) return -5;
    }
    if (value > 9) return -6;
    out[d] = '0' + value;
  }
  out[MSG_LEN] = '\0';
  return 0;
}

void waitForIdle(unsigned long idleUs) {
  unsigned long quietSince = micros();
  while ((micros() - quietSince) < idleUs) {
    uint8_t p = PIND;
    if (!(p & RXA_MASK) || !(p & RXB_MASK)) quietSince = micros();
  }
}
