/*
 * ============================================================
 *  EK 210 - Free-Space Optical Communication  |  TRANSMITTER
 * ============================================================
 *  WHAT THIS SKETCH DOES
 *  ---------------------
 *  1. Lets the user type a 10-digit message on the keypad.
 *     ('*' clears the entry, '#' confirms it.)
 *  2. Converts each digit into Morse code (dots and dashes).
 *  3. Blinks the IR LED in that Morse pattern to send the
 *     message as pulses of infrared light.
 *  4. Sweeps a servo back and forth across a +/-45 degree arc
 *     so the beam AUTONOMOUSLY finds the receiver - the team
 *     starts it, but never steers it by hand (client's rule).
 *  5. Repeats the transmission at each sweep position so at
 *     least one pass lands on the receiver.
 *
 *  Serial Monitor here is just for the operator's feedback
 *  (what got typed, when transmission starts). The RECEIVER's
 *  serial output is the one you film for the demo.
 *
 *  NOTE: The IRremote library is NOT used. Keypad and Servo
 *  libraries are general-purpose and allowed.
 * ============================================================
 */

#include <Keypad.h>   // Reads the matrix keypad (allowed)
#include <Servo.h>    // Drives the search servo (allowed)

// ---- Keypad setup (4 rows x 3 columns) ----
const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
byte rowPins[ROWS] = {9, 8, 7, 6}; // keypad row pins
byte colPins[COLS] = {5, 4, 3};    // keypad column pins
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ---- IR emitter ----
const int IR_LED_PIN = 11; // IR LED (through a current-limiting resistor)

// ---- Servo: autonomous rotary search ----
Servo searchServo;
const int SERVO_PIN    = 10;
const int SERVO_CENTER = 90;  // set so this angle points AT the receiver
const int SERVO_RANGE  = 45;  // sweep +/- this many degrees
int servoAngle = SERVO_CENTER - SERVO_RANGE; // start at one edge of the arc
int servoStep  = 3;           // degrees moved per sweep step
int servoDir   = 1;           // +1 or -1: current sweep direction

// ---- Morse timing (milliseconds) ----
// These durations MUST stay consistent with the receiver's
// thresholds: dash longer than dot, and the digit gap longer
// than the receiver's WORD_GAP (1500) with margin to spare.
const int DOT_MS       = 150;  // dot blink length
const int DASH_MS      = 450;  // dash blink length
const int SYMBOL_GAP_MS = 150; // gap between symbols inside one digit
const int DIGIT_GAP_MS = 1800; // gap AFTER a digit (tells RX to decode it)

String message = "";       // the digits typed so far
bool messageReady = false; // has the user pressed '#' on 10 digits?

// ---- Morse lookup table for digits 0-9 ----
struct MorseEntry {
  char symbol;
  const char* code;
};
MorseEntry morseTable[10] = {
  {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
  {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."}
};

void setup() {
  Serial.begin(9600);
  pinMode(IR_LED_PIN, OUTPUT);
  digitalWrite(IR_LED_PIN, LOW);

  searchServo.attach(SERVO_PIN);
  searchServo.write(SERVO_CENTER); // start pointed straight ahead

  Serial.println("========================================");
  Serial.println(" EK 210 Optical Comm - TRANSMITTER");
  Serial.println(" Type 10 digits, '*'=clear, '#'=confirm");
  Serial.println("========================================");
}

void loop() {
  if (!messageReady) {
    // Phase 1: collect the message from the keypad.
    captureMessage();
  } else {
    // Phase 2: autonomous search + transmit.
    // Move to the next sweep angle, let the servo settle, then
    // blink out the whole message once.
    searchServo.write(servoAngle);
    delay(120); // give the servo time to physically reach the angle
    transmitMessage();

    // Advance the sweep; reverse direction at the arc limits so it
    // keeps oscillating across the +/-45 degree range.
    servoAngle += servoStep * servoDir;
    if (servoAngle >= SERVO_CENTER + SERVO_RANGE ||
        servoAngle <= SERVO_CENTER - SERVO_RANGE) {
      servoDir *= -1;
    }
  }
}

// Read keypresses and build up the 10-digit message.
void captureMessage() {
  char key = keypad.getKey();
  if (!key) return; // nothing pressed this pass

  if (key >= '0' && key <= '9' && message.length() < 10) {
    message += key;
    Serial.print("Digit entered: ");
    Serial.print(key);
    Serial.print("  (");
    Serial.print(message.length());
    Serial.println("/10)");
  } else if (key == '*') {
    message = "";
    Serial.println("Message cleared.");
  } else if (key == '#') {
    if (message.length() == 10) {
      messageReady = true;
      Serial.print("Message locked: ");
      Serial.println(message);
      Serial.println("Beginning autonomous search + transmission...");
    } else {
      Serial.println("Need exactly 10 digits before confirming.");
    }
  }
}

// Blink the entire message out in Morse, one digit at a time.
void transmitMessage() {
  for (unsigned int i = 0; i < message.length(); i++) {
    const char* code = getMorseCode(message[i]);
    if (code == nullptr) continue;

    // Send each dot/dash of this digit.
    for (int j = 0; code[j] != '\0'; j++) {
      blinkLED(code[j] == '.' ? DOT_MS : DASH_MS);
      delay(SYMBOL_GAP_MS); // short gap between symbols
    }
    delay(DIGIT_GAP_MS); // long gap so the receiver knows the digit ended
  }
}

// Turn the IR LED on for the given duration, then off.
void blinkLED(int duration) {
  digitalWrite(IR_LED_PIN, HIGH);
  delay(duration);
  digitalWrite(IR_LED_PIN, LOW);
}

// Look up the Morse code string for a given digit character.
const char* getMorseCode(char digit) {
  for (int i = 0; i < 10; i++) {
    if (morseTable[i].symbol == digit) return morseTable[i].code;
  }
  return nullptr;
}