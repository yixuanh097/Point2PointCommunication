#include <Stepper.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>


#define ROTATE 1
#define SEARCH 2
#define ESTABLISH 3
#define TRANSMIT 4
#define INPUT 5

#define WAIT 1000
#define HEADER_PULSE 150
#define DIGIT_PULSE 100

#define IDLE 0   // in idle state, wait for a burst from transmitter
#define AWAIT 1  // wait for header signal
#define SAMPLE 2

int dummyPin = 6;  // simulate the signal from receiver
int txPin = 3;
int rxPin = 2;
int motorEnablePin = 5;
int numBursts = 10;
const int burstMs = 50;
const int gapMs = 200;
const int debounceMs = 30;

int debugPin = 7;

bool recMode = false;
int voltage = LOW;  // emitter data pin voltage for plotting


int state = INPUT;
int stateRx = IDLE;

const int headerWaitTime = 150;
const int sampleTime = 100;

volatile bool rxDetected = false;
unsigned long lastTrigger = 0;

int count = 0;
int readData = 0;
bool receivedHigh = false;
bool receivedLow = false;

unsigned long startTimeRx = 0;

const int stepsPerRevolution = 2048;
bool motorEnabled = true;
int sweepDeg = 10;
int countRx = 0;
unsigned long startTime = 0;
bool inputted = false;
int incoming = 0;

Stepper myStepper(stepsPerRevolution, 8, 10, 9, 11);

LiquidCrystal_I2C lcd(0x27, 20, 4);

const byte ROWS = 4;  //four rows
const byte COLS = 4;  //four columns
//define the cymbols on the buttons of the keypads
char hexaKeys[ROWS][COLS] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};
byte rowPins[ROWS] = {0, 4, 12, 7};    //connect to the row pinouts of the keypad
byte colPins[COLS] = { A0, A1, A2, A3 };  //connect to the column pinouts of the keypad

Keypad keyPad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

char key;
String keys = "";

void setup() {
  // put your setup code here, to run once:
  lcd.init();                      // initialize the lcd 
  lcd.backlight();
  myStepper.setSpeed(10);
  pinMode(txPin, OUTPUT);
  pinMode(dummyPin, OUTPUT);
  pinMode(rxPin, INPUT_PULLUP);
  pinMode(debugPin, INPUT_PULLUP);
  pinMode(motorEnablePin, OUTPUT);
  Serial.begin(9600);
  Serial.println("Initializing");
  digitalWrite(motorEnablePin, HIGH);
  if (recMode) {
    attachInterrupt(digitalPinToInterrupt(rxPin), fallingTriggered, FALLING);
    attachInterrupt(digitalPinToInterrupt(rxPin), risingTriggered, RISING);
    state = TRANSMIT;
    stateRx = AWAIT;
  }
  state = IDLE;
  Serial.println("Press any button to start");
  lcd.setCursor(0, 0);
  lcd.print("Transmitter: press any button to start");
}

void loop() {
  if (state == IDLE) {
    key = keyPad.getKey();
    if (key) {
      state = ROTATE;
      // lcd.clear();
      // lcd.setCursor(0, 0);
      // lcd.print("Rotating");
      // Serial.println("rotating");
    }
  } else if (state == INPUT) {
    readKeyPad();
  }
  else if (motorEnabled && state == ROTATE) {
    delay(5);
    myStepper.step(stepsPerRevolution / 180);
    delay(5);
    count++;
    // for (int i = 0; i < 45; i++){
    //    myStepper.step(stepsPerRevolution/360);
    //  }
    if (count == sweepDeg) {
      state = SEARCH;
      count = 0;
    } else {
      state = ROTATE;
    }
  }

  else if (state == SEARCH) {
    // transmit
    sendHigh(txPin, burstMs);
    Serial.println("Transmitted burst");
    attachInterrupt(digitalPinToInterrupt(rxPin), irTriggered, FALLING);
    rxDetected = false;
    state = ESTABLISH;
    
    startTime = millis();
  } else if (state == ESTABLISH) {
//Serial.println("entered establish state");
    if (rxDetected) {
        state = INPUT;
        rxDetected = false;
      Serial.println("received signal");
    } else if (millis() - startTime < WAIT) {
      state = ESTABLISH;
      // Serial.print("wait time:");
      //Serial.println(waitTime);
    } else {
      Serial.println("start rotation");
      state = ROTATE;
      count = 0;
    }
  }

  else if (state == TRANSMIT && inputted) {
    detachInterrupt(digitalPinToInterrupt(rxPin));
    // transmit encoded message
    //delay(5);  // wait 5 ms to allow receiver's IR LED to stop
    String num_str = String(keys);
    for (char c : num_str) {
      sendDigit(c - '0');
    }
    Serial.println("Emitting");
    state = INPUT;
    count = 0;
    inputted = false;  // can start new transmission
  }
  //transmitLoop(incoming);
}

void readKeyPad() {
  keys = "";  // clear
  Serial.println("Reading input from keyPad");
  key = keyPad.getKey();
  if (key == '*') {
    state = IDLE;
    return;
  }
  while (key != '#' && !inputted) {
    if (key == '*') {
      sendDigit(15);  // quit signal: 1111
      state = IDLE;
      Serial.println("Press any button to start");  // print upon entering new state
      return;
    }
    //
    key = keyPad.getKey();
    if (key && key != '#') {
      keys += key;
      Serial.println(keys);
    }
  }
  if (key == '#' && !inputted) {
    inputted = true;
    state = TRANSMIT;
    Serial.print("Received Incoming: ");
    Serial.println(keys);
    // clear other non-digit numbers
    // while (Serial.available() > 0) {
    //   Serial.read();
    // }
  } else if (key == '#' && inputted) {
    Serial.println("Transmission in progress");
  }
}

void irTriggered() {
  //Serial.println("Triggered");
    rxDetected = true;
}

void sendHigh(int pin, int width) {
  tone(pin, 38000);  // 38kHz carrier ON
  //voltage = HIGH;
  // Serial.println(millis());
  // Serial.print("Transmit:");
  // Serial.println(voltage);
  Serial.println("send high");

  delay(width);
  noTone(pin);
  //   Serial.println(millis());

  // Serial.print("Transmit:");
  // Serial.println(voltage);
  // voltage = LOW;
  // Serial.print("Transmit:");
  // Serial.println(voltage);
}

void sendLow(int pin, int width) {
  // voltage = LOW;
  //   Serial.println(millis());

  // Serial.print("Transmit:");
  // Serial.println(voltage);
  noTone(pin);  // carrier OFF
  Serial.println("send low");
  delay(width);
  //voltage = LOW;
}

void sendDigit(int digit) {
  // assume single digit
  // header: 150ms of high + 150ms low
  sendHigh(txPin, HEADER_PULSE);
  sendLow(txPin, HEADER_PULSE);
  // get binary
  bool bits[4];  // max 4 bits
  for (int i = 0; i < 4; i++) {
    // bitRead(value, bit_position) reads from right (0) to left (15)
    if (bitRead(digit, i) == 0) {
      sendLow(txPin, DIGIT_PULSE);

    } else {
      sendHigh(txPin, DIGIT_PULSE);
    }
  }
  Serial.print("sent digit: ");
  Serial.println(digit);
}


void fallingTriggered() {
  receivedHigh = true;
  if (stateRx == IDLE) {
    // transmit a "received" signal
    sendHigh(txPin, burstMs);
    sendLow(txPin, gapMs);
    stateRx = AWAIT;
    // Serial.println("Waiting for header signal");
  } else if (stateRx == SAMPLE) {
    receivedHigh = true;
  }
}

void risingTriggered() {
  if (stateRx == AWAIT) {
    receivedLow = true;
    // delay(headerWaitTime - sampleTime /2);
    // stateRx = SAMPLE;
    // //Serial.println("Sampling for data");
    // startTimeRx = millis();  // record start time of sampling
  }
}
