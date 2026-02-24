// Import libraries
#include <Arduino.h>
#include <Wire.h>

// put function declarations here:
int myFunction(int, int);

void setup() {
  Serial.begin(9600);
  Wire.begin();
}

void loop() {
  int result = myFunction(2, 3);
  Serial.print("Result: ");
  Serial.println(result);
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}