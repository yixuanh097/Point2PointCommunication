/* =====================================================================================================
   hw_CHECK  --  wiring verification ;;;; 

   written and by Fairuz Abushgarah, guided by AI for library information and hardware fault parameters
   ===========================================================================-=========================

    Uploaded to bare arduino, wiring each subsystem as testing followed the notes in the markdown file

   Serial Monitor at 115200, set line ending to "No line ending" or "Newline" (either works),
   type the number of the test you want.

     1  I2C scan            find the LCD's address
     2  LCD test            write to all four rows
     3  IR detector monitor live view of the VS1838B output pin
     4  IR LED blink        1 s on, 1 s off - check with a phone camera
     5  LED -> detector     fire this board's LED, see if its detector hears it
     6  Keypad scan         prints every key you press
     7  Stepper test        90 degrees out, 90 degrees back
     8  Serial rate check   confirms you are at 115200

   Nothing here is timing-critical, so this sketch prints freely — unlike the
   real firmware, where a Serial.println() inside a frame corrupts the protocol!!!!
   =========================================================================== */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Stepper.h>

// Two LCD objects so test 2 can try both common backpack addresses without you
// editing and re-uploading. Costs a few bytes, saves a round trip.
LiquidCrystal_I2C lcdA(0x27, 20, 4);
LiquidCrystal_I2C lcdB(0x3F, 20, 4);

// Two detectors per module, splayed to widen the field of view. Both live on
// PORTD so the real firmware can sample them with a single port read.
const int rxPinA = 2;   // VS1838B A OUT
const int rxPinB = 4;   // VS1838B B OUT
const int txPin  = 3;   // BOTH IR LEDs, through the one MOSFET

const byte ROWS = 4, COLS = 3;
const char keyMap[ROWS][COLS] = {
  { '1', '2', '3' },
  { '4', '5', '6' },
  { '7', '8', '9' },
  { '*', '0', '#' }
};
const byte rowPins[ROWS] = { 5, 6, 7, 12 };
const byte colPins[COLS] = { 13, A0, A1 };

const int stepsPerRev = 2048;
Stepper myStepper(stepsPerRev, 8, 10, 9, 11);   // IN1, IN3, IN2, IN4


void setup() {
  Serial.begin(115200);

  pinMode(rxPinA, INPUT_PULLUP);
  pinMode(rxPinB, INPUT_PULLUP);
  pinMode(txPin, OUTPUT);
  digitalWrite(txPin, LOW);
  noTone(txPin);

  for (byte r = 0; r < ROWS; r++) { pinMode(rowPins[r], OUTPUT); digitalWrite(rowPins[r], HIGH); }
  for (byte c = 0; c < COLS; c++) { pinMode(colPins[c], INPUT_PULLUP); }

  myStepper.setSpeed(10);
  Wire.begin();

  menu();
}


void menu() {
  Serial.println();
  Serial.println(F("===== HW_CHECK ====="));
  Serial.println(F(" 1  I2C scan          (after wiring the LCD)"));
  Serial.println(F(" 2  LCD test          (after test 1 finds an address)"));
  Serial.println(F(" 3  IR detector       (after wiring the VS1838B)"));
  Serial.println(F(" 4  IR LED blink      (after wiring the LED + MOSFET)"));
  Serial.println(F(" 5  LED -> detector   (needs both, on this one board)"));
  Serial.println(F(" 6  Keypad scan       (after wiring the keypad)"));
  Serial.println(F(" 7  Stepper test      (after wiring the ULN2003)"));
  Serial.println(F(" 8  Serial rate check"));
  Serial.println(F("Type a number and press Enter. Any key stops a running test."));
}


void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  while (Serial.available()) Serial.read();   // drop the newline

  switch (c) {
    case '1': i2cScan();       break;
    case '2': lcdTest();       break;
    case '3': detectorTest();  break;
    case '4': ledBlink();      break;
    case '5': selfLoop();      break;
    case '6': keypadTest();    break;
    case '7': stepperTest();   break;
    case '8': Serial.println(F("If you can read this cleanly, you are at 115200. Good."));
              break;
    case '\n': case '\r': return;
    default:  Serial.println(F("Unknown option."));
  }
  menu();
}


/* --- 1: I2C scan ---------------------------------------------------------
   Walks every legal I2C address and reports which ones answer. An LCD backpack
   is almost always 0x27 or 0x3F. Finding NOTHING means a wiring fault, not a
   dead LCD: check SDA on A4, SCL on A5, and that the LCD has 5V and ground. */
void i2cScan() {
  Serial.println(F("\nScanning I2C bus..."));
  int found = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("  device at 0x"));
      if (addr < 16) Serial.print('0');
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (found == 0) {
    Serial.println(F("  NOTHING FOUND."));
    Serial.println(F("  -> SDA must be on A4, SCL on A5. Check LCD power and ground."));
  } else {
    Serial.print(F("  ")); Serial.print(found); Serial.println(F(" device(s). That address goes in the firmware."));
  }
}


/* --- 2: LCD test ---------------------------------------------------------
   Writes to both common addresses. Whichever one lights up is yours. If the
   backlight is on but the rows are blank, that is CONTRAST, not address —
   turn the small blue potentiometer on the back of the I2C backpack. */
void lcdTest() {
  Serial.println(F("\nWriting to 0x27 and 0x3F. One of them should show text."));
  Serial.println(F("Backlight on but no characters = turn the contrast pot."));

  lcdA.init(); lcdA.backlight();
  lcdB.init(); lcdB.backlight();

  for (uint8_t r = 0; r < 4; r++) {
    lcdA.setCursor(0, r); lcdA.print("0x27 row ");  lcdA.print(r);
    lcdB.setCursor(0, r); lcdB.print("0x3F row ");  lcdB.print(r);
  }
  Serial.println(F("Done. Look at the screen."));
}


/* --- 3: IR detector monitor ----------------------------------------------
   The VS1838B idles HIGH and pulls LOW when it sees 38 kHz. Wave any TV or
   air-conditioner remote at it and you should see a burst of transitions.

   A remote is the ideal test source here: it is a known-good 38 kHz emitter
   that has nothing to do with your circuit, so a response proves the detector
   and its wiring in isolation. */
void detectorTest() {
  Serial.println(F("\nDetector monitor, BOTH detectors. Point a TV remote at each in turn."));
  Serial.println(F("Idle should read HIGH on both. Send any character to stop.\n"));

  int lastA = digitalRead(rxPinA), lastB = digitalRead(rxPinB);
  unsigned long edgesA = 0, edgesB = 0, t0 = millis();

  Serial.print(F("det A idle: ")); Serial.println(lastA ? F("HIGH (correct)") : F("LOW  <-- suspicious"));
  Serial.print(F("det B idle: ")); Serial.println(lastB ? F("HIGH (correct)") : F("LOW  <-- suspicious"));
  if (lastA == LOW || lastB == LOW)
    Serial.println(F("  A permanently LOW pin usually means OUT and VCC are swapped."));

  while (!Serial.available()) {
    int nowA = digitalRead(rxPinA), nowB = digitalRead(rxPinB);
    if (nowA != lastA) { edgesA++; lastA = nowA; }
    if (nowB != lastB) { edgesB++; lastB = nowB; }
    if (millis() - t0 > 1000) {
      // Test each detector separately. If only one ever moves, the other's OUT
      // wire is wrong - and because the firmware picks whichever detector hears
      // the frame first, a dead second detector is easy to miss later: the link
      // still works, you have just silently lost half your field of view.
      Serial.print(F("edges/s   A: ")); Serial.print(edgesA);
      Serial.print(F("   B: "));        Serial.println(edgesB);
      edgesA = 0; edgesB = 0; t0 = millis();
    }
  }
  while (Serial.available()) Serial.read();
  Serial.println(F("stopped."));
}


/* --- 4: IR LED blink -----------------------------------------------------
   940 nm is invisible to you but NOT to a phone camera, which sees it as a
   faint purple-white glow. This is the only practical way to confirm the LED
   is alive and the right way round — and a backwards LED looks exactly like a
   broken protocol once it is sealed in an enclosure. */
void ledBlink() {
  Serial.println(F("\n1 s ON, 1 s OFF. POINT A PHONE CAMERA AT THE LED."));
  Serial.println(F("You should see a faint purple-white flicker on the phone screen."));
  Serial.println(F("Nothing at all = LED backwards (long leg is the anode) or MOSFET miswired."));
  Serial.println(F("Send any character to stop.\n"));

  while (!Serial.available()) {
    tone(txPin, 38000);
    Serial.println(F("  ON"));
    delay(1000);
    noTone(txPin); digitalWrite(txPin, LOW);
    Serial.println(F("  off"));
    delay(1000);
  }
  noTone(txPin); digitalWrite(txPin, LOW);
  while (Serial.available()) Serial.read();
  Serial.println(F("stopped."));
}


/* --- 5: LED -> own detector ----------------------------------------------
   Fires this board's own LED and watches its own detector. On a breadboard the
   two are inches apart, so the detector WILL hear it — that is the point.

   This single test proves the whole analogue chain end to end: MOSFET switching,
   LED emitting, detector demodulating, pin reading. If tests 3 and 4 pass but
   this fails, the fault is between them: usually a missing common ground.

   (In the real firmware this same coupling is a nuisance, which is why the
   receiver blanks itself after sending its ACK.) */
void selfLoop() {
  Serial.println(F("\nFiring own LEDs, watching BOTH own detectors. 5 pulses."));

  int passA = 0, passB = 0;
  for (int i = 0; i < 5; i++) {
    bool sawA = false, sawB = false;
    tone(txPin, 38000);
    unsigned long t0 = millis();
    while (millis() - t0 < 200) {
      if (digitalRead(rxPinA) == LOW) sawA = true;
      if (digitalRead(rxPinB) == LOW) sawB = true;
    }
    noTone(txPin); digitalWrite(txPin, LOW);
    if (sawA) passA++;
    if (sawB) passB++;

    Serial.print(F("  pulse ")); Serial.print(i + 1);
    Serial.print(F("   A: ")); Serial.print(sawA ? F("PASS") : F("fail"));
    Serial.print(F("   B: ")); Serial.println(sawB ? F("PASS") : F("fail"));
    delay(300);
  }

  Serial.print(F("A passed ")); Serial.print(passA);
  Serial.print(F("/5,  B passed ")); Serial.print(passB); Serial.println(F("/5"));
  Serial.println(F("Both 5/5: emitter and both detector chains work."));
  Serial.println(F("One detector 0/5: that detector's OUT, VCC or GND wire."));
  Serial.println(F("Both 0/5: check common ground first, then tests 3 and 4 separately."));
}


/* --- 6: Keypad scan ------------------------------------------------------
   Drives one row LOW at a time and reads the three columns. A column reading
   LOW is the pressed key.

   If a whole ROW is dead, that row's wire is wrong. If a whole COLUMN is dead,
   that column's wire is wrong. If keys are transposed (pressing 1 reports 3),
   your row and column groups are swapped — on a 7-pin 3x4 pad the first four
   pins are rows, the last three are columns. */
void keypadTest() {
  Serial.println(F("\nPress keys. Send any character over serial to stop.\n"));
  char last = 0;
  unsigned long lastMs = 0;

  while (!Serial.available()) {
    char found = 0;
    for (byte r = 0; r < ROWS && found == 0; r++) {
      digitalWrite(rowPins[r], LOW);
      delayMicroseconds(50);
      for (byte c = 0; c < COLS; c++) {
        if (digitalRead(colPins[c]) == LOW) {
          found = keyMap[r][c];
          Serial.print(F("  key '")); Serial.print(found);
          Serial.print(F("'   row ")); Serial.print(r + 1);
          Serial.print(F(" (D")); Serial.print(rowPins[r]);
          Serial.print(F(")   col ")); Serial.print(c + 1);
          Serial.println();
          break;
        }
      }
      digitalWrite(rowPins[r], HIGH);
    }
    if (found == 0) { last = 0; continue; }
    if (found == last && millis() - lastMs < 250) continue;
    last = found; lastMs = millis();
  }
  while (Serial.available()) Serial.read();
  Serial.println(F("stopped."));
}


/* --- 7: Stepper test -----------------------------------------------------
   512 steps is a quarter turn on a 28BYJ-48 (2048 steps per output revolution
   after its 64:1 gearbox).

   Buzzing without turning means the pin ORDER is wrong. The constructor here is
   Stepper(2048, 8, 10, 9, 11) — note 8, 10, 9, 11, not sequential. The coils
   inside the motor are not wired in the order its connector implies. */
void stepperTest() {
  Serial.println(F("\n512 steps forward (90 deg), pause, 512 back."));
  Serial.println(F("Buzzing but not turning = pin order. Must be 8, 10, 9, 11."));
  Serial.println(F("Turning then stalling = not enough current; give the ULN2003 its own 5V."));

  myStepper.step(512);
  Serial.println(F("  forward done"));
  delay(700);
  myStepper.step(-512);
  Serial.println(F("  back done - should be at the original position"));

  // Leave the coils de-energised. Holding torque costs ~240 mA continuously and
  // cooks both the driver and your battery for no reason while idle.
  for (int p = 8; p <= 11; p++) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
}