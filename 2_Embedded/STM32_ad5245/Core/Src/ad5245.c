/**
 * @file    ad5245.c
 * @brief   AD5245 256-Position I2C Digital Potentiometer Driver
 * @details Implementation of the AD5245 driver functions. All functions
 *          accept an I2C handle and device address, allowing support for
 *          multiple AD5245 devices on the same bus.
 *
 * @note    I2C Write Frame (from datasheet Table 10):
 *          [S] [0 1 0 1 1 0 AD0 W] [A] [X RS SD X X X X X] [A] [D7..D0] [A] [P]
 *
 *          I2C Read Frame:
 *          [S] [0 1 0 1 1 0 AD0 R] [A] [D7..D0] [NACK] [P]
 */

#include "ad5245.h"

/**
 * @brief  Set the AD5245 wiper position.
 * @param  hi2c  Pointer to the I2C handle.
 * @param  addr  Left-shifted 7-bit I2C address of the device.
 * @param  value Wiper position (0x00 = B terminal, 0xFF = A terminal).
 * @retval HAL_StatusTypeDef HAL status.
 */
HAL_StatusTypeDef AD5245_SetWiper(I2C_HandleTypeDef *hi2c, uint16_t addr, uint8_t value){
    uint8_t buf[2] = {AD5245_INSTR_NOP, value};
    return HAL_I2C_Master_Transmit(hi2c, addr, buf, 2, HAL_MAX_DELAY);
}

/**
 * @brief  Read the current AD5245 wiper position.
 * @param  hi2c  Pointer to the I2C handle.
 * @param  addr  Left-shifted 7-bit I2C address of the device.
 * @param  value Pointer to store the read wiper position.
 * @retval HAL_StatusTypeDef HAL status.
 */
HAL_StatusTypeDef AD5245_ReadWiper(I2C_HandleTypeDef *hi2c, uint16_t addr, uint8_t *value){
    return HAL_I2C_Master_Receive(hi2c, addr, value, 1, HAL_MAX_DELAY);
}

/**
 * @brief  Reset the AD5245 wiper to midscale (0x80).
 * @param  hi2c  Pointer to the I2C handle.
 * @param  addr  Left-shifted 7-bit I2C address of the device.
 * @retval HAL_StatusTypeDef HAL status.
 */
HAL_StatusTypeDef AD5245_MidscaleReset(I2C_HandleTypeDef *hi2c, uint16_t addr){
    uint8_t buf[2] = { AD5245_INSTR_RS, 0x00 };
    return HAL_I2C_Master_Transmit(hi2c, addr, buf, 2, HAL_MAX_DELAY);
}

/**
 * @brief  Enter shutdown mode (opens the A terminal connection).
 * @param  hi2c  Pointer to the I2C handle.
 * @param  addr  Left-shifted 7-bit I2C address of the device.
 * @retval HAL_StatusTypeDef HAL status.
 */
HAL_StatusTypeDef AD5245_Shutdown(I2C_HandleTypeDef *hi2c, uint16_t addr){
    uint8_t buf[2] = { AD5245_INSTR_SD, 0x00 };
    return HAL_I2C_Master_Transmit(hi2c, addr, buf, 2, HAL_MAX_DELAY);
}

/**
 * @brief  Wake from shutdown by writing a new wiper value.
 * @details Writing a normal wiper command (INSTR_NOP) clears the shutdown
 *          state and restores the potentiometer to active operation.
 * @param  hi2c  Pointer to the I2C handle.
 * @param  addr  Left-shifted 7-bit I2C address of the device.
 * @param  value Wiper position to set on wake.
 * @retval HAL_StatusTypeDef HAL status.
 */
HAL_StatusTypeDef AD5245_WakeUp(I2C_HandleTypeDef *hi2c, uint16_t addr, uint8_t value){
    uint8_t buf[2] = { AD5245_INSTR_NOP, value };
    return HAL_I2C_Master_Transmit(hi2c, addr, buf, 2, HAL_MAX_DELAY);
}
