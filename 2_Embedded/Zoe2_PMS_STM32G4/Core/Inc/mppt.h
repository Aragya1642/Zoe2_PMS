/*
 * mppt.h
 *
 *  Created on: Apr 12, 2026
 *      Author: agoya
 */

#ifndef MPPT_H
#define MPPT_H

#include <stdint.h>
#include <stdbool.h>

/* ──────────────────────────── Configuration ──────────────────────────── */
/** @defgroup MPPT_Config MPPT Configuration Defines
 *  @brief   Compile-time constants for wiper limits and defaults.
 *  @{
 */

/** @brief Minimum safe wiper position (≈ 5 kΩ on a 50 kΩ pot). */
#define MPPT_WIPER_MIN          26U

/** @brief Maximum safe wiper position (≈ 45 kΩ on a 50 kΩ pot). */
#define MPPT_WIPER_MAX          230U

/** @brief Default wiper position on startup (mid-range). */
#define MPPT_WIPER_DEFAULT      128U

/** @brief Default P&O perturbation step size (wiper counts). */
#define MPPT_PO_STEP_DEFAULT    2U

/** @brief Default number of P&O steps between global scans.
 *  @details At a 200 ms P&O rate, 150 steps ≈ 30 seconds. */
#define MPPT_SCAN_INTERVAL_DEFAULT  150U

/** @brief Step size (wiper counts) used during a global scan sweep.
 *  @details Larger values = faster scan but lower resolution. */
#define MPPT_SCAN_STEP_DEFAULT  2U
/** @} */ /* end MPPT_Config */

/* ──────────────────────────── Data Structures ────────────────────────── */
/** @defgroup MPPT_Structs MPPT Data Structures
 *  @brief   Configuration, state machine, and runtime telemetry types.
 *  @{
 */

/**
 * @brief  MPPT algorithm state machine states.
 */
typedef enum {
    MPPT_STATE_IDLE,            /**< Not yet started (pre-init or disabled).         */
    MPPT_STATE_TRACKING,        /**< Normal P&O tracking mode.                       */
    MPPT_STATE_GLOBAL_SCAN,     /**< Sweeping the full wiper range for global MPP.   */
    MPPT_STATE_FAULT,           /**< Algorithm halted due to fault condition.         */
} MPPT_State_t;

/**
 * @brief  Runtime configuration — may be adjusted at runtime via
 *         @ref MPPT_SetConfig().
 */
typedef struct {
    uint8_t  wiper_min;         /**< Lower wiper clamp (default @ref MPPT_WIPER_MIN).        */
    uint8_t  wiper_max;         /**< Upper wiper clamp (default @ref MPPT_WIPER_MAX).        */
    uint8_t  po_step;           /**< P&O perturbation step in wiper counts.                   */
    uint8_t  scan_step;         /**< Wiper increment per global scan step.                    */
    uint16_t scan_interval;     /**< Number of P&O steps between global scans.                */
} MPPT_Config_t;

/**
 * @brief  Internal algorithm state and telemetry.
 * @details Readable via @ref MPPT_GetData() for debug logging, CAN
 *          telemetry, or dashboard display.
 */
typedef struct {
    MPPT_State_t state;         /**< Current FSM state.                                       */

    /* ── P&O tracking ── */
    uint8_t  wiper;             /**< Current wiper output position (0–255).                   */
    float    power_W;           /**< Most recent input power (V × I) in W.                    */
    float    prev_power_W;      /**< Previous input power sample in W.                        */
    float    in_voltage_V;      /**< Most recent input voltage reading in V.                  */
    float    in_current_A;      /**< Most recent input current reading in A.                  */
    float	 out_voltage_V; 	/**< Most recent output voltage reading in V.				  */
    float 	 out_current_A;		/**< Most recent output current reading in A.  			  	  */
    int8_t   direction;         /**< Current perturbation direction (+1 or −1).               */

    /* ── Global scan ── */
    uint16_t steps_since_scan;  /**< P&O step counter since last global scan.                 */
    uint8_t  scan_wiper;        /**< Wiper position during current scan sweep.                */
    float    scan_best_power_W; /**< Highest power found during current scan.                 */
    uint8_t  scan_best_wiper;   /**< Wiper position that produced @c scan_best_power_W.       */

    /* ── Statistics ── */
    uint32_t total_steps;       /**< Lifetime P&O step count (wraps at 2^32).                 */
    uint32_t total_scans;       /**< Lifetime global scan count.                              */
} MPPT_Data_t;
/** @} */ /* end MPPT_Structs */

/* ──────────────────────────── Public API ─────────────────────────────── */
/** @defgroup MPPT_API MPPT Public Functions
 *  @brief   Lifecycle: init -> periodic step -> read state.
 *  @{
 */

/**
 * @brief   Initialise the MPPT algorithm with default configuration.
 * @details Sets the wiper to @ref MPPT_WIPER_DEFAULT, clears all internal
 *          state, and transitions to @ref MPPT_STATE_TRACKING.  Call once
 *          at startup before the main control loop begins.
 *
 * @param[in] config  Pointer to a configuration struct, or NULL to use
 *                    all defaults.  The struct is copied internally.
 */
void MPPT_Init(const MPPT_Config_t *config);

/**
 * @brief   Execute one MPPT algorithm step.
 * @details In @ref MPPT_STATE_TRACKING mode this performs one P&O
 *          iteration: compute power, compare against previous, adjust
 *          wiper.  When the global scan interval elapses, the state
 *          transitions to @ref MPPT_STATE_GLOBAL_SCAN and the wiper
 *          sweeps from @c wiper_min to @c wiper_max.  Once the sweep
 *          completes, the wiper jumps to the best power point and
 *          tracking resumes.
 *
 *          The caller is responsible for:
 *          - Reading the ADS1115 and passing scaled V/I values.
 *          - Writing the returned wiper value to the AD5245.
 *
 * @param[in] v_in  Input voltage in volts (from ADS1115 AIN2, scaled).
 * @param[in] i_in  Input current in amps  (from ADS1115 AIN0, scaled).
 * @param[in] v_out Output voltage in volts (from ADS1115 AIN3, scaled).
 * @param[in] i_out Output current in amps (from ADS1115 AIN1, scaled).
 *
 * @return  New wiper position to write to the AD5245 (clamped to
 *          [@c wiper_min, @c wiper_max]).
 */
uint8_t MPPT_Step(float v_in, float i_in, float v_out, float i_out);

/**
 * @brief   Get a read-only pointer to the internal MPPT state.
 * @retval  Pointer to @ref MPPT_Data_t (never NULL).
 */
const MPPT_Data_t *MPPT_GetData(void);

/**
 * @brief   Update the runtime configuration.
 * @details Takes effect on the next call to @ref MPPT_Step().  The wiper
 *          is immediately clamped to the new [@c wiper_min, @c wiper_max]
 *          range if it falls outside.
 *
 * @param[in] config  Pointer to the new configuration (copied internally).
 */
void MPPT_SetConfig(const MPPT_Config_t *config);

/**
 * @brief   Force an immediate global scan on the next @ref MPPT_Step().
 * @details Useful after a large environmental change (e.g. panel
 *          reorientation) where the current local MPP may no longer
 *          be the global MPP.
 */
void MPPT_ForceGlobalScan(void);

/**
 * @brief   Halt the algorithm and transition to @ref MPPT_STATE_FAULT.
 * @details The wiper output freezes at its current value.  Call
 *          @ref MPPT_Init() to recover.
 */
void MPPT_Fault(void);

/** @} */ /* end MPPT_API */

#endif /* MPPT_H */
