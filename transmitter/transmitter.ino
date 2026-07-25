#include <Stepper.h>

#define ROTATE 1
#define SEARCH 2
#define ESTABLISH 3
#define TRANSMIT 4

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
int voltage = LOW; // emitter data pin voltage for plotting


int state = TRANSMIT;
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

void setup() {
  // put your setup code here, to run once:
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
}

void loop() {
  //   Serial.println(millis());

  // Serial.print("Transmit: ");
  // Serial.println(voltage);
  if (recMode) {
    if (stateRx == AWAIT){
      //Serial.println("Awaiting for header");
      if (receivedLow){
        Serial.println("Received edge");
        delay(headerWaitTime - sampleTime /2);
        stateRx = SAMPLE;
        startTimeRx = millis();
      }
    }
    if (stateRx == SAMPLE) {

      if (countRx > 3) {
        // already sampled four times
        countRx = 0;
        stateRx = IDLE;
        Serial.println("Sampling Complete");
      }
      else if ((millis() - startTimeRx) >= sampleTime) {
        if (receivedHigh) {
          readData += (1 << countRx);
          receivedHigh = false;
          Serial.println("Received Data high");
        } else {
          readData = readData;
          Serial.println("Received Data Low");
        }
        Serial.print("Read data:");
      Serial.println(readData);

        startTimeRx = millis();
        countRx++;
      }
      
    }
  }
  // read input
  if (Serial.available() > 0 && !inputted) {
    incoming = Serial.parseInt();
    inputted = true;
    Serial.print("Received Incoming: ");
    Serial.println(incoming, DEC);
    // clear other non-digit numbers
    while (Serial.available() > 0) {
      Serial.read();
    }
  } else if (Serial.available() > 0 && inputted) {
    Serial.println("Transmission in progress");
    // clear other non-digit numbers
    while (Serial.available() > 0) {
      Serial.read();
    }
  } else if (inputted) {
    if (state == TRANSMIT) {
    detachInterrupt(digitalPinToInterrupt(rxPin));
    // transmit encoded message
    //delay(5);  // wait 5 ms to allow receiver's IR LED to stop
    String num_str = String(incoming);
    for (char c : num_str) {
      sendDigit(c - '0');
    }
    Serial.println("Emitting");
    state = TRANSMIT;
    count = 0;
    inputted = false;  // can start new transmission
  }
    //transmitLoop(incoming);
  }
}

void transmitLoop(int num) {
  if (motorEnabled && state == ROTATE) {
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
    sendLow(txPin, gapMs / 10);
    Serial.println("Transmitted burst");
    attachInterrupt(digitalPinToInterrupt(rxPin), irTriggered, FALLING);
    rxDetected = false;
    state = ESTABLISH;
    startTime = millis();
    lastTrigger = millis();
  } else if (state == ESTABLISH) {

    //allow some time to get signal
    if (millis() - startTime == 50) {
      sendHigh(dummyPin, burstMs * 10);
      sendLow(dummyPin, gapMs);
      Serial.println("Transmitted dummy burst");
    }

    if (rxDetected) {
      if (millis() - lastTrigger > debounceMs) {
        state = TRANSMIT;
        rxDetected = false;
        lastTrigger = millis();
      }
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
  } else if (state == TRANSMIT) {
    detachInterrupt(digitalPinToInterrupt(rxPin));
    // transmit encoded message
    //delay(5);  // wait 5 ms to allow receiver's IR LED to stop
    String num_str = String(num);
    for (char c : num_str) {
      sendDigit(c - '0');
    }
    Serial.println("Emitting");
    state = TRANSMIT;
    count = 0;
    inputted = false;  // can start new transmission
  }


  if (!motorEnabled) {
    // motor not enabled
    Serial.println("Stopped");
    digitalWrite(motorEnablePin, LOW);
  }
}

void irTriggered() {
  //Serial.println("Triggered");
  if (state == ESTABLISH) {
    rxDetected = true;
  }
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
