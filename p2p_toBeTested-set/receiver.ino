/* ===========================================================================
   P2P OPTICAL LINK  --  RECEIVER
   EK 210 photophone project
   ===========================================================================

   WHAT THIS UNIT DOES
     1. Sits still and watches its IR detector.
     2. When a message arrives it decodes 10 digits and shows them on the LCD.
     3. It fires a short acknowledge burst back so the transmitter knows it can
        stop sweeping.

   HOW THE DECODE WORKS  (this is the self-clocking part)
     Every bit on the wire looks like this:

         [1 ms burst][gap]        gap = 1 ms -> bit is 0
                                  gap = 3 ms -> bit is 1

     So the receiver never has to guess WHEN to sample. It simply measures the
     burst, then measures the gap, then classifies the gap by length. The burst
     at the start of each bit IS the clock.

     The practical consequence: each bit is timed from its OWN falling edge, so
     a small error on bit 3 does not shift bit 4. Timing error cannot build up
     across the message. That is what the old level-encoded protocol could not
     do, and it is why digit 1 used to decode while everything after it drifted.

   A NOTE ON BLOCKING
     decodeMessage() blocks for roughly 100-170 ms while a frame is in flight.
     That is intentional and it is FINE here: there is nothing else this unit
     needs to do during those milliseconds, and blocking removes every stale-flag
     and race-condition failure that an interrupt-driven version invites. All
     LCD writes happen after the frame is complete, never during it.

   PAIRING
     The PROTOCOL block below must be character-for-character identical to the
     one in transmitter.ino.
   =========================================================================== */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

/* ===========================================================================
   PROTOCOL  --  MUST MATCH transmitter.ino EXACTLY
   Microseconds. All below 16383 (the safe ceiling for delayMicroseconds).
   =========================================================================== */
const unsigned int LEAD_MARK  = 8000;   // start-of-message burst
const unsigned int LEAD_SPACE = 4000;   // gap after the lead-in
const unsigned int BIT_MARK   = 1000;   // every bit opens with this burst
const unsigned int ZERO_SPACE = 1000;   // gap after the burst = 0
const unsigned int ONE_SPACE  = 3000;   // gap after the burst = 1
const unsigned int STOP_MARK  = 1000;   // closes the final bit's gap
const unsigned int ACK_MARK   = 15000;  // receiver -> transmitter "got it"

const int MSG_LEN        = 10;
const int BITS_PER_DIGIT = 4;
const int MSG_GAP_MS     = 50;
/* ========================================================================= */

// ---- Hardware ----
const int rxPin = 2;    // VS1838B output (idles HIGH, pulls LOW on carrier)
const int txPin = 3;    // IR LED through the MOSFET, used only for the ACK
                        // Shield this LED from this board's own detector, or
                        // aim it away - otherwise the ACK is seen by itself.

LiquidCrystal_I2C lcd(0x27, 20, 4);   // try 0x3F if the screen stays blank

// ---- Timeouts ----
const unsigned long EDGE_TIMEOUT_US = 20000;   // longest legal gap is 4 ms;
                                               // 20 ms means "frame is over"
const unsigned long NO_SIGNAL_MS = 30000;      // client spec: report within 30 s

// ---- Runtime state ----
char rxBuffer[MSG_LEN + 1];
unsigned long lastActivityMs = 0;


void setup() {
  pinMode(rxPin, INPUT_PULLUP);
  pinMode(txPin, OUTPUT);

  Serial.begin(115200);
  Serial.println(F("RECEIVER ready."));

  lcd.init();
  lcd.backlight();
  printRow(0, "P2P Receiver");
  printRow(1, "Status: Listening");
  printRow(2, "Msg: ----------");
  printRow(3, "");

  lastActivityMs = millis();
}


void loop() {
  // The detector idles HIGH. A LOW means a carrier burst just started, which
  // means somebody is transmitting. Only then do we commit to decoding.
  if (digitalRead(rxPin) == LOW) {

    int err = decodeMessage(rxBuffer);

    if (err == 0) {
      // Clean decode. Acknowledge, then show it.
      Serial.print(F("MESSAGE OK: "));
      Serial.println(rxBuffer);

      sendAck();

      printRow(1, "Status: Message OK");
      char line[21];
      snprintf(line, sizeof(line), "Msg: %s", rxBuffer);
      printRow(2, line);
      printRow(3, "Link established");

      lastActivityMs = millis();
      waitForIdle(MSG_GAP_MS * 1000UL);   // don't re-trigger on our own tail
    }
    else {
      // Partial or corrupt frame. Say nothing on the LCD - the transmitter
      // repeats the message at every sweep position, so the next copy is only
      // a fraction of a second away. Reporting every miss would make the
      // display flicker uselessly during the sweep.
      Serial.print(F("frame rejected, code "));
      Serial.println(err);
      waitForIdle(5000);                  // resynchronise on the next frame
    }
  }

  // Client requirement: indicate within 30 seconds of system initiation.
  if ((millis() - lastActivityMs) > NO_SIGNAL_MS) {
    printRow(1, "Status: No signal");
    lastActivityMs = millis();
  }
}


/* ---------------------------------------------------------------------------
   TIMING PRIMITIVE

   Measures how long the pin STAYS at `level`, starting from now, and returns
   that duration in microseconds. Returns -1 if it never changes before the
   timeout - which is how we detect "the frame ended early".

   Everything in the decoder is built out of this one function.
--------------------------------------------------------------------------- */
long measureLevel(uint8_t level, unsigned long timeoutUs) {
  unsigned long start = micros();
  while (digitalRead(rxPin) == level) {
    if ((micros() - start) > timeoutUs) return -1;
  }
  return (long)(micros() - start);
}


/* ---------------------------------------------------------------------------
   Is a measured duration close enough to what we expected?

   +/- 33% is deliberately loose. It has to absorb detector rise time, a little
   interrupt jitter, and part tolerance. It is still safe because the values it
   must tell apart are 3x apart:

        ZERO_SPACE 1000 -> accepts  667 .. 1333
        ONE_SPACE  3000 -> accepts 2000 .. 4000       (no overlap)
--------------------------------------------------------------------------- */
bool matches(long actual, long expected) {
  if (actual < 0) return false;
  long tolerance = expected / 3;
  return (actual > expected - tolerance) && (actual < expected + tolerance);
}


/* ---------------------------------------------------------------------------
   Decode one complete message.

   Returns 0 on success, or a negative error code telling you WHERE it failed:
       -1  lead-in burst was the wrong width  (usually: we joined mid-message)
       -2  gap after the lead-in was wrong
       -3  a bit's burst never arrived        (transmitter moved out of aim)
       -4  a bit's gap never ended
       -5  a gap matched neither 0 nor 1      (marginal signal)
       -6  a digit decoded above 9            (corrupt - see note below)

   Error -6 is a free error detector. Four bits can hold 0-15, but a decimal
   digit only uses 0-9. Any value of 10-15 is proof the frame is corrupt, at
   zero cost in bandwidth.
--------------------------------------------------------------------------- */
int decodeMessage(char *out) {

  // We were called because the line just went LOW, so we are inside the
  // lead-in burst right now. Measure how long it lasts.
  long leadMark = measureLevel(LOW, EDGE_TIMEOUT_US);
  if (!matches(leadMark, LEAD_MARK)) return -1;

  long leadSpace = measureLevel(HIGH, EDGE_TIMEOUT_US);
  if (!matches(leadSpace, LEAD_SPACE)) return -2;

  // 10 digits, 4 bits each, least-significant bit of each digit first.
  for (int d = 0; d < MSG_LEN; d++) {
    int value = 0;

    for (int i = 0; i < BITS_PER_DIGIT; i++) {

      // The clock tick. We do not care how long it is beyond "it happened" -
      // measuring it is what advances us to the start of the gap.
      long bitMark = measureLevel(LOW, EDGE_TIMEOUT_US);
      if (bitMark < 0) return -3;

      // The gap is the data.
      long bitSpace = measureLevel(HIGH, EDGE_TIMEOUT_US);
      if (bitSpace < 0) return -4;

      if (matches(bitSpace, ONE_SPACE)) {
        value |= (1 << i);                 // LSB-first, matching the sender
      }
      else if (!matches(bitSpace, ZERO_SPACE)) {
        return -5;                         // neither length - reject the frame
      }
    }

    if (value > 9) return -6;              // impossible for a decimal digit
    out[d] = '0' + value;
  }

  out[MSG_LEN] = '\0';
  return 0;
}


/* ---------------------------------------------------------------------------
   Wait until the line has been quiet for `idleUs` continuously.

   Any burst restarts the clock. This is how we avoid decoding the tail of a
   frame we already handled, and how we find a clean starting point after a
   rejected frame.
--------------------------------------------------------------------------- */
void waitForIdle(unsigned long idleUs) {
  unsigned long quietSince = micros();
  while ((micros() - quietSince) < idleUs) {
    if (digitalRead(rxPin) == LOW) quietSince = micros();
  }
}


/* ---------------------------------------------------------------------------
   PHYSICAL LAYER - identical to the transmitter's, so both units speak the
   same way. The receiver only ever sends one thing: the acknowledge burst.
--------------------------------------------------------------------------- */
void mark(unsigned int us) {
  tone(txPin, 38000);
  delayMicroseconds(us);
  noTone(txPin);
}

void sendAck() {
  // One long burst, far wider than anything in the message frame, so the
  // transmitter cannot mistake a reflected data bit for an acknowledgement.
  mark(ACK_MARK);
}


/* ---------------------------------------------------------------------------
   Write one 20-character LCD row, space-padded so leftover characters from a
   previous message are cleared. Avoids lcd.clear(), which is slow and flickers.
--------------------------------------------------------------------------- */
void printRow(uint8_t row, const char *text) {
  char buf[21];
  for (uint8_t i = 0; i < 20; i++) buf[i] = ' ';
  buf[20] = '\0';
  for (uint8_t i = 0; i < 20 && text[i]; i++) buf[i] = text[i];
  lcd.setCursor(0, row);
  lcd.print(buf);
}
