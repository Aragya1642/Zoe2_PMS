/**
 * @file    max31855.h
 * @brief   MAX31855 Cold-Junction Compensated Thermocouple-to-Digital Converter Driver
 * @details Driver for the Maxim MAX31855 K-type thermocouple converter using
 *          the STM32 HAL SPI interface. The MAX31855 is read-only — there are
 *          no configuration registers. Each read clocks out a 32-bit frame
 *          containing thermocouple temperature, cold-junction temperature,
 *          and fault status bits.
 *
 * @note    The MAX31855 operates at 3.3V. SPI settings: CPOL=0, CPHA=0
 *          (SPI Mode 0), MSB first, clock up to 5MHz. CS is active low
 *          and directly controlled via GPIO (not the HAL NSS). The max31855
 *          continuously converts in the background at about 100ms per conversion,
 *          so if you read faster than that you'll just get the same value twice.
 *
 * @note    32-bit frame layout (MSB first):
 *          D[31:18] — 14-bit signed thermocouple temperature (0.25°C/LSB)
 *          D[17]    — Reserved (always 0)
 *          D[16]    — Fault bit (1 = fault detected)
 *          D[15:4]  — 12-bit signed cold-junction temperature (0.0625°C/LSB)
 *          D[3]     — Reserved (always 0)
 *          D[2]     — SCV fault (thermocouple shorted to VCC)
 *          D[1]     — SCG fault (thermocouple shorted to GND)
 *          D[0]     — OC fault (thermocouple open circuit)
 */

#ifndef MAX31855_H
#define MAX31855_H

#include "stm32g4xx_hal.h"

/* -----------------------------------------------------------------------
 * Fault Bit Masks
 * ----------------------------------------------------------------------- */
/**
 * @defgroup MAX31855_Faults Fault Bit Masks
 * @brief    Applied to the raw 32-bit frame to extract fault status.
 * @{
 */
#define MAX31855_FAULT_ANY  0x00010000  /**< D16: any fault present        */
#define MAX31855_FAULT_SCV  0x00000004  /**< D2:  shorted to VCC           */
#define MAX31855_FAULT_SCG  0x00000002  /**< D1:  shorted to GND           */
#define MAX31855_FAULT_OC   0x00000001  /**< D0:  open circuit (no probe)  */
/** @} */

/* -----------------------------------------------------------------------
 * Temperature Resolution
 * ----------------------------------------------------------------------- */
/**
 * @defgroup MAX31855_Resolution Temperature LSB Values
 * @{
 */
#define MAX31855_TC_LSB_C   0.25f     /**< Thermocouple: °C per LSB  */
#define MAX31855_CJ_LSB_C   0.0625f   /**< Cold junction: °C per LSB */
/** @} */

/* -----------------------------------------------------------------------
 * SPI Timeout
 * ----------------------------------------------------------------------- */
#define MAX31855_SPI_TIMEOUT  100  /**< SPI timeout in milliseconds */

/* -----------------------------------------------------------------------
 * Data Structure
 * ----------------------------------------------------------------------- */
/**
 * @brief  Parsed result from a MAX31855 read.
 */
typedef struct {
    float    tc_temp;    /**< Thermocouple temperature in °C       */
    float    cj_temp;    /**< Cold-junction temperature in °C      */
    uint8_t  fault;      /**< 0 = no fault, nonzero = fault flags  */
    uint8_t  fault_scv;  /**< 1 = thermocouple shorted to VCC      */
    uint8_t  fault_scg;  /**< 1 = thermocouple shorted to GND      */
    uint8_t  fault_oc;   /**< 1 = thermocouple open circuit        */
} MAX31855_Data;

/* -----------------------------------------------------------------------
 * Function Prototypes
 * ----------------------------------------------------------------------- */
/**
 * @brief  Read the raw 32-bit frame from the MAX31855.
 * @param  hspi     Pointer to the SPI handle.
 * @param  cs_port  GPIO port of the chip-select pin.
 * @param  cs_pin   GPIO pin number of the chip-select pin.
 * @param  raw      Pointer to store the raw 32-bit frame.
 * @retval HAL_StatusTypeDef
 *         - HAL_OK:      Transaction completed successfully.
 *         - HAL_ERROR:   SPI error.
 *         - HAL_BUSY:    SPI bus is busy.
 *         - HAL_TIMEOUT: Transaction timed out.
 */
HAL_StatusTypeDef MAX31855_ReadRaw(SPI_HandleTypeDef *hspi,
                                   GPIO_TypeDef *cs_port, uint16_t cs_pin,
                                   uint32_t *raw);

/**
 * @brief  Read and parse the thermocouple and cold-junction temperatures.
 * @param  hspi     Pointer to the SPI handle.
 * @param  cs_port  GPIO port of the chip-select pin.
 * @param  cs_pin   GPIO pin number of the chip-select pin.
 * @param  data     Pointer to a MAX31855_Data structure to populate.
 * @retval HAL_StatusTypeDef
 *         - HAL_OK:      Read successful (check data->fault for faults).
 *         - HAL_ERROR:   SPI error.
 *         - HAL_BUSY:    SPI bus is busy.
 *         - HAL_TIMEOUT: Transaction timed out.
 */
HAL_StatusTypeDef MAX31855_Read(SPI_HandleTypeDef *hspi,
                                GPIO_TypeDef *cs_port, uint16_t cs_pin,
                                MAX31855_Data *data);

/**
 * @brief  Convert the raw 32-bit frame to thermocouple temperature in °C.
 * @param  raw  Raw 32-bit frame from MAX31855_ReadRaw.
 * @return Thermocouple temperature in °C (float).
 *         Returns NAN if a fault is present (check with isnan()).
 */
float MAX31855_GetThermocoupleTemp(uint32_t raw);

/**
 * @brief  Convert the raw 32-bit frame to cold-junction temperature in °C.
 * @param  raw  Raw 32-bit frame from MAX31855_ReadRaw.
 * @return Cold-junction temperature in °C (float).
 */
float MAX31855_GetColdJunctionTemp(uint32_t raw);


#endif /* MAX31855_H */
