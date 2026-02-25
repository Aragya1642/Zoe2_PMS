#include <Arduino.h>
#include <SPI.h>

/*

*/

// Define SPI Pin
const int CS_PIN = 10; // Chip Select pin

void setup() {
    Serial.begin(9600);
    SPI.begin();
    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH); // Deselect the slave
}

void loop() {
    const char* message = "Hello SPI!";

    digitalWrite(CS_PIN, LOW); // Select the slave
    
}