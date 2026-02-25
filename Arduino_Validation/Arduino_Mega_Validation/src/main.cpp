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

// Define SPI Pin
const int CS_PIN = 53; // Chip Select pin

void spiSend(const char* data);

void setup() {
    Serial.begin(9600);
    SPI.begin();
    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH); // Deselect the slave
}

void loop() {
    const char* message = "Hello SPI!";
    spiSend(message); // Send the message over SPI
    Serial.println("Message sent: " + String(message));
    delay(1000); // Wait before sending the next message
}

void spiSend(const char* data) {
    SPI.beginTransaction(SPISettings(250000, MSBFIRST, SPI_MODE0));
    digitalWrite(CS_PIN, LOW);
    while (*data) {
        SPI.transfer(*data++);
    }
    digitalWrite(CS_PIN, HIGH);
    SPI.endTransaction();
}