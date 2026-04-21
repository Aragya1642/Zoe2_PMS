/**
 * @file    safety.h
 * @author  Aragya Goyal
 * @brief   Safety monitor for the Zoë2 Power Management Board
 * @version 1.0
 * @date    2026-04-21
 *
 * @details This module evaluates electrical and thermal readings against
 *          configured limits and returns a binary response that main.c
 *          acts on. It is a pure-logic module with no direct hardware
 *          access — the caller owns GPIO, I2C, SPI, and CAN operations.
 *
 * @par Response Model
 * The monitor produces exactly one of two responses:
 *   - @ref SAFETY_SAFE — all values within limits; MPPT may run normally.
 *   - @ref SAFETY_SHUTDOWN — a limit was exceeded; the boost converter
 *     must be disabled until recovery completes.
 *
 * @par Auto-Recovery
 * Once shutdown trips, the monitor stays in @ref SAFETY_SHUTDOWN until
 * the offending condition has been clear continuously for
 * @ref Safety_Config_t::recovery_ms (default 5000 ms). Any re-trip during
 * the recovery window resets the timer, preventing thrashing at the
 * limit boundary.
 *
 * @par Monitored Conditions
 * | Fault                          | Trigger                                        |
 * |--------------------------------|------------------------------------------------|
 * | @ref SAFETY_FAULT_VIN_OVER     | @c v_in > @c vin_max_V                         |
 * | @ref SAFETY_FAULT_VIN_UNDER    | @c v_in < @c vin_min_V                         |
 * | @ref SAFETY_FAULT_IIN_OVER     | @c i_in > @c iin_max_A                         |
 * | @ref SAFETY_FAULT_VOUT_OVER    | @c v_out > @c vout_max_V                       |
 * | @ref SAFETY_FAULT_IOUT_OVER    | @c i_out > @c iout_max_A                       |
 * | @ref SAFETY_FAULT_TEMP_OVER    | any @c tc_C[i] ≥ @c temp_max_C                 |
 * | @ref SAFETY_FAULT_TC_STALE     | any @c tc_last[i] older than @c tc_stale_ms    |
 * | @ref SAFETY_FAULT_BMS_LOST     | @c bms_last_tick older than @c bms_timeout_ms  |
 *
 * @par Integration Steps
 *   -# Populate a @ref Safety_Config_t — either use the @c SAFETY_DEFAULT_*
 *      macros or override with hardware-specific values.
 *   -# Call @ref Safety_Init() once during startup.
 *   -# In the main control loop (same cadence as MPPT, typically 50 ms),
 *      build a @ref Safety_Inputs_t with the latest ADC and cache values
 *      and call @ref Safety_Check().
 *   -# On @ref SAFETY_SHUTDOWN, disable the boost converter and skip the
 *      MPPT step for that cycle.
 *
 * @par Usage Example
 * @code
 *   Safety_Config_t cfg = {
 *       .vin_max_V      = SAFETY_DEFAULT_VIN_MAX_V,
 *       .vin_min_V      = SAFETY_DEFAULT_VIN_MIN_V,
 *       .iin_max_A      = SAFETY_DEFAULT_IIN_MAX_A,
 *       .vout_max_V     = SAFETY_DEFAULT_VOUT_MAX_V,
 *       .iout_max_A     = SAFETY_DEFAULT_IOUT_MAX_A,
 *       .temp_max_C     = SAFETY_DEFAULT_TEMP_MAX_C,
 *       .tc_stale_ms    = SAFETY_DEFAULT_TC_STALE_MS,
 *       .bms_timeout_ms = SAFETY_DEFAULT_BMS_TIMEOUT_MS,
 *       .recovery_ms    = SAFETY_DEFAULT_RECOVERY_MS,
 *   };
 *   Safety_Init(&cfg);
 *
 *   // in loop, every 50 ms:
 *   Safety_Inputs_t si = {
 *       .v_in = v_in, .i_in = i_in,
 *       .v_out = v_out, .i_out = i_out,
 *       .tc_C = g_tc_C, .tc_last = g_tc_last,
 *       .bms_last_tick = BMS_GetData()->last_update_ms,
 *       .bms_enabled   = BMS_GetData()->valid,
 *   };
 *   if (Safety_Check(&si) == SAFETY_SHUTDOWN) {
 *       boost_disable_all();
 *   } else {
 *       boost_master_enable(true);
 *       uint8_t wiper = MPPT_Step(v_in, i_in, v_out, i_out);
 *       AD5245_SetWiper(&hi2c2, AD5245_ADDR_AD0_LOW, wiper);
 *   }
 * @endcode
 *
 * @copyright Copyright (c) 2026 Carnegie Mellon University – Planetary Robotics Lab
 */

#ifndef SAFETY_H
#define SAFETY_H

#include <stdint.h>
#include <stdbool.h>

/* ──────────────────────────── Configuration ──────────────────────────── */
/**
 * @defgroup Safety_Defaults Safety Default Limits
 * @brief    Recommended starting values for @ref Safety_Config_t.
 * @details  Override with hardware-specific values before calling
 *           @ref Safety_Init().
 * @{
 */

/** @brief Default hard input-overvoltage limit in V. */
#define SAFETY_DEFAULT_VIN_MAX_V       40.0f

/** @brief Default input-undervoltage threshold in V. Below this the
 *         panel is considered disconnected. */
#define SAFETY_DEFAULT_VIN_MIN_V        5.0f

/** @brief Default hard input-overcurrent limit in A. */
#define SAFETY_DEFAULT_IIN_MAX_A        4.0f

/** @brief Default hard output-overvoltage limit in V.
 *  @details Sized for a typical Li-ion 80 V battery pack with headroom. */
#define SAFETY_DEFAULT_VOUT_MAX_V      80.0f

/** @brief Default hard output-overcurrent limit in A. */
#define SAFETY_DEFAULT_IOUT_MAX_A       5.5f

/** @brief Default hard overtemperature shutdown threshold in °C.
 *  @note  Any thermocouple reaching this value trips the board. */
#define SAFETY_DEFAULT_TEMP_MAX_C      85.0f

/** @brief Default TC staleness window in ms.
 *  @details Thermocouple readings older than this are treated as missing
 *           and trigger @ref SAFETY_FAULT_TC_STALE. Sized for 3× the
 *           nominal 1 Hz TC read period. */
#define SAFETY_DEFAULT_TC_STALE_MS   3000u

/** @brief Default BMS silence timeout in ms. Matches @c BMS_TIMEOUT_MS. */
#define SAFETY_DEFAULT_BMS_TIMEOUT_MS 5000u

/** @brief Default recovery dwell time in ms.
 *  @details After a shutdown trips, the board remains shut down until
 *           all conditions have been clear continuously for this long. */
#define SAFETY_DEFAULT_RECOVERY_MS   5000u

/** @} */ /* end Safety_Defaults */

/* ──────────────────────────── Data Structures ────────────────────────── */
/**
 * @defgroup Safety_Enums Safety Enumerations
 * @brief    Fault codes and response levels.
 * @{
 */

/**
 * @brief  Fault bitfield — multiple faults can be active simultaneously.
 * @details The @ref Safety_Status_t::fault_flags field is a bitwise OR of
 *          any currently-active faults. Zero means no faults.
 */
typedef enum {
    SAFETY_FAULT_NONE       = 0,            /**< No faults active.          */
    SAFETY_FAULT_VIN_OVER   = (1u << 0),    /**< Input overvoltage.          */
    SAFETY_FAULT_VIN_UNDER  = (1u << 1),    /**< Input undervoltage.         */
    SAFETY_FAULT_IIN_OVER   = (1u << 2),    /**< Input overcurrent.          */
    SAFETY_FAULT_VOUT_OVER  = (1u << 3),    /**< Output overvoltage.         */
    SAFETY_FAULT_IOUT_OVER  = (1u << 4),    /**< Output overcurrent.         */
    SAFETY_FAULT_TEMP_OVER  = (1u << 5),    /**< Any TC over @c temp_max_C. */
    SAFETY_FAULT_TC_STALE   = (1u << 6),    /**< Any TC reading too old.     */
    SAFETY_FAULT_BMS_LOST   = (1u << 7),    /**< BMS broadcast silent.       */
} Safety_Fault_t;

/**
 * @brief  Binary response returned by @ref Safety_Check().
 */
typedef enum {
    SAFETY_SAFE     = 0,    /**< All clear; caller may run MPPT normally. */
    SAFETY_SHUTDOWN = 1,    /**< Disable boost; skip MPPT this cycle.     */
} Safety_Response_t;

/** @} */ /* end Safety_Enums */

/**
 * @defgroup Safety_Structs Safety Data Structures
 * @brief    Configuration, input, and status containers.
 * @{
 */

/**
 * @brief   Tunable limits and timeouts.
 * @details Passed to @ref Safety_Init() once at startup. The struct is
 *          copied internally so the caller may pass a stack-local.
 */
typedef struct {
    /* Electrical limits */
    float vin_max_V;        /**< Hard limit on panel input voltage (V).       */
    float vin_min_V;        /**< Below this, input is disconnected (V).       */
    float iin_max_A;        /**< Hard limit on input current (A).             */
    float vout_max_V;       /**< Hard limit on output (BMS) voltage (V).      */
    float iout_max_A;       /**< Hard limit on output current (A).            */

    /* Thermal limits */
    float temp_max_C;       /**< Hard thermocouple shutdown threshold (°C).   */

    /* Timeouts */
    uint32_t tc_stale_ms;   /**< TC reading older than this is stale (ms).    */
    uint32_t bms_timeout_ms;/**< No BMS update in this long = lost (ms).      */

    /* Recovery */
    uint32_t recovery_ms;   /**< Time all values must be in-limit to recover
                                 from a shutdown (ms). */
} Safety_Config_t;

/**
 * @brief   Bundle of inputs to @ref Safety_Check().
 * @details Grouping these in a struct keeps the call site clean and makes
 *          it easy to add new inputs later without breaking the signature.
 */
typedef struct {
    float v_in;                     /**< Latest input voltage reading (V).    */
    float i_in;                     /**< Latest input current reading (A).    */
    float v_out;                    /**< Latest output voltage reading (V).   */
    float i_out;                    /**< Latest output current reading (A).   */

    const float    *tc_C;           /**< Pointer to TC cache (3 floats, °C).  */
    const uint32_t *tc_last;        /**< Pointer to TC timestamp cache
                                         (3 uint32_t, HAL tick ms).           */

    uint32_t bms_last_tick;         /**< HAL tick at last BMS update (ms).    */
    bool     bms_enabled;           /**< @c false to skip BMS timeout check.  */
} Safety_Inputs_t;

/**
 * @brief   Queryable safety state, updated on each @ref Safety_Check() call.
 * @details Read via @ref Safety_GetStatus(). Useful for CAN TPDO bridging
 *          and OLED status rendering.
 */
typedef struct {
    uint32_t          fault_flags;      /**< Bitwise OR of active
                                             @ref Safety_Fault_t bits.       */
    Safety_Response_t response;         /**< Current binary response.         */
    uint32_t          shutdown_tick;    /**< HAL tick when the most recent
                                             shutdown first tripped (0 when
                                             in @ref SAFETY_SAFE).           */
    uint32_t          clear_since_tick; /**< HAL tick when conditions last
                                             went all-clear during a recovery
                                             window (0 otherwise).            */
} Safety_Status_t;

/** @} */ /* end Safety_Structs */

/* ──────────────────────────── Public API ─────────────────────────────── */
/**
 * @defgroup Safety_API Safety Public Functions
 * @brief    Lifecycle: init → periodic check → query status.
 * @{
 */

/**
 * @brief   Initialise the safety monitor with the given configuration.
 * @details Copies @p cfg internally, clears all status, and sets the
 *          response to @ref SAFETY_SAFE. Must be called before
 *          @ref Safety_Check().
 *
 * @param[in] cfg  Pointer to configuration. May be a stack local — contents
 *                 are copied. Passing @c NULL is a no-op (module remains
 *                 uninitialised).
 */
void Safety_Init(const Safety_Config_t *cfg);

/**
 * @brief   Evaluate inputs against configured limits.
 * @details Call at the main control rate, typically alongside MPPT
 *          (e.g. every 50 ms). On each call the function:
 *            -# builds a fresh bitfield of active faults from @p in;
 *            -# updates the internal @ref Safety_Status_t;
 *            -# returns @ref SAFETY_SHUTDOWN if any fault is active OR
 *               if the module is still within the post-trip recovery
 *               window, otherwise @ref SAFETY_SAFE.
 *
 *          Recovery is automatic: when all faults clear, a timer starts
 *          counting. Once @c recovery_ms have elapsed with no re-trip,
 *          the module returns to @ref SAFETY_SAFE.
 *
 * @param[in] in  Pointer to current readings and cache pointers. Passing
 *                @c NULL returns the previous response without state change.
 *
 * @retval SAFETY_SAFE      All conditions within limits and recovery
 *                          (if any) has completed.
 * @retval SAFETY_SHUTDOWN  Either a fault is currently active, or the
 *                          recovery window has not yet elapsed.
 */
Safety_Response_t Safety_Check(const Safety_Inputs_t *in);

/**
 * @brief   Get a read-only pointer to the current safety status.
 * @details The underlying struct is updated by @ref Safety_Check(). Read
 *          at any time (including from a different thread/ISR, though all
 *          fields are single-word reads on STM32G4 so no locking needed
 *          for casual inspection).
 *
 * @retval  Pointer to the internal @ref Safety_Status_t (never NULL).
 */
const Safety_Status_t *Safety_GetStatus(void);

/**
 * @brief   Return a short text tag for the worst currently-active fault.
 * @details Intended for the OLED yellow header when the board is in
 *          @ref SAFETY_SHUTDOWN. Priority ordering (most urgent first):
 *          @c OT → @c OV:OUT → @c OV:IN → @c OC:OUT → @c OC:IN →
 *          @c BMS → @c TC? → @c UV:IN. If no faults are active but the
 *          module is still in the recovery window, returns @c "WAIT".
 *          Returns an empty string when @ref SAFETY_SAFE.
 *
 * @retval  Static, null-terminated string of at most 6 characters.
 */
const char *Safety_FaultTag(void);

/** @} */ /* end Safety_API */

#endif /* SAFETY_H */
