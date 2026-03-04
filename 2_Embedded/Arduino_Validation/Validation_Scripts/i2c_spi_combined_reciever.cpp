#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

volatile char buffer[32];
volatile int bufferIndex = 0;
volatile bool newData = false;

ISR(SPI_STC_vect) {
  char c = SPDR;
  if (bufferIndex < 31) {
    buffer[bufferIndex++] = c;
    buffer[bufferIndex] = '\0';
    newData = true;
  }
}

void receiveEvent(int numBytes) {
  String message = "";
  while (Wire.available()) {
    char c = Wire.read();
    message += c;
  }
  Serial.print("Received: ");
  Serial.println(message);
}

void setup() {
  Serial.begin(9600);
  
  // I2C setup
  Wire.begin(0x08); // this device's address
  Wire.onReceive(receiveEvent); // register callback

  // SPI setup
  pinMode(MISO, OUTPUT);
  SPCR |= bit(SPE);       // enable SPI
  SPCR &= ~bit(CPOL);     // SPI_MODE0: clock polarity 0
  SPCR &= ~bit(CPHA);     // SPI_MODE0: clock phase 0
  SPI.attachInterrupt();
  Serial.println("Receiver ready...");
}

void loop() {
  if (newData) {
    delay(10);
    Serial.print("Received: ");
    Serial.println((char*)buffer);
    bufferIndex = 0;
    newData = false;
  }
}