// Import libraries
#include <Arduino.h>
#include <Wire.h>

void setup() {
  Serial.begin(9600);
  Wire.begin();
}

void loop() {
  Serial.print("Result: ");
  Serial.println(5);
}
