#include <Arduino.h>
#include <Wire.h>
#include <AD5245.h>

// AD0 pin tied to GND -> address 0x2C
// AD0 pin tied to VDD -> address 0x2D
AD5245 pot(0x2C);

void setup()
{
    Serial.begin(115200);
    while (!Serial);

    Wire.begin();

    if (!pot.begin())
    {
        Serial.println("AD5245 not found on bus!");
        while (1);
    }

    if (!pot.isConnected())
    {
        Serial.println("AD5245 not responding");
        while (1);
    }

    Serial.println("AD5245 connected");

    // reset to midscale (128)
    pot.reset();
    Serial.print("After reset: ");
    Serial.println(pot.read());
}

void loop()
{
    int testVals[5] = {0, 64, 128, 192, 255};

    for (int i = 0; i < 5; i++){
        pot.write(testVals[i]);
        Serial.print("Readback from device: ");
        Serial.print(pot.readDevice());
        Serial.print(" | Resistance Value Expected: ");
        Serial.print(testVals[i] / 255.0 * 50);
        Serial.println(" Ohms");
        delay(2000);
    }
}