#include <Arduino.h>
#include <Wire.h>

/*
Pinout:
Mega - SDA: 20, SCL: 21
UNO - SDA: A4, SCL: A5
Make sure to connect grounds together and use pull-up resistors (4.7kΩ)
*/

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
  Wire.begin(0x08); // this device's address
  Wire.onReceive(receiveEvent); // register callback
  Serial.begin(9600);
  Serial.println("Receiver ready...");
}

void loop() {
  // nothing needed here, receiveEvent handles it
}