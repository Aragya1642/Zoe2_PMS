/**
 * @file    ad5245.h
 * @brief   AD5245 256-Position I2C Digital Potentiometer Driver
 * @details Driver for the Analog Devices AD5245 digital potentiometer
 *          using the STM32 HAL I2C interface. Supports wiper read/write,
 *          midscale reset, and shutdown operations.
 *
 * @note    The AD5245 uses 7-bit I2C addressing. The base address is 0x2C
 *          (AD0 pin low) or 0x2D (AD0 pin high). The address is left-shifted
 *          by 1 for the STM32 HAL, which handles the R/W bit internally.
 *
 * @note    Instruction byte format:
 *          Bit 7: Don't care
 *          Bit 6: RS (Midscale Reset)
 *          Bit 5: SD (Shutdown)
 *          Bits 4-0: Don't care
 */

#ifndef AD5245_H
#define AD5245_H

#include "stm32g4xx_hal.h"

/**
 * @defgroup AD5245_Addresses I2C Device Addresses
 * @brief    Left-shifted 7-bit addresses for STM32 HAL compatibility.
 * @{
 */
#define AD5245_ADDR_AD0_LOW   (0x2C << 1)  /**< AD0 pin tied to GND */
#define AD5245_ADDR_AD0_HIGH  (0x2D << 1)  /**< AD0 pin tied to VDD */
/** @} */

/**
 * @defgroup AD5245_Instructions Instruction Byte Definitions
 * @brief    Control bits for the instruction byte (bits 6 and 5).
 * @{
 */
#define AD5245_INSTR_NOP  0x00  /**< No operation, normal wiper write */
#define AD5245_INSTR_RS   0x40  /**< Midscale reset (wiper -> 0x80)  */
#define AD5245_INSTR_SD   0x20  /**< Shutdown mode (open A terminal) */
/** @} */

/**
 * @defgroup AD5245_Wiper Wiper Position Constants
 * @{
 */
#define AD5245_WIPER_MIN       0x00  /**< Wiper at B terminal */
#define AD5245_WIPER_MIDSCALE  0x80  /**< Wiper at midscale   */
#define AD5245_WIPER_MAX       0xFF  /**< Wiper at A terminal */
/** @} */

/**
 * @brief  Set the AD5245 wiper position.
 * @param  hi2c  Pointer to the I2C handle.
 * @param  addr  Left-shifted 7-bit I2C address of the device.
 * @param  value Wiper position (0x00 = B terminal, 0xFF = A terminal).
 * @retval HAL_StatusTypeDef
 *         - HAL_OK:      Transaction completed successfully.
 *         - HAL_ERROR:    Device did not acknowledge (wrong address or not connected).
 *         - HAL_BUSY:     I2C bus is busy.
 *         - HAL_TIMEOUT:  Transaction timed out.
 */
HAL_StatusTypeDef AD5245_SetWiper(I2C_HandleTypeDef *hi2c, uint16_t addr, uint8_t value);

/**
 * @brief  Read the current AD5245 wiper position.
 * @param  hi2c  Pointer to the I2C handle.
 * @param  addr  Left-shifted 7-bit I2C address of the device.
 * @param  value Pointer to store the read wiper position.
 * @retval HAL_StatusTypeDef
 *         - HAL_OK:      Transaction completed successfully.
 *         - HAL_ERROR:    Device did not acknowledge.
 *         - HAL_BUSY:     I2C bus is busy.
 *         - HAL_TIMEOUT:  Transaction timed out.
 */
HAL_StatusTypeDef AD5245_ReadWiper(I2C_HandleTypeDef *hi2c, uint16_t addr, uint8_t *value);

/**
 * @brief  Reset the AD5245 wiper to midscale (0x80).
 * @param  hi2c  Pointer to the I2C handle.
 * @param  addr  Left-shifted 7-bit I2C address of the device.
 * @retval HAL_StatusTypeDef
 *         - HAL_OK:      Transaction completed successfully.
 *         - HAL_ERROR:    Device did not acknowledge.
 *         - HAL_BUSY:     I2C bus is busy.
 *         - HAL_TIMEOUT:  Transaction timed out.
 */
HAL_StatusTypeDef AD5245_MidscaleReset(I2C_HandleTypeDef *hi2c, uint16_t addr);

/**
 * @brief  Enter shutdown mode (opens the A terminal connection).
 * @param  hi2c  Pointer to the I2C handle.
 * @param  addr  Left-shifted 7-bit I2C address of the device.
 * @retval HAL_StatusTypeDef
 *         - HAL_OK:      Transaction completed successfully.
 *         - HAL_ERROR:    Device did not acknowledge.
 *         - HAL_BUSY:     I2C bus is busy.
 *         - HAL_TIMEOUT:  Transaction timed out.
 */
HAL_StatusTypeDef AD5245_Shutdown(I2C_HandleTypeDef *hi2c, uint16_t addr);

/**
 * @brief  Wake from shutdown by writing a wiper value.
 * @param  hi2c  Pointer to the I2C handle.
 * @param  addr  Left-shifted 7-bit I2C address of the device.
 * @param  value Wiper position to set on wake.
 * @retval HAL_StatusTypeDef
 *         - HAL_OK:      Transaction completed successfully.
 *         - HAL_ERROR:    Device did not acknowledge.
 *         - HAL_BUSY:     I2C bus is busy.
 *         - HAL_TIMEOUT:  Transaction timed out.
 */
HAL_StatusTypeDef AD5245_WakeUp(I2C_HandleTypeDef *hi2c, uint16_t addr, uint8_t value);

#endif /* AD5245_H */
