/**
 * @file    safety.c
 * @author  Aragya Goyal
 * @brief   Safety monitor implementation for the Zoë2 PMS board
 * @version 1.0
 * @date    2026-04-21
 *
 * @details Implements the binary shutdown/recovery state machine defined
 *          in @ref safety.h. All state is kept in file-scope statics;
 *          the module supports a single instance per build.
 *
 */

#include "safety.h"
#include "main.h"   /* for HAL_GetTick() */
#include <string.h>

/* ─────────────────────────────────────────────────────────────────── */
/* Internal state                                                      */
/* ─────────────────────────────────────────────────────────────────── */
/** @brief Copy of the caller-supplied configuration. Written by
 *         @ref Safety_Init(), read by @ref Safety_Check(). */
static Safety_Config_t s_cfg;

/** @brief Live status, updated on every @ref Safety_Check() call. */
static Safety_Status_t s_status;

/* ─────────────────────────────────────────────────────────────────── */
/* Public API                                                          */
/* ─────────────────────────────────────────────────────────────────── */

/**
 * @brief   Initialise the safety monitor with the given configuration.
 * @details See @ref Safety_Init() in safety.h.
 */
void Safety_Init(const Safety_Config_t *cfg)
{
    if (cfg == NULL) return;

    memcpy(&s_cfg, cfg, sizeof(s_cfg));
    memset(&s_status, 0, sizeof(s_status));
    s_status.response = SAFETY_SAFE;
}

/**
 * @brief   Evaluate inputs against configured limits and update state.
 * @details Implementation outline:
 *            -# Build @c flags by testing each limit.
 *            -# Walk the three-element TC cache, raising
 *               @ref SAFETY_FAULT_TC_STALE or @ref SAFETY_FAULT_TEMP_OVER
 *               per sensor.
 *            -# Check BMS liveness if enabled.
 *            -# Store @c flags in @ref Safety_Status_t::fault_flags.
 *            -# If any fault active → trip to @ref SAFETY_SHUTDOWN and
 *               reset the clear-window timer.
 *               If none active → advance the clear-window timer; once it
 *               has run for @c recovery_ms, return to @ref SAFETY_SAFE.
 */
Safety_Response_t Safety_Check(const Safety_Inputs_t *in)
{
    if (in == NULL) return s_status.response;

    uint32_t now   = HAL_GetTick();
    uint32_t flags = 0;

    /* ── Electrical checks ───────────────────────────────────────── */
    if (in->v_in  > s_cfg.vin_max_V)  flags |= SAFETY_FAULT_VIN_OVER;
    if (in->v_in  < s_cfg.vin_min_V)  flags |= SAFETY_FAULT_VIN_UNDER;
    if (in->i_in  > s_cfg.iin_max_A)  flags |= SAFETY_FAULT_IIN_OVER;
    if (in->v_out > s_cfg.vout_max_V) flags |= SAFETY_FAULT_VOUT_OVER;
    if (in->i_out > s_cfg.iout_max_A) flags |= SAFETY_FAULT_IOUT_OVER;

    /* ── Thermal checks ──────────────────────────────────────────── */
    if (in->tc_C != NULL && in->tc_last != NULL) {
        for (uint8_t i = 0; i < 3; i++) {
            /* Treat "never read" (last == 0) the same as "too old" */
            if (in->tc_last[i] == 0 ||
                (now - in->tc_last[i]) > s_cfg.tc_stale_ms) {
                flags |= SAFETY_FAULT_TC_STALE;
            } else if (in->tc_C[i] >= s_cfg.temp_max_C) {
                flags |= SAFETY_FAULT_TEMP_OVER;
            }
        }
    } else {
        /* Caller passed NULL cache pointers — treat as stale */
        flags |= SAFETY_FAULT_TC_STALE;
    }

    /* ── BMS liveness ────────────────────────────────────────────── */
    if (in->bms_enabled) {
        if (in->bms_last_tick == 0 ||
            (now - in->bms_last_tick) > s_cfg.bms_timeout_ms) {
            flags |= SAFETY_FAULT_BMS_LOST;
        }
    }

    s_status.fault_flags = flags;

    /* ── Decide response with auto-recovery ──────────────────────── */
    if (flags != 0) {
        /* A fault is active this cycle — trip (or stay tripped) */
        if (s_status.response == SAFETY_SAFE) {
            s_status.shutdown_tick = now;
        }
        s_status.response         = SAFETY_SHUTDOWN;
        s_status.clear_since_tick = 0;   /* reset recovery timer */
    } else {
        /* No faults active this cycle */
        if (s_status.response == SAFETY_SHUTDOWN) {
            /* Still shut down — run the recovery window */
            if (s_status.clear_since_tick == 0) {
                s_status.clear_since_tick = now;
            }
            if ((now - s_status.clear_since_tick) >= s_cfg.recovery_ms) {
                /* Clean for long enough — recover */
                s_status.response         = SAFETY_SAFE;
                s_status.shutdown_tick    = 0;
                s_status.clear_since_tick = 0;
            }
            /* else: keep counting, stay in SHUTDOWN */
        }
        /* else: already SAFE with no faults — nothing to do */
    }

    return s_status.response;
}

/**
 * @brief   Read-only access to the internal status struct.
 * @details See @ref Safety_GetStatus() in safety.h.
 */
const Safety_Status_t *Safety_GetStatus(void)
{
    return &s_status;
}

/**
 * @brief   Return a short text tag for the worst active fault.
 * @details See @ref Safety_FaultTag() in safety.h. Priority is fixed in
 *          source order and chosen to surface the most urgent condition
 *          first on the OLED header.
 */
const char *Safety_FaultTag(void)
{
    uint32_t f = s_status.fault_flags;

    /* Priority order — most urgent first */
    if (f & SAFETY_FAULT_TEMP_OVER)  return "OT";
    if (f & SAFETY_FAULT_VOUT_OVER)  return "OV:OUT";
    if (f & SAFETY_FAULT_VIN_OVER)   return "OV:IN";
    if (f & SAFETY_FAULT_IOUT_OVER)  return "OC:OUT";
    if (f & SAFETY_FAULT_IIN_OVER)   return "OC:IN";
    if (f & SAFETY_FAULT_BMS_LOST)   return "BMS";
    if (f & SAFETY_FAULT_TC_STALE)   return "TC?";
    if (f & SAFETY_FAULT_VIN_UNDER)  return "UV:IN";

    /* No faults currently active but still shut down → recovery window */
    if (s_status.response == SAFETY_SHUTDOWN) return "WAIT";
    return "";
}
