/*
 * ads1115.c
 *
 *  Created on: Mar 25, 2026
 *      Author: agoya
 */

#include "ads1115.h"

/* -----------------------------------------------------------------------
 * Internal Helpers
 * ----------------------------------------------------------------------- */

/**
 * @brief  Pack an ADS1115_Config struct into a 16-bit config register value.
 *         Comparator is disabled by default (COMP_QUE = 11).
 */
static uint16_t ADS1115_PackConfig(const ADS1115_Config *config){
    return (config->mux       |
            config->pga       |
            config->mode      |
            config->dr        |
            config->comp_mode |
            config->comp_pol  |
            config->comp_lat  |
            config->comp_que);
}


/* -----------------------------------------------------------------------
 * Initialization
 * ----------------------------------------------------------------------- */
HAL_StatusTypeDef ADS1115_Init(I2C_HandleTypeDef *hi2c, uint16_t addr,
                               const ADS1115_Config *config){
    uint16_t cfg = ADS1115_PackConfig(config);
    return ADS1115_WriteRegister(hi2c, addr, ADS1115_REG_CONFIG, cfg);
}

/* -----------------------------------------------------------------------
 * Register Access
 * ----------------------------------------------------------------------- */
HAL_StatusTypeDef ADS1115_WriteRegister(I2C_HandleTypeDef *hi2c, uint16_t addr,
                                        uint8_t reg, uint16_t value){
    uint8_t buf[2];
    buf[0] = (uint8_t)(value >> 8);    /* MSB first */
    buf[1] = (uint8_t)(value & 0xFF);  /* LSB       */

    return HAL_I2C_Mem_Write(hi2c, addr, reg, I2C_MEMADD_SIZE_8BIT,
                             buf, 2, ADS1115_I2C_TIMEOUT);
}

HAL_StatusTypeDef ADS1115_ReadRegister(I2C_HandleTypeDef *hi2c, uint16_t addr,
                                       uint8_t reg, uint16_t *value){
    uint8_t buf[2];
    HAL_StatusTypeDef status;

    status = HAL_I2C_Mem_Read(hi2c, addr, reg, I2C_MEMADD_SIZE_8BIT,
                              buf, 2, ADS1115_I2C_TIMEOUT);

    if (status == HAL_OK) {
        *value = ((uint16_t)buf[0] << 8) | buf[1];  /* MSB first */
    }

    return status;
}

/* -----------------------------------------------------------------------
 * Channel Selection
 * ----------------------------------------------------------------------- */
HAL_StatusTypeDef ADS1115_SetChannel(I2C_HandleTypeDef *hi2c, uint16_t addr,
                                     ADS1115_Config *config, uint16_t mux){
    config->mux = mux;
    uint16_t cfg = ADS1115_PackConfig(config);
    return ADS1115_WriteRegister(hi2c, addr, ADS1115_REG_CONFIG, cfg);
}

/* -----------------------------------------------------------------------
 * Conversion Reads
 * ----------------------------------------------------------------------- */
HAL_StatusTypeDef ADS1115_ReadConversion(I2C_HandleTypeDef *hi2c, uint16_t addr,
                                         int16_t *result){
    uint16_t raw;
    HAL_StatusTypeDef status;

    status = ADS1115_ReadRegister(hi2c, addr, ADS1115_REG_CONVERSION, &raw);

    if (status == HAL_OK) {
        *result = (int16_t)raw;  /* Two's complement, sign is preserved */
    }

    return status;
}

HAL_StatusTypeDef ADS1115_ReadSingleShot(I2C_HandleTypeDef *hi2c, uint16_t addr,
                                         const ADS1115_Config *config, int16_t *result){
    HAL_StatusTypeDef status;
    uint16_t cfg;
    uint16_t poll;

    /* Build config word with OS bit set to trigger conversion */
    cfg = ADS1115_PackConfig(config) | ADS1115_OS_BEGIN;

    /* Write config to start conversion */
    status = ADS1115_WriteRegister(hi2c, addr, ADS1115_REG_CONFIG, cfg);
    if (status != HAL_OK) {
        return status;
    }

    /* Poll the OS bit until conversion is complete.
     * OS bit reads 1 when device is idle (conversion done).
     * At 8 SPS (slowest rate), conversion takes ~125ms.
     * 200 iterations at 1ms each gives generous margin. */
    uint16_t timeout = 200;

    do {
        HAL_Delay(1);  /* Avoid hammering the bus */
        status = ADS1115_ReadRegister(hi2c, addr, ADS1115_REG_CONFIG, &poll);
        if (status != HAL_OK) {
            return status;
        }
        if (--timeout == 0) {
            return HAL_TIMEOUT;
        }
    } while ((poll & ADS1115_OS_READY) == 0);

    /* Read the conversion result */
    return ADS1115_ReadConversion(hi2c, addr, result);
}

/* -----------------------------------------------------------------------
 * Voltage Conversion Utility
 * ----------------------------------------------------------------------- */
float ADS1115_ConvertToMillivolts(int16_t raw, uint16_t pga){
    float lsb_mv;

    switch (pga) {
        case ADS1115_PGA_6_144V: lsb_mv = 0.1875f;   break;
        case ADS1115_PGA_4_096V: lsb_mv = 0.125f;     break;
        case ADS1115_PGA_2_048V: lsb_mv = 0.0625f;    break;
        case ADS1115_PGA_1_024V: lsb_mv = 0.03125f;   break;
        case ADS1115_PGA_0_512V: lsb_mv = 0.015625f;  break;
        case ADS1115_PGA_0_256V: lsb_mv = 0.0078125f; break;
        default:                 lsb_mv = 0.0625f;     break;  /* Default to 2.048V */
    }

    return (float)raw * lsb_mv;
}
