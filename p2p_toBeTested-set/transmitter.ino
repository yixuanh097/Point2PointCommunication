/* ===========================================================================
   P2P OPTICAL LINK  --  TRANSMITTER
   EK 210 photophone project
   ===========================================================================

   WHAT THIS UNIT DOES
     1. The user types a 10-digit message on the 4x4 keypad. The LCD shows it
        as it is entered.
     2. Pressing # starts the search. The stepper sweeps the beam across a
        90-degree arc.
     3. At every sweep position the unit transmits the whole message once,
        then listens briefly for the receiver's acknowledge burst.
     4. When it hears the ACK it stops sweeping - the link is established.

   HOW THE ENCODING WORKS  (the self-clocking part)
     Every bit is sent as a short burst followed by a gap, and the LENGTH OF
     THE GAP carries the value:

         [1 ms burst][1 ms gap] = 0        [1 ms burst][3 ms gap] = 1

     Because every bit opens with its own burst, the receiver gets an edge on
     every single bit. It never has to guess where a bit boundary is, and it
     re-times itself 40 times per message instead of once. That is what makes
     the protocol self-clocking.

     The old protocol held the carrier ON for 100 ms to mean 1. Four 1s in a row
     were 400 ms of unbroken carrier with no edges inside - the receiver had to
     free-run on its own clock, and the VS1838B's gain control started blanking
     the signal partway through. Short uniform bursts fix both problems at once.

   THIS UNIT IS A CARBON COPY OF THE RECEIVER, PLUS:
     - a 4x4 matrix keypad for message entry
     - a 28BYJ-48 stepper for the search sweep
     Everything else - Arduino, IR LED + MOSFET, VS1838B, 20x4 I2C LCD,
     battery - is identical hardware in an identical enclosure.

   PAIRING
     The PROTOCOL block below must be character-for-character identical to the
     one in receiver.ino.
   =========================================================================== */

#include <Stepper.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

/* ===========================================================================
   PROTOCOL  --  MUST MATCH receiver.ino EXACTLY
   Microseconds. All below 16383 (the safe ceiling for delayMicroseconds).
   =========================================================================== */
const unsigned int LEAD_MARK  = 8000;
const unsigned int LEAD_SPACE = 4000;
const unsigned int BIT_MARK   = 1000;
const unsigned int ZERO_SPACE = 1000;
const unsigned int ONE_SPACE  = 3000;
const unsigned int STOP_MARK  = 1000;
const unsigned int ACK_MARK   = 15000;

const int MSG_LEN        = 10;
const int BITS_PER_DIGIT = 4;
const int MSG_GAP_MS     = 50;
/* ========================================================================= */

// ---- Hardware ----
const int rxPin = 2;    // VS1838B output - used only to hear the ACK
const int txPin = 3;    // IR LED through the MOSFET

LiquidCrystal_I2C lcd(0x27, 20, 4);   // try 0x3F if the screen stays blank

// ---- 4x4 matrix keypad ----------------------------------------------------
// A matrix keypad is just 16 switches arranged in a grid. To find which one is
// pressed we drive ONE row LOW at a time and read all four columns. Columns
// idle HIGH because of their pull-up resistors, so a column reading LOW means
// the switch joining that row and column is closed. Scanning 4 rows covers all
// 16 keys with only 8 pins.
const byte ROWS = 4;
const byte COLS = 4;
const char keyMap[ROWS][COLS] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};
const byte rowPins[ROWS] = { 4, 5, 6, 7 };
const byte colPins[COLS] = { 12, 13, A0, A1 };   // A0/A1 work fine as digital

const unsigned long DEBOUNCE_MS = 200;   // ignore repeats within this window
char lastKey = 0;
unsigned long lastKeyMs = 0;

// ---- Stepper sweep --------------------------------------------------------
const int stepsPerRev    = 2048;                              // 28BYJ-48 output
const int sweepStepDeg   = 5;                                 // degrees per hop
const int stepsPerMove   = (long)stepsPerRev * sweepStepDeg / 360;   // = 28
const int sweepPositions = 18;                                // 18 x 5 = 90 deg

Stepper myStepper(stepsPerRev, 8, 10, 9, 11);   // IN1, IN3, IN2, IN4 order

// ---- Runtime state --------------------------------------------------------
#define ENTERING 0    // user is typing the message
#define SEARCHING 1   // sweeping and transmitting
#define LINKED 2      // receiver acknowledged; done

int  mode = ENTERING;
char message[MSG_LEN + 1];
int  entryCount = 0;
int  sweepIndex = 0;
int  sweepDir   = 1;


void setup() {
  pinMode(txPin, OUTPUT);
  pinMode(rxPin, INPUT_PULLUP);

  // Rows are outputs that idle HIGH; only the row being scanned goes LOW.
  for (byte r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH);
  }
  // Columns are inputs held HIGH by the internal pull-ups.
  for (byte c = 0; c < COLS; c++) {
    pinMode(colPins[c], INPUT_PULLUP);
  }

  myStepper.setSpeed(10);   // 10 rpm - slow enough not to lose steps

  Serial.begin(115200);
  Serial.println(F("TRANSMITTER ready."));

  lcd.init();
  lcd.backlight();
  message[0] = '\0';
  showEntryScreen();
}


void loop() {

  // -------------------------------------------------------------------------
  // MODE 1: the user is typing the message on the keypad.
  // -------------------------------------------------------------------------
  if (mode == ENTERING) {
    char key = readKeypad();
    if (key == 0) return;                    // nothing pressed

    if (key >= '0' && key <= '9') {
      if (entryCount < MSG_LEN) {
        message[entryCount++] = key;
        message[entryCount] = '\0';
        showEntryScreen();
      }
    }
    else if (key == '*') {                   // clear and start over
      entryCount = 0;
      message[0] = '\0';
      showEntryScreen();
    }
    else if (key == '#') {                   // send
      if (entryCount == MSG_LEN) {
        mode = SEARCHING;
        sweepIndex = 0;
        sweepDir = 1;
        printRow(2, "Status: Searching");
        printRow(3, "Sweeping for target");
        Serial.print(F("searching with message "));
        Serial.println(message);
      } else {
        printRow(3, "Need 10 digits");
      }
    }
    return;
  }

  // -------------------------------------------------------------------------
  // MODE 2: sweeping. Transmit at this position, then listen for the ACK.
  // -------------------------------------------------------------------------
  if (mode == SEARCHING) {

    sendMessage(message);

    // The receiver only acknowledges after a message decodes cleanly, so an
    // ACK means we are aimed correctly AND the data arrived intact.
    if (listenForAck(250)) {
      mode = LINKED;
      printRow(2, "Status: LINKED");
      printRow(3, "Message delivered");
      Serial.println(F("ACK received - stopping sweep."));
      return;
    }

    // No acknowledgement: nudge the beam and try the next position.
    //
    // Note the message went out regardless of whether the ACK came back. The
    // handshake only tells us when we can STOP - it is never a precondition
    // for sending. A lost ACK costs one extra sweep, never the message.
    myStepper.step(sweepDir * stepsPerMove);

    sweepIndex += sweepDir;
    if (sweepIndex >= sweepPositions || sweepIndex <= 0) {
      sweepDir = -sweepDir;                  // hit an end of the arc - reverse
    }
    return;
  }

  // -------------------------------------------------------------------------
  // MODE 3: linked. Press * to enter a new message.
  // -------------------------------------------------------------------------
  if (mode == LINKED) {
    if (readKeypad() == '*') {
      mode = ENTERING;
      entryCount = 0;
      message[0] = '\0';
      showEntryScreen();
    }
  }
}


/* ---------------------------------------------------------------------------
   KEYPAD SCAN

   Drive one row LOW, read the four columns. A column that reads LOW is the
   pressed key, because closing that switch connects the LOW row to that
   column and overpowers its pull-up.

   The debounce is a simple time window: a mechanical switch bounces for a few
   milliseconds when pressed, which would otherwise register as several
   keystrokes. Ignoring repeats of the same key inside DEBOUNCE_MS is enough
   here and costs nothing.
--------------------------------------------------------------------------- */
char readKeypad() {
  char found = 0;

  for (byte r = 0; r < ROWS && found == 0; r++) {
    digitalWrite(rowPins[r], LOW);           // activate this row
    delayMicroseconds(50);                   // let the line settle

    for (byte c = 0; c < COLS; c++) {
      if (digitalRead(colPins[c]) == LOW) {
        found = keyMap[r][c];
        break;
      }
    }
    digitalWrite(rowPins[r], HIGH);          // release the row
  }

  if (found == 0) {
    lastKey = 0;                             // key released - allow it again
    return 0;
  }

  if (found == lastKey && (millis() - lastKeyMs) < DEBOUNCE_MS) {
    return 0;                                // still the same press
  }

  lastKey = found;
  lastKeyMs = millis();
  return found;
}


/* ---------------------------------------------------------------------------
   PHYSICAL LAYER
   mark()  = carrier ON  for `us` microseconds (receiver's detector goes LOW)
   space() = carrier OFF for `us` microseconds (detector goes HIGH)

   tone() generates the 38 kHz carrier on a hardware timer in the background,
   so the microsecond delay is what actually sets the burst width.
--------------------------------------------------------------------------- */
void mark(unsigned int us) {
  tone(txPin, 38000);
  delayMicroseconds(us);
  noTone(txPin);
}

void space(unsigned int us) {
  delayMicroseconds(us);                     // carrier already off; just wait
}


/* ---------------------------------------------------------------------------
   Send one complete message.

   Frame layout:
       LEAD_MARK  LEAD_SPACE  [bit][bit] ... [bit]  STOP_MARK  quiet

   40 bits total (10 digits x 4 bits), least-significant bit of each digit
   first, digits in the order the user typed them.

   The STOP_MARK matters: without it the last bit's gap would run straight into
   the inter-message quiet time and the receiver would have no way to measure
   it. Every gap needs a burst on both sides.
--------------------------------------------------------------------------- */
void sendMessage(const char *msg) {
  mark(LEAD_MARK);
  space(LEAD_SPACE);

  for (int d = 0; d < MSG_LEN; d++) {
    int digit = msg[d] - '0';                // character '7' -> number 7

    for (int i = 0; i < BITS_PER_DIGIT; i++) {
      mark(BIT_MARK);                        // the clock tick
      // bitRead(value, i) reads bit i counting up from the LSB
      space(bitRead(digit, i) ? ONE_SPACE : ZERO_SPACE);
    }
  }

  mark(STOP_MARK);
  delay(MSG_GAP_MS);                         // quiet gap for resynchronisation
}


/* ---------------------------------------------------------------------------
   Listen for the receiver's acknowledge burst.

   The ACK is one 15 ms burst - far longer than any burst in the message frame
   (the widest is the 8 ms lead-in), so it cannot be confused with our own
   signal bouncing off a wall. We accept anything wider than 12 ms.
--------------------------------------------------------------------------- */
bool listenForAck(unsigned long windowMs) {
  unsigned long windowStart = millis();

  while (millis() - windowStart < windowMs) {
    if (digitalRead(rxPin) == LOW) {         // a burst started
      unsigned long burstStart = micros();

      // Measure it, with a ceiling so a stuck-LOW pin cannot hang the sketch.
      while (digitalRead(rxPin) == LOW && (micros() - burstStart) < 40000) {
        // spin
      }

      if ((micros() - burstStart) > 12000) return true;
    }
  }
  return false;
}


/* ---------------------------------------------------------------------------
   DISPLAY
--------------------------------------------------------------------------- */
void showEntryScreen() {
  printRow(0, "P2P Transmitter");

  // Show the digits entered so far, with underscores for the ones still to go,
  // so the user can see at a glance how many remain.
  char line[21];
  char slots[MSG_LEN + 1];
  for (int i = 0; i < MSG_LEN; i++) slots[i] = (i < entryCount) ? message[i] : '_';
  slots[MSG_LEN] = '\0';

  snprintf(line, sizeof(line), "Msg: %s", slots);
  printRow(1, line);
  printRow(2, "Status: Entry");
  printRow(3, "* clear    # send");
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
