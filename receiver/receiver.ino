#include<Wire.h>
#include<LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27,20, 4);
#define IDLE 0  // in idle state, wait for a burst from transmitter
#define AWAIT 1  // wait for header signal
#define SAMPLE 2

#include <LiquidCrystal_I2C.h>

const int rxPin = 2;
const int txPin = 3;

int lcdCol = 0;

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

LiquidCrystal_I2C lcd(0x27, 20, 4);

void setup() {
  // put your setup code here, to run once:
    lcd.init();                      // initialize the lcd 
  lcd.backlight();
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
  stateRx = IDLE;
  Serial.println("Press any button to start");
  lcd.setCursor(0, 0);
  lcd.print("Receiver: Initializing");


  

}

void loop() {
  if (stateRx == IDLE){
    if (receivedHigh){
      Serial.println("scanning signal received");
      printRow(1, "Status: Scanning");
      delay(waitSendTime);
      sendHigh(txPin, burstMs);
      delay(waitHeadTime);
      receivedHigh = false;
      receivedLow = false;
      stateRx = AWAIT;
      lcd.setCursor(0, 0);
      lcd.print("Scanning signal received");
    }
  }
  // put your main code here, to run repeatedly:
 else if (stateRx == AWAIT){
      //Serial.println("Awaiting for header");
      if (receivedLow){
        Serial.println("Received edge");
        delay(headerWaitTime - sampleTime/2);
        stateRx = SAMPLE;
        printRow(1, "Status: Sampling");
        
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
      if (lcdCol == 0){
         lcd.clear();
      }
      
      lcd.setCursor(0, 0);
      lcd.print("Samping Complete");
      lcd.setCursor(lcdCol, 3);
      lcd.print(readData);
      lcdCol++;
        

      Serial.println(readData);
       printRow(1, "Status: Message OK");
        char line[21];
        snprintf(line, sizeof(line), "Data: %d", readData);
        printRow(2, line);
        char bits[21];
        snprintf(bits, sizeof(bits), "Bits: %d%d%d%d",
                 (readData >> 3) & 1, (readData >> 2) & 1,
                 (readData >> 1) & 1, readData & 1);
        printRow(3, bits);

      if (readData == 15){  // quit signal
      stateRx = IDLE;
      lcdCol = 0;
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

void printRow(uint8_t row, const char *text) {
  char buf[21];
  for (uint8_t i = 0; i < 20; i++) buf[i] = ' ';
  buf[20] = '\0';
  for (uint8_t i = 0; i < 20 && text[i]; i++) buf[i] = text[i];
  lcd.setCursor(0, row);
  lcd.print(buf);
}
