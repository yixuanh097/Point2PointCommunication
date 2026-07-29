#define IDLE 0  // in idle state, wait for a burst from transmitter
#define AWAIT 1  // wait for header signal
#define SAMPLE 2

const int rxPin = 2;
const int txPin = 3;

volatile int prev = HIGH;

const int burstMs = 100;
const int gapMs = 100;
const int headerWaitTime = 150;
const int sampleTime = 100;
const int debounceTime = 30;
const int waitSendTime = 100;
const int waitHeadTime = 100;

int stateRx = IDLE;
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
  stateRx = IDLE;
}

void loop() {
  if (stateRx == IDLE){
    if (receivedHigh){
      Serial.println("scanning signal received");
      delay(waitSendTime);
      sendHigh(txPin, burstMs);
      delay(waitHeadTime);
      receivedHigh = false;
      receivedLow = false;
      stateRx = AWAIT;
    }
  }
  // put your main code here, to run repeatedly:
 else if (stateRx == AWAIT){
      //Serial.println("Awaiting for header");
      if (receivedLow){
        Serial.println("Received edge");
        delay(headerWaitTime - sampleTime/2);
        stateRx = SAMPLE;
        startTimeRx = millis();
        
        receivedLow = false;
        receivedHigh = false;
        prev = HIGH;
      }
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
      if (readData == 15){  // quit signal
      stateRx = IDLE;
      }
        readData = 0;
      }
      else if ((millis() - startTimeRx) >= sampleTime) {
        if (receivedHigh) {
          readData |= (1 << countRx);
          
          Serial.println("Received Data high");
        } else {
          Serial.println("Received Data Low");
        }
        //Serial.print("Read data:");
      //Serial.println(readData);
      //receivedHigh = false;
        startTimeRx = millis();
        countRx++;
      }
      
    }
}

void Triggered(){
  //receivedHigh = true;
  //receivedLow = true;
  if (digitalRead(rxPin) == HIGH){  // rising edge: header
    receivedLow = true;
    receivedHigh = false;
  }
  else if (digitalRead(rxPin) == LOW){
    receivedHigh = true;

  }
  //prev = digitalRead(rxPin);
}

void sendHigh(int pin, int width) {
  tone(pin, 38000);  // 38kHz carrier ON
  Serial.println("sent high");
  delay(width);
  noTone(pin);
}

void sendLow(int pin, int width) {
  noTone(pin);  // carrier OFF
  delay(width);
}