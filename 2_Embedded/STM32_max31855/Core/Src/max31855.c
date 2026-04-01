/*
 * max31855.c
 *
 *  Created on: Apr 1, 2026
 *      Author: agoya
 */

#include "max31855.h"
#include <math.h>

/* -----------------------------------------------------------------------
 * Raw SPI Read
 * ----------------------------------------------------------------------- */
HAL_StatusTypeDef MAX31855_ReadRaw(SPI_HandleTypeDef *hspi,
                                   GPIO_TypeDef *cs_port, uint16_t cs_pin,
                                   uint32_t *raw){
    uint8_t buf[4] = {0};
    HAL_StatusTypeDef status;

    /* Pull CS low to start the read */
    HAL_GPIO_WritePin(cs_port, cs_pin, GPIO_PIN_RESET);

    /* Clock out 32 bits (4 bytes). MOSI data is don't-care. */
    status = HAL_SPI_Receive(hspi, buf, 4, MAX31855_SPI_TIMEOUT);

    /* Release CS */
    HAL_GPIO_WritePin(cs_port, cs_pin, GPIO_PIN_SET);

    if (status == HAL_OK) {
        /* Reassemble 4 bytes into 32-bit frame, MSB first */
        *raw = ((uint32_t)buf[0] << 24) |
               ((uint32_t)buf[1] << 16) |
               ((uint32_t)buf[2] << 8)  |
               ((uint32_t)buf[3]);
    }

    return status;
}

/* -----------------------------------------------------------------------
 * Temperature Extraction
 * ----------------------------------------------------------------------- */
float MAX31855_GetThermocoupleTemp(uint32_t raw)
{
    /* Check for fault */
    if (raw & MAX31855_FAULT_ANY) {
        return NAN;
    }

    /* D[31:18] = 14-bit signed thermocouple temperature.
     * Shift right by 18 to get the 14-bit value. */
    int32_t tc_raw = (int32_t)(raw >> 18);

    /* Sign-extend from 14-bit to 32-bit */
    if (tc_raw & 0x2000) {
        tc_raw |= 0xFFFFC000;
    }

    return (float)tc_raw * MAX31855_TC_LSB_C;
}

float MAX31855_GetColdJunctionTemp(uint32_t raw)
{
    /* D[15:4] = 12-bit signed cold-junction temperature.
     * Shift right by 4 to get the 12-bit value. */
    int32_t cj_raw = (int32_t)((raw >> 4) & 0x0FFF);

    /* Sign-extend from 12-bit to 32-bit */
    if (cj_raw & 0x0800) {
        cj_raw |= 0xFFFFF000;
    }

    return (float)cj_raw * MAX31855_CJ_LSB_C;
}

/* -----------------------------------------------------------------------
 * Combined Read + Parse
 * ----------------------------------------------------------------------- */
HAL_StatusTypeDef MAX31855_Read(SPI_HandleTypeDef *hspi,
                                GPIO_TypeDef *cs_port, uint16_t cs_pin,
                                MAX31855_Data *data)
{
    uint32_t raw;
    HAL_StatusTypeDef status;

    status = MAX31855_ReadRaw(hspi, cs_port, cs_pin, &raw);
    if (status != HAL_OK) {
        return status;
    }

    /* Parse fault bits */
    data->fault     = (raw & MAX31855_FAULT_ANY) ? 1 : 0;
    data->fault_scv = (raw & MAX31855_FAULT_SCV) ? 1 : 0;
    data->fault_scg = (raw & MAX31855_FAULT_SCG) ? 1 : 0;
    data->fault_oc  = (raw & MAX31855_FAULT_OC)  ? 1 : 0;

    /* Parse temperatures */
    data->tc_temp = MAX31855_GetThermocoupleTemp(raw);
    data->cj_temp = MAX31855_GetColdJunctionTemp(raw);

    return HAL_OK;
}
