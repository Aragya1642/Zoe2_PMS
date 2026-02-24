#include <Arduino.h>
#include <Wire.h>

/*
Pinout:
Mega - SDA: 20, SCL: 21
UNO - SDA: A4, SCL: A5
Make sure to connect grounds together and use pull-up resistors (4.7kΩ)
*/

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