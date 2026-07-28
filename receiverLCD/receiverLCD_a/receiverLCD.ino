#include <Wire.h>
#include <LiquidCrystal_I2C.h>


LiquidCrystal_I2C lcd(0x27, 20, 4);

#define IDLE 0    // wait for a scan burst from the far transmitter
#define AWAIT 1   // wait for the header mark
#define LOCK 2    // header confirmed: acknowledge + re-anchor, then sample
#define SAMPLE 3  // sample the data bits

// ---------------------------------------------------------------------------
// TRANSCEIVER LOCK-ACK
// When the receiver locks the header, fire a short burst out txPin so the far
// transmitter knows we're locked and can begin sending data. THIS IS A TWO-SIDED
// CHANGE: the far transmitter must WAIT for this ack before sending the data
// bits, otherwise the bits arrive while we're still acking and get misaligned.
// If your transmitter does not wait yet, set this to 0 to keep the lock/display
// behavior with your original (working) timing untouched.
#define SEND_LOCK_ACK 1
const int ackMs = 50;   // length of the lock-acknowledge burst

// ---------------------------------------------------------------------------
// OPTIONAL HEADER WIDTH QUALIFICATION
// A genuine header is a long mark (~headerWaitTime); data-bit marks are shorter.
// Gating on width rejects stray edges/noise so we only lock on a real header.
// Left OFF by default because the VS1838B's AGC can chop a long continuous
// carrier, so your measured header width may NOT be ~150 ms. The sketch prints
// the measured mark width on every edge (see Serial) — read real header
// width first, set the window below, THEN flip this to 1.
#define QUALIFY_HEADER 0
const unsigned long headerMin = 120;
const unsigned long headerMax = 180;
// ---------------------------------------------------------------------------

const int rxPin = 2;
const int txPin = 3;

volatile int prev = HIGH;

const int burstMs = 100;
const int gapMs = 100;
const int headerWaitTime = 150;
const int sampleTime = 100;
const int debounceTime = 30;

int stateRx = AWAIT;
int countRx = 0;
int readData = 0;
volatile bool receivedHigh = false;
volatile bool receivedLow = false;

// Header-width measurement (set in the ISR, read in the loop)
volatile unsigned long markStart = 0;
volatile unsigned long lastMarkWidth = 0;

unsigned long startTimeRx = 0;


void setup() {
  pinMode(rxPin, INPUT_PULLUP);
  pinMode(txPin, OUTPUT);
  Serial.begin(9600);
  Serial.println("Initializing");

  lcd.init();
  lcd.backlight();
  printRow(0, "P2P Rx        [    ]");   // [    ] = unlocked, [LOCK] = locked
  printRow(1, "Status: Listening");
  printRow(2, "Data: --");
  printRow(3, "Bits: ----");

  attachInterrupt(digitalPinToInterrupt(rxPin), Triggered, CHANGE);
}

void loop() {
  if (stateRx == IDLE){
    if (receivedHigh){
      Serial.println("scanning signal received");
      printRow(1, "Status: Scanning");
      sendHigh(txPin, burstMs);
      stateRx = AWAIT;
      receivedHigh = false;
    }
  }

  else if (stateRx == AWAIT){
      if (receivedLow){                 //mark just ended (dummy header)
        receivedLow = false;

        bool isHeader = true;
#if QUALIFY_HEADER
        isHeader = (lastMarkWidth >= headerMin && lastMarkWidth <= headerMax);
#endif
        Serial.print("Edge: mark width=");
        Serial.print(lastMarkWidth);
        Serial.println(isHeader ? " -> HEADER LOCK" : " -> ignored");

        if (isHeader){
          stateRx = LOCK;
        }
      }
    }

  else if (stateRx == LOCK){
      // 1) Show the lock on the display (persistent badge on row 0).
      printRow(0, "P2P Rx        [LOCK]");

      // 2) Transceiver: the receiver's transmitter confirms the lock back to
      //    the far unit. (With SEND_LOCK_ACK the far transmitter should be
      //    waiting for this before it streams the data bits.)
#if SEND_LOCK_ACK
      sendHigh(txPin, ackMs);
#endif

      // 3) Re-anchor exactly like original code to line the center sampling back up. startTimeRx is captured AFTER the ack, so the whole thing stays
      //    self-consistent whether the ack is on or off.
      delay(headerWaitTime - sampleTime/2);
      startTimeRx = millis();
      countRx = 0;
      readData = 0;
      receivedLow = false;
      receivedHigh = false;
      prev = HIGH;

      stateRx = SAMPLE;

      // 4) Status now reads "Sampling". Written AFTER anchoring, so this slow
      //    I2C write does not touch the bit-sampling timing.
      printRow(1, "Status: Sampling");
    }

  else if (stateRx == SAMPLE) {

      if (countRx > 3) {
        // already sampled four times
        countRx = 0;
        delay(sampleTime);
        stateRx = AWAIT;
        receivedLow = false;
        Serial.println("Sampling Complete");
        Serial.print("Final Read data:");
        Serial.println(readData);

        // --- LCD update (safe: sampling done, not timing-critical) ---
        printRow(0, "P2P Rx        [    ]");   // release the lock badge
        printRow(1, "Status: Message OK");
        char line[21];
        snprintf(line, sizeof(line), "Data: %d", readData);
        printRow(2, line);
        char bits[21];
        snprintf(bits, sizeof(bits), "Bits: %d%d%d%d",
                 (readData >> 3) & 1, (readData >> 2) & 1,
                 (readData >> 1) & 1, readData & 1);
        printRow(3, bits);
        // -------------------------------------------------------------

        readData = 0;
      }
      else if ((millis() - startTimeRx) >= sampleTime) {
        // Timing-critical bit loop -> no LCD / Serial-heavy work here.
        if (receivedHigh) {
          readData |= (1 << countRx);
          Serial.println("Received Data high");
        } else {
          Serial.println("Received Data Low");
        }
        startTimeRx = millis();
        countRx++;
      }

    }
}

void Triggered(){
  // Keep the ISR tiny. NEVER call lcd.* / Wire / Serial here.
  // rxPin LOW  = carrier ON  (mark)   -> VS1838B pulls its output low
  // rxPin HIGH = carrier OFF (space)
  unsigned long now = millis();
  if (digitalRead(rxPin) == HIGH){      // rising edge: the mark just ended
    lastMarkWidth = now - markStart;    // how long that mark lasted (header check)
    receivedLow = true;
    receivedHigh = false;
  }
  else {                                // falling edge: a mark just began
    markStart = now;
    receivedHigh = true;
  }
}

void sendHigh(int pin, int width) {
  tone(pin, 38000);  // 38kHz carrier ON
  delay(width);
  noTone(pin);
}

void sendLow(int pin, int width) {
  noTone(pin);  // carrier OFF
  delay(width);
}

// Writes a string to one 20-char LCD row, space-padded so leftover characters
// from a previous message are cleared. Avoids lcd.clear() in the loop (slow +
// flickers).
void printRow(uint8_t row, const char *text) {
  char buf[21];
  for (uint8_t i = 0; i < 20; i++) buf[i] = ' ';
  buf[20] = '\0';
  for (uint8_t i = 0; i < 20 && text[i]; i++) buf[i] = text[i];
  lcd.setCursor(0, row);
  lcd.print(buf);
}
