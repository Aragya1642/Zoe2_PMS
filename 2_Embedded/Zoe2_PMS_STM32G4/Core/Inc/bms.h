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

/** @brief Maximum number of individual NTC temperature sensors.
 *  @note  The BMS sends 7 sensor readings per fc=0x01 frame.
 *         Frame 1 (PCnt=1) carries sensors 1–7, frame 2 (PCnt=2)
 *         carries sensors 8–14, etc.  Set this to cover the total
 *         number of NTCs in the pack.  Readings beyond this limit
 *         are silently ignored. */
#define BMS_MAX_TEMP_SENSORS    15

/** @brief Timeout in ms — if no frame is received for this long,
 *         the BMS is considered offline.
 *  @details The BMS broadcasts cell voltages every ~300 ms once awake,
 *           so 5 s of silence indicates a real failure. */
#define BMS_TIMEOUT_MS          5000U

/** @brief Recommended keepalive interval in ms.
 *  @details Send @ref BMS_SendQuery() at this rate to keep the BMS
 *           broadcasting.  The initial query acts as a wake-up. */
#define BMS_KEEPALIVE_MS        2500U
/** @} */ /* end BMS_Config */

/* ──────────────────────────── Data Structures ────────────────────────── */
/** @defgroup BMS_Structs BMS Data Structures
 *  @brief   Typed containers for every BMS response frame, plus the
 *           top-level aggregate @ref BMS_Data_t.
 *  @{
 */

/**
 * @brief  Individual cell voltages (func_code 0x00).
 * @details The BMS reports 3 cell voltages per frame.  The @c group byte
 *          (data[0]) selects which triple: group 1 -> cells 1-3, group 2 ->
 *          cells 4-6, etc.  Values are stored 0-indexed in @c cell_mv.
 */
typedef struct{
    uint16_t cell_mv[BMS_MAX_CELLS];    /**< Per-cell voltage in mV (0-indexed).          */
    uint8_t  cells_received;            /**< Number of cell readings updated this cycle.   */
} BMS_CellVoltages_t;

/**
 * @brief  NTC temperature readings (func_code 0x01).
 * @details Each frame carries up to 7 sensor readings in Data1–Data7,
 *          with Data0 as the frame group number (PCnt).  Group 1 holds
 *          sensors 1–7, group 2 holds sensors 8–14, etc.
 *          Raw byte value has a +40 °C offset (i.e. 0x28 = 0 °C).
 *          Padding bytes are 0xFF and are ignored.
 */
typedef struct {
    int8_t   temp_c[BMS_MAX_TEMP_SENSORS];  /**< Per-sensor temperature in °C (0-indexed, offset-corrected). */
    uint8_t  sensors_received;              /**< Number of sensor readings updated.                          */
} BMS_Temperatures_t;

/**
 * @brief  Pack-level summary — voltage, current, SoC (func_code 0x02).
 * @details
 *   - @c pack_voltage_V : big-endian u16 × 0.1 V/bit
 *   - @c current_A      : big-endian u16 with −3000 offset, × 0.1 A/bit
 *   - @c soc_pct        : big-endian u16 × 0.1 %/bit
 */
typedef struct {
    float    pack_voltage_V;    /**< Total pack voltage in V.                               */
    float    current_A;         /**< Pack current in A (positive = discharge, sign TBC).     */
    float    soc_pct;           /**< State of charge in % (0.0 – 100.0).                    */
    uint8_t  life_cycle;        /**< BMS life-cycle counter (data[6]).                       */
} BMS_TotalInfo0_t;

/**
 * @brief  Pack power and MOSFET temperature (func_code 0x03).
 */
typedef struct {
    uint16_t power_W;           /**< Instantaneous pack power in W (big-endian u16).        */
    int8_t   mos_temp_C;        /**< MOSFET board temperature in °C (raw − 40).             */
} BMS_TotalInfo1_t;

/**
 * @brief  Cell voltage min/max/diff statistics (func_code 0x04).
 */
typedef struct {
    uint16_t max_mv;            /**< Highest cell voltage in mV.                            */
    uint8_t  max_cell;          /**< Cell number with highest voltage (1-based).             */
    uint16_t min_mv;            /**< Lowest cell voltage in mV.                             */
    uint8_t  min_cell;          /**< Cell number with lowest voltage (1-based).              */
    uint16_t diff_mv;           /**< Difference between max and min cell voltage in mV.     */
} BMS_CellStats_t;

/**
 * @brief  Temperature sensor min/max/diff statistics (func_code 0x05).
 */
typedef struct {
    int8_t   max_temp_C;        /**< Highest temperature reading in °C (raw − 40).          */
    uint8_t  max_unit;          /**< Sensor unit number with highest reading (1-based).      */
    int8_t   min_temp_C;        /**< Lowest temperature reading in °C (raw − 40).           */
    uint8_t  min_unit;          /**< Sensor unit number with lowest reading (1-based).       */
    uint8_t  diff_C;            /**< Difference between max and min temperature in °C.       */
} BMS_TempStats_t;

/**
 * @brief  MOSFET switch states (func_code 0x06).
 * @details Each field is a boolean-like state byte reported by the BMS:
 *          0 = off/open, 1 = on/closed (verify with Aegis documentation).
 */
typedef struct {
    uint8_t  charge;            /**< Charge MOSFET state.                                    */
    uint8_t  discharge;         /**< Discharge MOSFET state.                                 */
    uint8_t  precharge;         /**< Pre-charge MOSFET state.                                */
    uint8_t  heat;              /**< Heater MOSFET state.                                    */
    uint8_t  fan;               /**< Fan MOSFET state.                                       */
} BMS_MOSStates_t;

/**
 * @brief  General status flags (func_code 0x07).
 * @warning Bit-level meanings are not fully verified — cross-check with
 *          Aegis documentation.
 */
typedef struct {
    uint8_t  bat_state;         /**< Battery state code.                                     */
    uint8_t  chg_detect;        /**< Charger detection flag.                                 */
    uint8_t  load_detect;       /**< Load detection flag.                                    */
    uint8_t  do_state;          /**< Digital output state byte.                              */
    uint8_t  di_state;          /**< Digital input state byte.                               */
} BMS_Status1_t;

/**
 * @brief  Pack configuration and capacity (func_code 0x08).
 */
typedef struct {
    uint8_t  cell_number;               /**< Number of series cells in the pack.             */
    uint8_t  ntc_number;                /**< Number of NTC temperature sensors.              */
    uint32_t remain_capacity_mAh;       /**< Remaining capacity in mAh (big-endian u32).     */
    uint16_t cycle_count;               /**< Charge/discharge cycle count (big-endian u16).   */
} BMS_Status2_t;


// TODO: Add func_code 0x09


/**
 * @brief  Cell-balancing / equilibrium state (func_code 0x0A).
 * @details Balance state bitmasks indicate which cells are actively
 *          being balanced.  Each @c bal_X_Y byte is an 8-bit mask for
 *          cells X through Y.
 */
typedef struct {
    uint8_t  balance_state;             /**< Global balancing state flag.                     */
    uint16_t balance_current_mA;        /**< Balancing current in mA (big-endian u16).        */
    uint8_t  bal_1_8;                   /**< Balance bitmask for cells 1–8.                   */
    uint8_t  bal_9_16;                  /**< Balance bitmask for cells 9–16.                  */
    uint8_t  bal_17_24;                 /**< Balance bitmask for cells 17–24.                 */
    uint8_t  bal_25_32;                 /**< Balance bitmask for cells 25–32.                 */
} BMS_Balance_t;

/**
 * @brief  Charging status and wake-up source (func_code 0x0B).
 */
typedef struct {
    uint16_t remaining_charge_min;      /**< Estimated time to full charge in minutes.        */
    uint8_t  wakeup_source;             /**< Code indicating what woke the BMS.               */
} BMS_ChargeInfo_t;


// NOTE: Did not include func_code 0x0C which was the RTC because it's not helpful information


/**
 * @brief  Current-limiting and state-of-health data (func_code 0x0D).
 */
typedef struct {
    uint8_t  limit_state;               /**< Current-limiting active flag.                    */
    uint16_t limit_current_mA;          /**< Imposed current limit in mA (big-endian u16).    */
    uint16_t soh_pct;                   /**< State of health in % (big-endian u16).            */
    uint16_t pwm_duty;                  /**< PWM duty cycle value (big-endian u16).            */
} BMS_Limiting_t;


// TODO: Add func_code 0x0E


// Note: Did not include func_code 0x0F which is the AFE data - never recieved and not sure what it is


/**
 * @brief  Top-level aggregate of all BMS telemetry.
 * @details Updated in ISR context by @ref BMS_RxCallback().  Read from
 *          main-loop context via @ref BMS_GetData().  Fields map 1:1 to
 *          the response function codes.
 *
 * @note   This struct is written from ISR context.  If you need atomic
 *         snapshots (e.g. for a CAN bridge TPDO), copy under a critical
 *         section or use a double-buffer scheme.
 */
typedef struct {
    BMS_CellVoltages_t  cells;          /**< Cell voltages        (fc 0x00). */
    BMS_Temperatures_t  temps;          /**< NTC temperatures     (fc 0x01). */
    BMS_TotalInfo0_t    total0;         /**< Pack V / I / SoC     (fc 0x02). */
    BMS_TotalInfo1_t    total1;         /**< Power / MOS temp     (fc 0x03). */
    BMS_CellStats_t     cell_stats;     /**< Cell V statistics    (fc 0x04). */
    BMS_TempStats_t     temp_stats;     /**< Temp statistics      (fc 0x05). */
    BMS_MOSStates_t     mos;            /**< MOS switch states    (fc 0x06). */
    BMS_Status1_t       status1;        /**< Status flags         (fc 0x07). */
    BMS_Status2_t       status2;        /**< Config / capacity    (fc 0x08). */
    /* fc 0x09 = hardware fault — reserved for future implementation */
    BMS_Balance_t       balance;        /**< Balancing state      (fc 0x0A). */
    BMS_ChargeInfo_t    charge_info;    /**< Charge info          (fc 0x0B). */
    /* fc 0x0C = RTC data — reserved for future implementation */
    BMS_Limiting_t      limiting;       /**< Current limit / SoH  (fc 0x0D). */
    /* fc 0x0E = faults  — reserved for future implementation */
    /* fc 0x0F = AFE data — reserved for future implementation */

    uint32_t            last_update_ms; /**< @c HAL_GetTick() timestamp of last parsed frame. */
    uint32_t            rx_total;       /**< Lifetime frame count (wraps at 2^32).             */
    bool                valid;          /**< @c true after at least one successful parse.      */
} BMS_Data_t;
/** @} */ /* end BMS_Structs */

/* ──────────────────────────── Public API ─────────────────────────────── */
/** @defgroup BMS_API BMS Public Functions
 *  @brief   Driver lifecycle: init → periodic query → read data.
 *  @{
 */

/**
 * @brief   Initialise the BMS driver.
 * @details Configures an FDCAN2 extended-ID acceptance filter for all BMS
 *          response frames (0x04XX____), sets up the TX header for the
 *          query frame, and enables the RX FIFO0 new-message interrupt.
 *
 * @pre     @c MX_FDCAN2_Init() has been called.
 * @pre     FDCAN2 @c ExtFiltersNbr >= 1 in the CubeMX configuration.
 * @post    @c HAL_FDCAN_Start() may now be called.
 *
 * @param[in] hfdcan  Pointer to the FDCAN2 peripheral handle.
 * @retval    HAL_OK  on success.
 * @retval    HAL_ERROR if @p hfdcan is NULL or filter/notification setup fails.
 */
HAL_StatusTypeDef BMS_Init(FDCAN_HandleTypeDef *hfdcan);

/**
 * @brief   Send a BMS keepalive / wake-up frame on FDCAN2.
 * @details Transmits the fixed query (ID = @ref BMS_QUERY_ID, 8 × 0x00).
 *          The first call wakes the BMS and starts its autonomous broadcast
 *          cycle.  Subsequent calls act as keepalives to prevent the BMS
 *          from going back to sleep.  Recommended interval:
 *          @ref BMS_KEEPALIVE_MS.
 *
 *          Responses are parsed automatically by @ref BMS_RxCallback()
 *          as they stream in — this is not a request/response transaction.
 *
 * @pre     @ref BMS_Init() and @c HAL_FDCAN_Start() have been called.
 *
 * @retval  HAL_OK    Frame queued successfully.
 * @retval  HAL_ERROR Driver not initialised or TX FIFO full.
 */
HAL_StatusTypeDef BMS_SendQuery(void);

/**
 * @brief   FDCAN RX FIFO0 callback entry point — do @b not call directly.
 * @details Wire this into the HAL callback in your application:
 *          @code
 *          void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan,
 *                                          uint32_t RxFifo0ITs) {
 *              BMS_RxCallback(hfdcan);
 *          }
 *          @endcode
 *          The function drains all pending frames from FIFO0 and parses
 *          each one into the internal @ref BMS_Data_t.
 *
 * @param[in] hfdcan  The FDCAN handle that triggered the interrupt.
 *                    Frames from handles other than the one passed to
 *                    @ref BMS_Init() are silently ignored.
 */
void BMS_RxCallback(FDCAN_HandleTypeDef *hfdcan);

/**
 * @brief   Get a read-only pointer to the latest BMS telemetry.
 *
 * @note    The underlying struct is written from ISR context.  For a
 *          consistent snapshot (e.g. when bridging to CANopen TPDOs),
 *          copy the struct inside a @c __disable_irq() / @c __enable_irq()
 *          critical section.
 *
 * @retval  Pointer to the internal @ref BMS_Data_t (never NULL).
 */
const BMS_Data_t *BMS_GetData(void);

/**
 * @brief   Check whether the BMS broadcast has gone silent.
 * @details Compares the current tick against @c last_update_ms.  Returns
 *          @c true if no frame has been received for longer than
 *          @ref BMS_TIMEOUT_MS.
 *
 *          Typical usage in the main loop:
 *          @code
 *          if (BMS_IsTimedOut()) {
 *              // BMS went silent — flag fault, attempt re-wake, etc.
 *          }
 *          @endcode
 *
 * @retval  true   BMS data is stale (no RX for > @ref BMS_TIMEOUT_MS).
 * @retval  false  BMS is broadcasting normally, or no data received yet
 *                 (@c valid == false).
 */
bool BMS_IsTimedOut(void);

/** @} */ /* end BMS_API */


#endif /* BMS_H */
