/*
 * ads1115.h
 *
 *  Created on: Mar 25, 2026
 *      Author: agoya
 */

#ifndef ADS1115_H
#define ADS1115_H

#include "stm32l4xx_hal.h"

/* -----------------------------------------------------------------------
 * I2C Device Addresses (left-shifted for STM32 HAL)
 * ----------------------------------------------------------------------- */
/**
 * @defgroup ADS1115_Addresses I2C Device Addresses
 * @brief    Left-shifted 7-bit addresses for STM32 HAL compatibility.
 * @{
 */
#define ADS1115_ADDR_GND	(0x48 << 1)
#define ADS1115_ADDR_VDD	(0x49 << 1)
#define ADS1115_ADDR_SDA	(0x4A << 1)
#define ADS1115_ADDR_SCL	(0x4B << 1)
/** @} */

/* -----------------------------------------------------------------------
 * Register Pointer Addresses
 * ----------------------------------------------------------------------- */
/**
 * @defgroup ADS1115_Registers Register Pointer Addresses
 * @brief    8-bit pointer register values used to select the target register.
 * @{
 */
#define ADS1115_REG_CONVERSION  0x00  /**< Conversion result (read-only)  */
#define ADS1115_REG_CONFIG      0x01  /**< Configuration (read/write)     */
#define ADS1115_REG_LO_THRESH   0x02  /**< Low threshold (read/write)     */
#define ADS1115_REG_HI_THRESH   0x03  /**< High threshold (read/write)    */
/** @} */

/* -----------------------------------------------------------------------
 * Config Register Bit Definitions
 * ----------------------------------------------------------------------- */
/**
 * @defgroup ADS1115_OS Operational Status / Single-Shot (Bit 15)
 * @brief    Writing 1 begins a single-shot conversion (when in power-down).
 *           Reading 1 means no conversion is in progress.
 * @{
 */
#define ADS1115_OS_BEGIN    0x8000  /**< Write: start single-shot conversion */
#define ADS1115_OS_BUSY     0x0000  /**< Read: conversion in progress        */
#define ADS1115_OS_READY    0x8000  /**< Read: conversion complete            */
/** @} */

/**
 * @defgroup ADS1115_MUX Input Multiplexer Configuration (Bits 14:12)
 * @brief    Selects which analog inputs are connected to the ADC.
 *           Differential pairs or single-ended (referenced to GND).
 * @{
 */
#define ADS1115_MUX_DIFF_01  0x0000  /**< AIN0-AIN1 (default) */
#define ADS1115_MUX_DIFF_03  0x1000  /**< AIN0-AIN3           */
#define ADS1115_MUX_DIFF_13  0x2000  /**< AIN1-AIN3           */
#define ADS1115_MUX_DIFF_23  0x3000  /**< AIN2-AIN3           */
#define ADS1115_MUX_SINGLE_0 0x4000  /**< AIN0 vs GND         */
#define ADS1115_MUX_SINGLE_1 0x5000  /**< AIN1 vs GND         */
#define ADS1115_MUX_SINGLE_2 0x6000  /**< AIN2 vs GND         */
#define ADS1115_MUX_SINGLE_3 0x7000  /**< AIN3 vs GND         */
/** @} */

/**
 * @defgroup ADS1115_PGA Programmable Gain Amplifier (Bits 11:9)
 * @brief    Sets the full-scale range of the ADC input.
 *           Note: Actual input must never exceed VDD + 0.3V regardless
 *           of the PGA setting.
 * @{
 */
#define ADS1115_PGA_6_144V  0x0000  /**< +/-6.144V (LSB = 187.5uV)  */
#define ADS1115_PGA_4_096V  0x0200  /**< +/-4.096V (LSB = 125uV)    */
#define ADS1115_PGA_2_048V  0x0400  /**< +/-2.048V (LSB = 62.5uV) (default) */
#define ADS1115_PGA_1_024V  0x0600  /**< +/-1.024V (LSB = 31.25uV)  */
#define ADS1115_PGA_0_512V  0x0800  /**< +/-0.512V (LSB = 15.625uV) */
#define ADS1115_PGA_0_256V  0x0A00  /**< +/-0.256V (LSB = 7.8125uV) */
/** @} */

/**
 * @defgroup ADS1115_MODE Operating Mode (Bit 8)
 * @brief    Continuous conversion or single-shot with auto power-down.
 * @{
 */
#define ADS1115_MODE_CONTINUOUS  0x0000  /**< Continuous conversion mode         */
#define ADS1115_MODE_SINGLE      0x0100  /**< Single-shot + power-down (default) */
/** @} */

/**
 * @defgroup ADS1115_DR Data Rate (Bits 7:5)
 * @brief    Conversion rate in samples per second (SPS).
 * @{
 */
#define ADS1115_DR_8SPS    0x0000  /**<   8 SPS */
#define ADS1115_DR_16SPS   0x0020  /**<  16 SPS */
#define ADS1115_DR_32SPS   0x0040  /**<  32 SPS */
#define ADS1115_DR_64SPS   0x0060  /**<  64 SPS */
#define ADS1115_DR_128SPS  0x0080  /**< 128 SPS (default) */
#define ADS1115_DR_250SPS  0x00A0  /**< 250 SPS */
#define ADS1115_DR_475SPS  0x00C0  /**< 475 SPS */
#define ADS1115_DR_860SPS  0x00E0  /**< 860 SPS */
/** @} */

/**
 * @defgroup ADS1115_COMP Comparator Configuration (Bits 4:0)
 * @brief    Comparator mode, polarity, latching, and queue settings.
 *           Default configuration disables the comparator (COMP_QUE = 11).
 * @{
 */
#define ADS1115_COMP_MODE_TRAD    0x0000  /**< Traditional comparator (default)*/
#define ADS1115_COMP_MODE_WINDOW  0x0010  /**< Window comparator         */

#define ADS1115_COMP_POL_LOW      0x0000  /**< ALERT/RDY active low (default)*/
#define ADS1115_COMP_POL_HIGH     0x0008  /**< ALERT/RDY active high     */

#define ADS1115_COMP_LAT_OFF      0x0000  /**< Non-latching (default)    */
#define ADS1115_COMP_LAT_ON       0x0004  /**< Latching comparator       */

#define ADS1115_COMP_QUE_1        0x0000  /**< Assert after 1 conversion */
#define ADS1115_COMP_QUE_2        0x0001  /**< Assert after 2 conversions*/
#define ADS1115_COMP_QUE_4        0x0002  /**< Assert after 4 conversions*/
#define ADS1115_COMP_QUE_DISABLE  0x0003  /**< Disable comparator (default) */
/** @} */

/* -----------------------------------------------------------------------
 * I2C Timeout
 * ----------------------------------------------------------------------- */
#define ADS1115_I2C_TIMEOUT  100  /**< I2C timeout in milliseconds */

/* -----------------------------------------------------------------------
 * Configuration Helper Struct
 * ----------------------------------------------------------------------- */
/**
 * @brief  Configuration structure for readable ADC setup.
 * @note   Each field should be set using the corresponding defines above.
 *         The driver packs these into a single 16-bit config register value.
 */
typedef struct {
	uint16_t mux;       /**< Input multiplexer (ADS1115_MUX_xxx)       */
	uint16_t pga;       /**< Gain / full-scale range (ADS1115_PGA_xxx) */
	uint16_t mode;      /**< Operating mode (ADS1115_MODE_xxx)         */
	uint16_t dr;        /**< Data rate (ADS1115_DR_xxx)                */
	uint16_t comp_mode; /**< Comparator mode (ADS1115_COMP_MODE_xxx)   */
	uint16_t comp_pol;  /**< Comparator polarity (ADS1115_COMP_POL_xxx)*/
	uint16_t comp_lat;  /**< Comparator latching (ADS1115_COMP_LAT_xxx)*/
	uint16_t comp_que;  /**< Comparator queue (ADS1115_COMP_QUE_xxx)   */
} ADS1115_Config;


/* -----------------------------------------------------------------------
 * Function Prototypes
 * ----------------------------------------------------------------------- */
/**
 * @brief  Write a 16-bit value to an ADS1115 register.
 * @param  hi2c  Pointer to the I2C handle.
 * @param  addr  Left-shifted 7-bit I2C address of the device.
 * @param  reg   Register pointer address (ADS1115_REG_xxx).
 * @param  value 16-bit value to write (sent MSB first).
 * @retval HAL_StatusTypeDef
 *         - HAL_OK:      Transaction completed successfully.
 *         - HAL_ERROR:   Device did not acknowledge.
 *         - HAL_BUSY:    I2C bus is busy.
 *         - HAL_TIMEOUT: Transaction timed out.
 */
HAL_StatusTypeDef ADS1115_WriteRegister(I2C_HandleTypeDef *hi2c, uint16_t addr,
                                        uint8_t reg, uint16_t value);

/**
 * @brief  Read a 16-bit value from an ADS1115 register.
 * @param  hi2c  Pointer to the I2C handle.
 * @param  addr  Left-shifted 7-bit I2C address of the device.
 * @param  reg   Register pointer address (ADS1115_REG_xxx).
 * @param  value Pointer to store the 16-bit register value.
 * @retval HAL_StatusTypeDef
 */
HAL_StatusTypeDef ADS1115_ReadRegister(I2C_HandleTypeDef *hi2c, uint16_t addr,
                                       uint8_t reg, uint16_t *value);

/**
 * @brief  Initialize the ADS1115 by writing the config register.
 * @param  hi2c   Pointer to the I2C handle.
 * @param  addr   Left-shifted 7-bit I2C address of the device.
 * @param  config Pointer to a configuration structure.
 * @retval HAL_StatusTypeDef
 */
HAL_StatusTypeDef ADS1115_Init(I2C_HandleTypeDef *hi2c, uint16_t addr,
                               const ADS1115_Config *config);

/**
 * @brief  Trigger a single-shot conversion, poll for completion, and
 *         return the raw 16-bit result.
 * @param  hi2c    Pointer to the I2C handle.
 * @param  addr    Left-shifted 7-bit I2C address of the device.
 * @param  config  Pointer to a configuration structure (must use MODE_SINGLE).
 * @param  result  Pointer to store the raw signed 16-bit conversion result.
 * @retval HAL_StatusTypeDef
 */
HAL_StatusTypeDef ADS1115_ReadSingleShot(I2C_HandleTypeDef *hi2c, uint16_t addr,
                                         const ADS1115_Config *config, int16_t *result);

/**
 * @brief  Read the latest conversion result from the conversion register.
 * @note   In continuous mode, this returns the most recent sample.
 *         In single-shot mode, this returns the last completed conversion.
 * @param  hi2c   Pointer to the I2C handle.
 * @param  addr   Left-shifted 7-bit I2C address of the device.
 * @param  result Pointer to store the raw signed 16-bit conversion result.
 * @retval HAL_StatusTypeDef
 */
HAL_StatusTypeDef ADS1115_ReadConversion(I2C_HandleTypeDef *hi2c, uint16_t addr,
                                         int16_t *result);

/**
 * @brief  Change the input multiplexer channel without modifying other
 *         config settings. Writes the updated config register.
 * @param  hi2c   Pointer to the I2C handle.
 * @param  addr   Left-shifted 7-bit I2C address of the device.
 * @param  config Pointer to the config structure (mux field will be updated).
 * @param  mux    New multiplexer setting (ADS1115_MUX_xxx).
 * @retval HAL_StatusTypeDef
 */
HAL_StatusTypeDef ADS1115_SetChannel(I2C_HandleTypeDef *hi2c, uint16_t addr,
                                     ADS1115_Config *config, uint16_t mux);

/**
 * @brief  Convert a raw ADC reading to voltage in millivolts.
 * @param  raw  Signed 16-bit ADC result.
 * @param  pga  PGA setting used for the conversion (ADS1115_PGA_xxx).
 * @return Voltage in millivolts (float).
 */
float ADS1115_ConvertToMillivolts(int16_t raw, uint16_t pga);

#endif /* ADS1115_H */
