# Zoë2 Solar Power Management System

## Pinout Reference for Dev Boards

| Board             | **I2C**       |               | ‖ | **SPI**       |               |               |               | ‖ | **CAN**       |               |
|-------------------|---------------|---------------|---|---------------|---------------|---------------|---------------|---|---------------|---------------|
|                   | **SDA**       | **SCL**       | ‖ | **MISO**      | **MOSI**      | **SCLK**      | **CS**        | ‖ | **CANRX**     | **CANTX**     |
| Arduino Uno R3    | A4            | A5            | ‖ | 12            | 11            | 13            | 10            | ‖ | N/S           | N/S           |
| Arduino Mega 2560 | 20            | 21            | ‖ | 50            | 51            | 52            | 53            | ‖ | N/S           | N/S           |
| STM32L4 Nucleo    | PB7(CN7-21)   | PB6(CN10-17)  | ‖ | PC2(CN7-35)   | PC3(CN7-37)   | PB10(CN10-25) | PB15(CN10-26) | ‖ | PA11(CN10-14) | PA12(CN10-12) |
| STM32G4 Nucleo    | -             | -             | ‖ | -             | -             | -             | -             | ‖ | -             | -             |
| TEENSY 4.0        | -             | -             | ‖ | -             | -             | -             | -             | ‖ | -             | -             |

### <u>General Pinout Notes:</u>
- Ensure there is a shared ground connection
- For SPI, the CS pin can be configured to any available GPIO pin in your code, I have used PB15 for STM32L4 for convenience
- Arduinos run at 5V logic levels, while STM32 and TEENSY 4.0 run at 3.3V. Ensure using level shifters if connecting 5V devices to 3.3V boards
- CAN bus requires additional hardware (CAN transceiver) for each board

### <u>STM32 L47RG Pinout Image:</u>
![STM32L47RG Pinout](./Documentation_Assets/nucleo_l4_pinout.png)
### <u>STM32 G431RB Pinout Image:</u>
![STM32G431RB Pinout](./Documentation_Assets/nucleo_g4_pinout.png)