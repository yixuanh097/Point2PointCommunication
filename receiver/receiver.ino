#define IDLE 0  // in idle state, wait for a burst from transmitter
#define AWAIT 1  // wait for header signal
#define SAMPLE 2

const int rxPin = 2;
const int txPin = 3;

int prev = HIGH;

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

unsigned long startTimeRx = 0;


void setup() {
  // put your setup code here, to run once:
  pinMode(rxPin, INPUT_PULLUP);
  pinMode(txPin, OUTPUT);
  Serial.begin(9600);
  Serial.println("Initializing");
  attachInterrupt(digitalPinToInterrupt(rxPin), Triggered, CHANGE);
}

void loop() {
  if (stateRx == IDLE){
    if (receivedHigh){
      Serial.println("scanning signal received");
      sendHigh(txPin, burstMs);
      stateRx = AWAIT;
      receivedHigh = false;
    }
  }
  // put your main code here, to run repeatedly:
 else if (stateRx == AWAIT){
      //Serial.println("Awaiting for header");
      if (receivedLow){
        Serial.println("Received edge");
        delay(headerWaitTime - sampleTime /2);
        stateRx = SAMPLE;
        startTimeRx = millis();
        receivedLow = false;
        receivedHigh = false;
      }
    }
  else if (stateRx == SAMPLE) {

      if (countRx > 3) {
        // already sampled four times
        countRx = 0;
        stateRx = AWAIT;
        Serial.println("Sampling Complete");
        Serial.print("Final Read data:");
      Serial.println(readData);
        readData = 0;
      }
      else if ((millis() - startTimeRx) >= sampleTime) {
        if (receivedHigh) {
          readData |= (1 << countRx);
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

void Triggered(){
  if (prev == LOW && digitalRead(rxPin) == HIGH){  // rising edge: header
    receivedLow = true;
  }
  else if (prev == HIGH && digitalRead(rxPin) == LOW){
    receivedHigh = true;
  }
  prev = digitalRead(rxPin);
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