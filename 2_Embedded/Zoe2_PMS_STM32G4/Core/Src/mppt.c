/*
 * mppt.c
 *
 *  Created on: Apr 12, 2026
 *      Author: agoya
 */

#include "mppt.h"
#include <string.h>

/* ──────────────────────────── Private State ──────────────────────────── */
/** @brief Runtime configuration (copied from caller or defaults). */
static MPPT_Config_t mppt_cfg;

/** @brief Internal algorithm state and telemetry. */
static MPPT_Data_t mppt_data;

/* ──────────────────────────── Helpers ────────────────────────────────── */
/**
 * @brief   Clamp a wiper value to the configured safe range.
 * @param[in] val  Unclamped wiper value.
 * @return  Value clamped to [@c wiper_min, @c wiper_max].
 */
static inline uint8_t clamp_wiper(int16_t val){
    if (val < (int16_t)mppt_cfg.wiper_min) return mppt_cfg.wiper_min;
    if (val > (int16_t)mppt_cfg.wiper_max) return mppt_cfg.wiper_max;
    return (uint8_t)val;
}

/* ──────────────────────────── Init ──────────────────────────────────── */
void MPPT_Init(const MPPT_Config_t *config){
    /* Apply configuration (caller-supplied or defaults) */
    if (config != NULL) {
        mppt_cfg = *config;
    } else {
        mppt_cfg.wiper_min     = MPPT_WIPER_MIN;
        mppt_cfg.wiper_max     = MPPT_WIPER_MAX;
        mppt_cfg.po_step       = MPPT_PO_STEP_DEFAULT;
        mppt_cfg.scan_step     = MPPT_SCAN_STEP_DEFAULT;
        mppt_cfg.scan_interval = MPPT_SCAN_INTERVAL_DEFAULT;
    }

    /* Zero all runtime state */
    memset(&mppt_data, 0, sizeof(mppt_data));

    /* Start at mid-range with a positive perturbation direction */
    mppt_data.wiper     = clamp_wiper(MPPT_WIPER_DEFAULT);
    mppt_data.direction = +1;
    mppt_data.state     = MPPT_STATE_TRACKING;
}

/* ──────────────────────────── Core Algorithm ─────────────────────────── */
/**
 * @brief   Execute one P&O tracking iteration.
 * @details Compares current power against previous power:
 *          - If power increased → keep perturbation direction.
 *          - If power decreased → reverse perturbation direction.
 *          Then perturbs the wiper by @c po_step in the current direction.
 *
 * @param[in] power  Current input power in W (V_in × I_in).
 */
static void mppt_po_step(float power){
    float delta_p = power - mppt_data.prev_power_W;

    /*
     * P&O decision:
     *   delta_P > 0 -> we moved toward the MPP, keep going
     *   delta_P < 0 -> we moved away, reverse
     *   delta_P = 0 -> hold direction (avoid oscillation on flat regions)
     */
    if (delta_p > 0.0f) {
        /* Power increased — keep current direction */
    } else if (delta_p < 0.0f) {
        /* Power decreased — reverse */
        mppt_data.direction = -mppt_data.direction;
    }
    /* else: ΔP == 0, hold direction */

    /* Apply perturbation */
    int16_t new_wiper = (int16_t)mppt_data.wiper +
                        (int16_t)(mppt_data.direction * mppt_cfg.po_step);
    mppt_data.wiper = clamp_wiper(new_wiper);

    /* Save for next iteration */
    mppt_data.prev_power_W = power;
    mppt_data.steps_since_scan++;
    mppt_data.total_steps++;
}

/**
 * @brief   Execute one step of the global IV-curve scan.
 * @details Sweeps the wiper from @c wiper_min to @c wiper_max in
 *          increments of @c scan_step.  At each position, records the
 *          power and tracks the maximum.  When the sweep completes,
 *          the wiper jumps to the position that produced peak power
 *          and the FSM returns to @ref MPPT_STATE_TRACKING.
 *
 * @param[in] power  Current input power in W at the current scan position.
 */
static void mppt_scan_step(float power){
    /* Track the best power point seen during this sweep */
    if (power > mppt_data.scan_best_power_W) {
        mppt_data.scan_best_power_W = power;
        mppt_data.scan_best_wiper   = mppt_data.scan_wiper;
    }

    /* Advance to the next scan position */
    int16_t next = (int16_t)mppt_data.scan_wiper + (int16_t)mppt_cfg.scan_step;

    if (next > (int16_t)mppt_cfg.wiper_max) {
        /*
         * Scan complete — jump to the best wiper position found and
         * resume P&O tracking from there.
         */
        mppt_data.wiper         = mppt_data.scan_best_wiper;
        mppt_data.prev_power_W  = mppt_data.scan_best_power_W;
        mppt_data.direction     = +1;
        mppt_data.steps_since_scan = 0;
        mppt_data.state         = MPPT_STATE_TRACKING;
        mppt_data.total_scans++;
    } else {
        /* Continue scanning */
        mppt_data.scan_wiper = (uint8_t)next;
        mppt_data.wiper      = mppt_data.scan_wiper;
    }
}

/**
 * @brief   Begin a new global scan sweep.
 * @details Resets scan tracking variables and moves the wiper to
 *          @c wiper_min to start the sweep.
 */
static void mppt_start_scan(void){
    mppt_data.state            = MPPT_STATE_GLOBAL_SCAN;
    mppt_data.scan_wiper       = mppt_cfg.wiper_min;
    mppt_data.scan_best_power_W = 0.0f;
    mppt_data.scan_best_wiper  = mppt_cfg.wiper_min;
    mppt_data.wiper            = mppt_cfg.wiper_min;
}

/* ──────────────────────────── Public Step ─────────────────────────────── */
uint8_t MPPT_Step(float v_in, float i_in, float v_out, float i_out){
    /* Store latest readings for telemetry / debug */
    mppt_data.in_voltage_V  = v_in;
    mppt_data.in_current_A  = i_in;
    mppt_data.out_voltage_V = v_out;
    mppt_data.out_current_A = i_out;

    float power = v_in * i_in;
    mppt_data.power_W = power;

    switch (mppt_data.state){
    case MPPT_STATE_TRACKING:
        /* Check if it's time for a global scan */
        if (mppt_data.steps_since_scan >= mppt_cfg.scan_interval){
            mppt_start_scan();
            /* Don't run P&O this step — first scan sample will be taken
             * on the next call once the wiper has settled at wiper_min. */
        } else{
            mppt_po_step(power);
        }
        break;

    case MPPT_STATE_GLOBAL_SCAN:
        mppt_scan_step(power);
        break;

    case MPPT_STATE_FAULT:
        /* Wiper frozen — return current value without modification */
        break;

    case MPPT_STATE_IDLE:
    default:
        /* Not started — return current wiper as-is */
        break;
    }

    return mppt_data.wiper;
}

/* ──────────────────────────── Accessors / Control ────────────────────── */
const MPPT_Data_t *MPPT_GetData(void){
    return &mppt_data;
}

void MPPT_SetConfig(const MPPT_Config_t *config){
    if (config == NULL) return;
    mppt_cfg = *config;

    /* Immediately clamp the current wiper to the new range */
    mppt_data.wiper = clamp_wiper((int16_t)mppt_data.wiper);
}

void MPPT_ForceGlobalScan(void){
	if (mppt_data.state == MPPT_STATE_TRACKING) {
        mppt_start_scan();
    }
}

void MPPT_Fault(void){
    mppt_data.state = MPPT_STATE_FAULT;
}
