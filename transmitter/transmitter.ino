#include <Stepper.h>

#define ROTATE 1
#define SEARCH 2
#define ESTABLISH 3
#define TRANSMIT 4

int dummyPin = 6;  // simulate the signal from receiver
int txPin = 3;
int rxPin = 2;
int motorEnablePin = 5;
int numBursts = 10;
const int burstMs = 50;
const int gapMs = 200;
const int debounceMs = 30;


int state = ROTATE;

int prev = HIGH;
bool rxDetected = false;


const int stepsPerRevolution = 2048;
bool motorEnabled = true;
int sweepDeg = 60;
int count = 0;
int waitTime = 100;  // number of cycles to wait
bool inputted = false;
int incoming = 0;

Stepper myStepper(stepsPerRevolution, 8, 10, 9, 11);

void setup() {
  // put your setup code here, to run once:
  pinMode(txPin, OUTPUT);
  pinMode(dummyPin, OUTPUT);
  pinMode(rxPin, INPUT_PULLUP);
  pinMode(motorEnablePin, OUTPUT);
  Serial.begin(9600);
  Serial.println("Initializing");
  digitalWrite(motorEnablePin, HIGH);
  attachInterrupt(digitalPinToInterrupt(rxPin), irTriggered, FALLING);
}

void loop() {
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
  }
  else if (Serial.available() > 0 && inputted){
    Serial.println("Transmission in progress");
    // clear other non-digit numbers
    while (Serial.available() > 0) {
      Serial.read();
    }
  }
  else if (inputted){
    transmitLoop(incoming);
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
    sendLow(txPin, gapMs);
    Serial.println("Transmitted burst");
    state = ESTABLISH;
    waitTime = 100;
  } else if (state == ESTABLISH) {

    //allow some time to get signal
    if (waitTime == 50) {
      sendHigh(dummyPin, burstMs * 10);
      sendLow(dummyPin, gapMs);
      Serial.println("Transmitted dummy burst");
    }

    if (rxDetected) {
      Serial.print("received signal");
      state = TRANSMIT;
      rxDetected = false;
    } else if (waitTime > 0) {
      state = ESTABLISH;
      waitTime--;
      // Serial.print("wait time:");
      //Serial.println(waitTime);
    } else {
      Serial.println("start rotation");
      state = ROTATE;
      count = 0;
    }
  } else if (state == TRANSMIT) {
    // transmit encoded message
    delay(5);  // wait 5 ms to allow receiver's IR LED to stop

    Serial.println("Emitting");
    state = ROTATE;
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
  if (state == ESTABLISH) {
    rxDetected = true;  // filter out noises
  } else {
    rxDetected = false;
  }
}

void sendHigh(int pin, int width) {
  tone(pin, 38000);  // 38kHz carrier ON
  delay(width);
}

void sendLow(int pin, int width) {
  noTone(pin);  // carrier OFF
  delay(width);
}

void sendDigit(int digit) {
  // assume single digit
  // header: 
}
