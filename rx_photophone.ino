/*
 * ============================================================
 *  EK 210 - Free-Space Optical Communication  |  RECEIVER
 * ============================================================
 *  WHAT THIS SKETCH DOES
 *  ---------------------
 *  1. Watches the IR sensor's digital output pin.
 *  2. Measures how long each IR "blink" lasts and classifies
 *     it as a Morse dot (.) or dash (-).
 *  3. Groups those dots/dashes into digits using timing gaps.
 *  4. Looks each digit up in a Morse table and rebuilds the
 *     original 10-digit message.
 *  5. Prints progress and the final message to the SERIAL
 *     MONITOR (this is what you'll film for the prototype demo).
 *
 *  IMPORTANT: This does NOT use the IRremote library, which is
 *  banned for this project. All decoding is done by hand with
 *  digitalRead() + millis() timing. That's the whole point of
 *  the "custom decoding" requirement.
 *
 *  Open Tools > Serial Monitor and set the baud rate to 9600
 *  (bottom-right dropdown) to see the output.
 * ============================================================
 */

#include <LiquidCrystal.h>   // Standard LCD library (allowed; only IRremote is banned)

// LCD is optional for the video. If it isn't wired yet, these
// lines simply do nothing visible - Serial still works fine.
LiquidCrystal lcd(7, 8, 9, 10, 11, 12); // RS, E, D4, D5, D6, D7

// ---- Pins ----
const int SENSOR_PIN   = 2;   // IR receiver module's OUTPUT pin
const int LED_INDICATOR = 13; // Onboard LED: mirrors the incoming blinks

// ---- Timing thresholds (milliseconds) ----
// These MUST match the durations the transmitter actually sends.
// A pulse shorter than DOT_MAX counts as a dot; longer = dash.
// A silence longer than WORD_GAP means "that digit is finished,
// go ahead and decode it."
const unsigned long DOT_MAX     = 300;  // dot vs dash cutoff
const unsigned long WORD_GAP    = 1500; // gap that ends a digit
const unsigned long DEBOUNCE_US = 50;   // ignore tiny electrical noise glitches

// ---- Working variables (state we carry between loop() passes) ----
String morseBuffer   = "";   // dots/dashes collected for the CURRENT digit
String decodedMessage = "";  // the full message decoded so far
unsigned long lastChangeTime = 0;   // when the sensor pin last flipped
bool lastState = HIGH;              // previous sensor reading (idle is HIGH)
unsigned long pulseStart = 0;       // timestamp when the current blink began
bool pulseActive = false;           // are we mid-blink right now?

unsigned long systemStartTime = 0;  // used to measure detection time
bool messageComplete = false;       // stop once we've got all 10 digits

// ---- Morse lookup table for the digits 0-9 ----
struct MorseEntry {
  char symbol;
  const char* code;
};
MorseEntry morseTable[10] = {
  {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
  {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."}
};

void setup() {
  Serial.begin(9600);              // start serial link to your computer
  pinMode(SENSOR_PIN, INPUT);
  pinMode(LED_INDICATOR, OUTPUT);

  lcd.begin(16, 2);
  lcd.print("Listening...");

  systemStartTime = millis();      // mark t=0 for the detection timer

  // Banner for the video - makes the serial output easy to read on camera
  Serial.println("========================================");
  Serial.println(" EK 210 Optical Comm - RECEIVER ONLINE");
  Serial.println(" Waiting for transmitted message...");
  Serial.println("========================================");
}

void loop() {
  // Once we've decoded the whole message, freeze and do nothing more.
  if (messageComplete) return;

  bool currentState = digitalRead(SENSOR_PIN); // read the IR sensor
  unsigned long now = millis();                // current time in ms

  // ----- Detect a change on the sensor pin (start or end of a blink) -----
  if (currentState != lastState) {
    delayMicroseconds(DEBOUNCE_US);            // brief wait to reject noise
    if (digitalRead(SENSOR_PIN) == currentState) { // confirm it's a real change

      if (currentState == LOW) {
        // Sensor went LOW = IR light detected = a blink just STARTED.
        pulseStart = now;
        pulseActive = true;
        digitalWrite(LED_INDICATOR, HIGH);     // mirror the blink on pin 13
      } else {
        // Sensor went HIGH = light gone = the blink just ENDED.
        if (pulseActive) {
          unsigned long pulseDuration = now - pulseStart;
          // Classify this blink: short = dot, long = dash.
          morseBuffer += (pulseDuration < DOT_MAX) ? '.' : '-';
          pulseActive = false;
        }
        digitalWrite(LED_INDICATOR, LOW);
      }

      lastChangeTime = now;
      lastState = currentState;
    }
  }

  // ----- Check whether the gap of silence means a digit is done -----
  if (!pulseActive && morseBuffer.length() > 0) {
    unsigned long silence = now - lastChangeTime;

    if (silence > WORD_GAP) {
      // Long pause: decode whatever dots/dashes we collected.
      char decodedChar = decodeMorse(morseBuffer);

      if (decodedChar != '?') {
        decodedMessage += decodedChar;
        // Live progress to the serial monitor
        Serial.print("Decoded digit: ");
        Serial.print(decodedChar);
        Serial.print("   |  Message so far: ");
        Serial.println(decodedMessage);
        updateLCD();
      } else {
        Serial.print("Unrecognized pattern: ");
        Serial.println(morseBuffer); // helps you debug timing on camera
      }

      morseBuffer = ""; // clear buffer for the next digit

      // ----- Have we received all 10 digits? -----
      if (decodedMessage.length() >= 10) {
        messageComplete = true;
        unsigned long detectionTime = millis() - systemStartTime;

        // Big, clear final report for the prototype video
        Serial.println("========================================");
        Serial.print(" MESSAGE RECEIVED: ");
        Serial.println(decodedMessage);
        Serial.print(" Detection time: ");
        Serial.print(detectionTime / 1000.0, 1);
        Serial.println(" seconds");
        Serial.println("========================================");

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("MSG: " + decodedMessage);
        lcd.setCursor(0, 1);
        lcd.print("Time: " + String(detectionTime / 1000.0, 1) + "s");

        digitalWrite(LED_INDICATOR, HIGH); // solid LED = success
      }
    }
  }

  // ----- Safety net: the 30-second detection window is a hard constraint -----
  if (!messageComplete && (now - systemStartTime > 30000)) {
    messageComplete = true;
    Serial.println("!!! 30-SECOND WINDOW EXPIRED - message incomplete !!!");
    Serial.print("Partial message: ");
    Serial.println(decodedMessage);

    lcd.clear();
    lcd.print("TIMEOUT");
    lcd.setCursor(0, 1);
    lcd.print(decodedMessage);
  }
}

// Refresh the LCD with the message-in-progress (no effect if LCD unwired)
void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Receiving...");
  lcd.setCursor(0, 1);
  lcd.print(decodedMessage);
}

// Compare the collected dots/dashes against the table; return the digit
// or '?' if nothing matches.
char decodeMorse(String code) {
  for (int i = 0; i < 10; i++) {
    if (code == morseTable[i].code) return morseTable[i].symbol;
  }
  return '?';
}