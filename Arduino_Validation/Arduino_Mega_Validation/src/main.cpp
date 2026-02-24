#include <Arduino.h>
#include <Wire.h>

void setup() {
  Wire.begin(); // primary, no address
  Serial.begin(9600);
}

void loop() {
  Wire.beginTransmission(0x08); // send to secondary
  Wire.write("Hello!");
  Wire.endTransmission();

  Serial.println("Sent: Hello!");
  delay(1000);
}