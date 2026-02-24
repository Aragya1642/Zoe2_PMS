#include <Arduino.h>
#include <Wire.h>

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