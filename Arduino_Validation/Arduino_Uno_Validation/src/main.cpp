#include <Arduino.h>
#include <SPI.h>

/*
Pinout:
Mega 2560:
- MOSI: Pin 51
- MISO: Pin 50
- SCK: Pin 52
- CS: Pin 53

Arduino Uno:
- MOSI: Pin 11
- MISO: Pin 12
- SCK: Pin 13
- CS: Pin 10

Connect CS-CS, MOSI-MOSI, MISO-MISO, SCK-SCK between the two boards. Make sure to connect the grounds together as well.
*/

volatile char buffer[32];
volatile int bufferIndex = 0;
volatile bool newData = false;

void setup() {
  Serial.begin(9600);
  pinMode(MISO, OUTPUT);
  SPCR |= bit(SPE);       // enable SPI
  SPCR &= ~bit(CPOL);     // SPI_MODE0: clock polarity 0
  SPCR &= ~bit(CPHA);     // SPI_MODE0: clock phase 0
  SPI.attachInterrupt();
  Serial.println("Receiver ready...");
}

ISR(SPI_STC_vect) {
  char c = SPDR;
  if (bufferIndex < 31) {
    buffer[bufferIndex++] = c;
    buffer[bufferIndex] = '\0';
    newData = true;
  }
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