/*
 * ============================================================
 *  EK 210 - Free-Space Optical Communication  |  RECEIVER
 *  Matched to teammate's 38 kHz binary transmitter
 * ============================================================
 *
 *  THE PROTOCOL THIS DECODES (from the TX sketch)
 *  ----------------------------------------------
 *  Each digit is sent as a 700 ms frame:
 *
 *    [ carrier ON 150ms ][ carrier OFF 150ms ][ 4 x 100ms bit slots ]
 *      \____ header ____/                       \___ 4-bit BCD ___/
 *
 *    In each 100 ms bit slot:  carrier ON  = binary 1
 *                              carrier OFF = binary 0
 *
 *    Bits are sent LEAST-SIGNIFICANT-FIRST, because the TX uses
 *    bitRead(digit, i) with i counting 0,1,2,3.
 *
 *  WHY WE SAMPLE BY TIME INSTEAD OF MEASURING PULSES
 *  -------------------------------------------------
 *  The TX holds the carrier at a LEVEL for each 100 ms slot. Two
 *  1-bits in a row therefore produce ONE continuous 200 ms burst
 *  with no edge between them - there is nothing to "measure."
 *  So once we find the header we stop looking at edges entirely
 *  and just read the pin at the middle of each slot. This is
 *  standard synchronous sampling, same idea as a UART.
 *
 *  SIGNAL POLARITY
 *  ---------------
 *  The TX uses tone(pin, 38000), i.e. a 38 kHz modulated carrier,
 *  so the receiver is a demodulating module. Its output IDLES
 *  HIGH and is pulled LOW while carrier is present.
 *      pin LOW  = light/carrier detected = logical 1 in a bit slot
 *      pin HIGH = no carrier             = logical 0 in a bit slot
 *
 *  HANDSHAKE
 *  ---------
 *  The TX sweeps, fires a short 50 ms search burst, then waits in
 *  its ESTABLISH state for the receiver to answer. This sketch
 *  detects that short burst and fires back its own 38 kHz burst
 *  from an IR LED, which is what trips the TX's interrupt and
 *  makes it start sending data. (Set SEND_ACK to false if you
 *  want to test decoding only, with no reply LED wired.)
 *
 *  No IRremote library is used anywhere - decoding is done by
 *  hand with digitalRead() and millis(), as the project requires.
 *
 *  Open Tools > Serial Monitor at 9600 baud for the demo video.
 * ============================================================
 */

// ---------------- Pins ----------------
const int IR_RX_PIN = 2;   // demodulated IR receiver OUTPUT (idles HIGH)
const int IR_TX_PIN = 3;   // this unit's IR LED, used to answer the TX
const int LED_PIN   = 13;  // onboard LED, mirrors detected carrier

// ---------------- Protocol constants ----------------
// These MUST mirror the transmitter's #defines exactly.
const int HEADER_PULSE  = 150;  // TX HEADER_PULSE
const int DIGIT_PULSE   = 100;  // TX DIGIT_PULSE
const int BITS_PER_DIGIT = 4;
// One full digit frame = header ON + header OFF + 4 bit slots
const int DIGIT_PERIOD  = (2 * HEADER_PULSE) + (BITS_PER_DIGIT * DIGIT_PULSE); // 700 ms

// ---------------- Pulse classification windows ----------------
// We only ever measure ONE pulse by hand: the very first carrier
// burst we see. Its length tells us what kind of event it is.
//   ~50 ms  -> the TX's search burst (burstMs in the TX sketch)
//   ~150 ms -> the header of a data frame (HEADER_PULSE)
// The windows are deliberately loose to tolerate a few ms of jitter.
const unsigned long SEARCH_MIN = 25,  SEARCH_MAX = 90;
const unsigned long HEADER_MIN = 110, HEADER_MAX = 210;

// ---------------- Acknowledgment settings ----------------
const bool SEND_ACK      = true;  // set false to test decoding only
const int  ACK_DELAY_MS  = 220;   // wait before replying (see note below)
const int  ACK_BURST_MS  = 50;    // length of each reply burst
const int  ACK_REPEATS   = 3;     // send a few, to be sure one lands
const int  ACK_GAP_MS    = 60;    // dark gap between reply bursts

// ---------------- Message settings ----------------
const int MAX_DIGITS = 10;             // project spec: 10-digit message
const unsigned long DETECT_WINDOW_MS = 30000;  // hard 30 s constraint

unsigned long systemStartTime = 0;
bool windowExpired = false;

void setup() {
  Serial.begin(9600);
  pinMode(IR_RX_PIN, INPUT_PULLUP);  // matches the TX sketch's rxPin mode
  pinMode(IR_TX_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  noTone(IR_TX_PIN);

  systemStartTime = millis();

  Serial.println(F("========================================"));
  Serial.println(F(" EK 210 Optical Comm - RECEIVER ONLINE"));
  Serial.println(F(" 38 kHz carrier, 4-bit BCD per digit"));
  Serial.println(F(" Waiting for transmitter..."));
  Serial.println(F("========================================"));
}

void loop() {
  // ---- Hard 30-second detection constraint ----
  if (!windowExpired && (millis() - systemStartTime > DETECT_WINDOW_MS)) {
    windowExpired = true;
    Serial.println(F("!!! 30-SECOND WINDOW EXPIRED - no message decoded !!!"));
    Serial.println(F("(Still listening; reset the board to restart the timer.)"));
  }

  // ---- Idle: nothing to do until the carrier appears ----
  // The line sits HIGH when there is no carrier, so we simply
  // return until it drops LOW.
  if (digitalRead(IR_RX_PIN) == HIGH) return;

  // ---- A burst just started. Measure how long it lasts. ----
  unsigned long tFall = millis();
  digitalWrite(LED_PIN, HIGH);

  // Hold here while the carrier is present. The 2 s cap stops us
  // hanging forever if something is stuck LOW.
  while (digitalRead(IR_RX_PIN) == LOW && (millis() - tFall) < 2000) {
    // busy-wait
  }

  unsigned long tRise  = millis();      // moment the carrier switched off
  unsigned long lowDur = tRise - tFall; // how long the burst lasted
  digitalWrite(LED_PIN, LOW);

  // ---- Decide what that burst meant ----
  if (lowDur >= SEARCH_MIN && lowDur <= SEARCH_MAX) {
    // Short burst = the transmitter is sweeping and asking "anyone there?"
    handleSearchBurst();
  }
  else if (lowDur >= HEADER_MIN && lowDur <= HEADER_MAX) {
    // ~150 ms burst = the header of a real data frame.
    // tRise is our timing anchor for everything that follows.
    receiveMessage(tRise);
  }
  else {
    // Anything else is noise, a reflection, or a partial frame
    // caught mid-sweep. Ignore it and keep listening.
    Serial.print(F("Ignored stray burst: "));
    Serial.print(lowDur);
    Serial.println(F(" ms"));
  }
}

/*
 * Reply to the transmitter's search burst so it knows it has
 * found us and can stop sweeping.
 *
 * WHY THE DELAY: the TX sends its 50 ms burst, then sits in a
 * 200 ms dark gap (gapMs) BEFORE it enters the ESTABLISH state.
 * Its interrupt handler ignores anything that arrives while it is
 * still in SEARCH. So we deliberately wait past that gap before
 * answering. If the handshake proves unreliable, ACK_DELAY_MS is
 * the first number to adjust.
 */
void handleSearchBurst() {
  Serial.println(F("Search burst detected - transmitter is sweeping."));

  if (!SEND_ACK) return;

  delay(ACK_DELAY_MS);

  for (int i = 0; i < ACK_REPEATS; i++) {
    tone(IR_TX_PIN, 38000);   // carrier ON
    delay(ACK_BURST_MS);
    noTone(IR_TX_PIN);        // carrier OFF
    delay(ACK_GAP_MS);
  }

  Serial.println(F("Acknowledgment sent - listening for data frame."));
}

/*
 * Decode a whole message, starting from the first header we found.
 *
 * tRise = the instant the first header's carrier switched OFF.
 * Everything downstream is computed from that one anchor, so a
 * long run of identical bits can never desynchronize us.
 */
void receiveMessage(unsigned long tRise) {
  String message = "";
  unsigned long frameStart = millis();

  Serial.println(F("--- Header locked, decoding digits ---"));

  while (message.length() < MAX_DIGITS) {

    int value = 0;
    String slotBits = "";   // bits in the order received (LSB first)

    // ---- Sample the four bit slots ----
    // Slot i occupies  [tRise + 150 + 100*i , +100ms).
    // We read at the CENTER of each slot, which is the point
    // furthest from any switching edge and therefore safest.
    for (int i = 0; i < BITS_PER_DIGIT; i++) {
      unsigned long sampleAt = tRise
                             + HEADER_PULSE                       // header's dark half
                             + (unsigned long)DIGIT_PULSE * i     // slots already passed
                             + (DIGIT_PULSE / 2);                 // aim at slot center
      waitUntil(sampleAt);

      // LOW = carrier present = 1,  HIGH = no carrier = 0
      int bit = (digitalRead(IR_RX_PIN) == LOW) ? 1 : 0;

      // TX used bitRead(digit, i) with i ascending, so bit i is
      // worth 2^i. Shift it back into place.
      value |= (bit << i);
      slotBits += String(bit);
    }

    // BCD digits only ever run 0-9. A value of 10-15 means a bit
    // was misread, which is nearly always a timing or alignment
    // problem rather than a logic problem.
    if (value > 9) {
      Serial.print(F("Invalid BCD group ("));
      Serial.print(slotBits);
      Serial.print(F(" = "));
      Serial.print(value);
      Serial.println(F(") - abandoning frame, waiting for next sweep pass."));
      return;
    }

    message += String(value);
    Serial.print(F("Digit "));
    Serial.print(message.length());
    Serial.print(F(": bits(LSB first)="));
    Serial.print(slotBits);
    Serial.print(F("  value="));
    Serial.print(value);
    Serial.print(F("   message so far: "));
    Serial.println(message);

    // ---- Is another digit coming? ----
    // Digits are sent back to back with no gap, so the next frame's
    // header carrier should be ON from tRise+550 to tRise+700.
    // We peek at the middle of that span. If the carrier is there,
    // another digit is arriving; if not, the transmission ended.
    //
    // This timing check is essential: when a digit's last bit is a
    // 1 (digits 8 and 9), the carrier runs straight into the next
    // header with NO falling edge to detect. Edge-based detection
    // would silently lose sync there.
    unsigned long nextRise = tRise + DIGIT_PERIOD;
    waitUntil(nextRise - (HEADER_PULSE / 2));

    if (digitalRead(IR_RX_PIN) != LOW) {
      Serial.println(F("No further header - end of transmission."));
      break;
    }

    tRise = nextRise;   // re-anchor on the new frame and continue
  }

  // ---- Report the result ----
  unsigned long elapsed = millis() - systemStartTime;

  Serial.println(F("========================================"));
  Serial.print(F(" MESSAGE RECEIVED: "));
  Serial.println(message);
  Serial.print(F(" Digits decoded:   "));
  Serial.println(message.length());
  Serial.print(F(" Frame duration:   "));
  Serial.print((millis() - frameStart) / 1000.0, 2);
  Serial.println(F(" s"));
  Serial.print(F(" Time since start: "));
  Serial.print(elapsed / 1000.0, 1);
  Serial.println(F(" s"));
  Serial.println(F("========================================"));

  // Solid LED = successful decode, easy to see on camera
  digitalWrite(LED_PIN, HIGH);
  delay(1500);
  digitalWrite(LED_PIN, LOW);
}

/*
 * Block until millis() reaches targetMs.
 * The signed cast makes this safe across the millis() rollover
 * and also makes it return immediately if the target already passed.
 */
void waitUntil(unsigned long targetMs) {
  while ((long)(millis() - targetMs) < 0) {
    // busy-wait
  }
}