/*
 * bms.h
 *
 *  Created on: Apr 12, 2026
 *      Author: agoya
 */

#ifndef BMS_H
#define BMS_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* ──────────────────────────── Configuration ──────────────────────────── */
/** @defgroup BMS_Config BMS Configuration Defines
 *  @brief   Compile-time constants for the Aegis BMS protocol.
 *  @{
 */

/** @brief Extended CAN ID used to query the BMS for a full telemetry dump. */
#define BMS_QUERY_ID            0x0400FF80U

/** @brief Maximum number of series cells the BMS may report.
 *  @note  The Aegis BMS sends 3 cells per fc=0x00 frame; groups beyond
 *         this limit are silently ignored. */
#define BMS_MAX_CELLS           20

/** @brief Maximum number of temperature sensor groups.
 *  @note  Each group carries two NTC readings.  Increase if the pack
 *         has more than 8 groups. */
#define BMS_MAX_TEMP_GROUPS     8
/** @} */ /* end BMS_Config */






#endif /* BMS_H */
