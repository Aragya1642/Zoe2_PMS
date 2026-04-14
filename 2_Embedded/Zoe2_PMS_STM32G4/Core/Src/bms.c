/*
 * bms.c
 *
 *  Created on: Apr 13, 2026
 *      Author: agoya
 */

#include "bms.h"
#include <string.h>
#include <stdio.h>

/* ──────────────────────────── Private State ──────────────────────────── */
/** @brief Cached handle to the FDCAN2 peripheral (set by @ref BMS_Init). */
static FDCAN_HandleTypeDef *bms_hfdcan = NULL;

/** @brief Pre-built TX header reused on every @ref BMS_SendQuery() call. */
static FDCAN_TxHeaderTypeDef bms_txHeader;

/** @brief TX data buffer — always all zeros for the query frame. */
static uint8_t bms_txData[8];

/** @brief Internal telemetry store, updated from ISR context. */
static BMS_Data_t bms_data;

/* ──────────────────────────── Byte Helpers ───────────────────────────── */
/**
 * @brief  Read a big-endian unsigned 16-bit value from a byte buffer.
 * @param[in] p  Pointer to the first (most-significant) byte.
 * @return    Decoded uint16_t value.
 */
static inline uint16_t be16(const uint8_t *p){
    return ((uint16_t)p[0] << 8) | p[1];
}

/**
 * @brief  Read a big-endian unsigned 32-bit value from a byte buffer.
 * @param[in] p  Pointer to the first (most-significant) byte.
 * @return    Decoded uint32_t value.
 */
static inline uint32_t be32(const uint8_t *p){
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* ──────────────────────────── Init ──────────────────────────────────── */
HAL_StatusTypeDef BMS_Init(FDCAN_HandleTypeDef *hfdcan){
    HAL_StatusTypeDef status;
    bms_hfdcan = hfdcan;

    /* Clear the entire telemetry struct to known-zero state */
    memset(&bms_data, 0, sizeof(bms_data));

    /*
     * Extended-ID filter: accept all BMS responses whose CAN IDs match
     * 0x04XX____.  The mask checks the top 5 bits of the 29-bit ID,
     * which correspond to the priority nibble (0x04).
     *
     *   FilterID1 = match pattern  = 0x04000000
     *   FilterID2 = mask           = 0x1F000000  (bits 28:24 must match)
     */
    FDCAN_FilterTypeDef filter = {0};
    filter.IdType       = FDCAN_EXTENDED_ID;
    filter.FilterIndex  = 0;
    filter.FilterType   = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1    = 0x04000000;
    filter.FilterID2    = 0x1F000000;

    status = HAL_FDCAN_ConfigFilter(bms_hfdcan, &filter);
    if (status != HAL_OK) {
        return status;
    }

    /* Reject any frame that doesn't pass the filter (keeps the FIFO clean) */
    status = HAL_FDCAN_ConfigGlobalFilter(bms_hfdcan,
                FDCAN_REJECT, FDCAN_REJECT,
                FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);
    if (status != HAL_OK) {
        return status;
    }

    /*
     * Prepare the TX header once — it never changes between queries.
     *   ID     = 0x0400FF80  (29-bit extended)
     *   DLC    = 8
     *   Format = Classic CAN, no BRS
     */
    bms_txHeader.Identifier          = BMS_QUERY_ID;
    bms_txHeader.IdType              = FDCAN_EXTENDED_ID;
    bms_txHeader.TxFrameType         = FDCAN_DATA_FRAME;
    bms_txHeader.DataLength          = FDCAN_DLC_BYTES_8;
    bms_txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    bms_txHeader.BitRateSwitch       = FDCAN_BRS_OFF;
    bms_txHeader.FDFormat            = FDCAN_CLASSIC_CAN;
    bms_txHeader.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
    bms_txHeader.MessageMarker       = 0;

    memset(bms_txData, 0x00, sizeof(bms_txData));

    /* Enable the RX FIFO0 new-message interrupt so responses are
     * handled automatically without polling from the main loop. */
    status = HAL_FDCAN_ActivateNotification(bms_hfdcan,
                FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);

    return status;
}

/* ──────────────────────────── TX ─────────────────────────────────────── */
HAL_StatusTypeDef BMS_SendQuery(void){
    if (bms_hfdcan == NULL) {
        return HAL_ERROR;
    }

    return HAL_FDCAN_AddMessageToTxFifoQ(bms_hfdcan, &bms_txHeader, bms_txData);
}


/* ──────────────────────────── RX Frame Parser ────────────────────────── */
/**
 * @brief   Parse a single BMS response frame into @ref bms_data.
 * @details Extracts the function code from bits [23:16] of the extended
 *          CAN ID and dispatches to the appropriate decoder.  All numeric
 *          fields are big-endian as transmitted by the Aegis BMS.
 *
 * @param[in] id  Full 29-bit extended CAN ID.
 * @param[in] d   Pointer to 8 data bytes.
 */
static void BMS_ParseFrame(uint32_t id, const uint8_t *d){
	// Get the function code
	uint8_t fc = (id >> 16) & 0xFF;

	// Switch on the function code
	switch (fc) {
		/* ── fc 0x00: Cell Voltages (3 cells per frame) ───────────────────── */
		case 0x00: {
			uint8_t group = d[0];
			if (group == 0) break;
			uint8_t base = (group - 1) * 3;        /* 0-indexed cell number */
			for (uint8_t i = 0; i < 3; i++) {
				uint8_t cell = base + i;
				if (cell >= BMS_MAX_CELLS) break;
				bms_data.cells.cell_mv[cell] = be16(&d[1 + i * 2]);
			}
			break;
		}

		/* ── fc 0x01: Temperatures (up to 7 sensors per frame) ───────────── */
		case 0x01: {
			uint8_t group = d[0];
			if (group == 0) break;
			uint8_t base = (group - 1) * 7;    /* 0-indexed sensor number */
			for (uint8_t i = 0; i < 7; i++) {
				uint8_t idx = base + i;
				if (idx >= BMS_MAX_TEMP_SENSORS) break;
				if (d[1 + i] == 0xFF) break;   /* 0xFF = padding, no more data */
				bms_data.temps.temp_c[idx] = (int8_t)(d[1 + i] - 40);
			}
			break;
		}

		/* ── fc 0x02: Total Information 0 (V, I, SoC) ────────────────────── */
		case 0x02:
			bms_data.total0.pack_voltage_V = (float)be16(&d[0]) * 0.1f;
			bms_data.total0.current_A      = ((float)be16(&d[2]) - 30000.0f) * 0.1f;
			bms_data.total0.soc_pct        = (float)be16(&d[4]) * 0.1f;
			bms_data.total0.life_cycle     = d[6];
			break;

		/* ── fc 0x03: Total Information 1 (Power, MOS Temp) ───────────────── */
		case 0x03:
			bms_data.total1.power_W    = be16(&d[0]);
			bms_data.total1.mos_temp_C = (int8_t)(d[4] - 40);
			break;

		/* ── fc 0x04: Cell Voltage Statistics ─────────────────────────────── */
		case 0x04:
			bms_data.cell_stats.max_mv   = be16(&d[0]);
			bms_data.cell_stats.max_cell = d[2];
			bms_data.cell_stats.min_mv   = be16(&d[3]);
			bms_data.cell_stats.min_cell = d[5];
			bms_data.cell_stats.diff_mv  = be16(&d[6]);
			break;

		/* ── fc 0x05: Temperature Statistics ──────────────────────────────── */
		case 0x05:
			bms_data.temp_stats.max_temp_C = (int8_t)(d[0] - 40);
			bms_data.temp_stats.max_unit   = d[1];
			bms_data.temp_stats.min_temp_C = (int8_t)(d[2] - 40);
			bms_data.temp_stats.min_unit   = d[3];
			bms_data.temp_stats.diff_C     = d[4];
			break;

		/* ── fc 0x06: MOS Switch States ───────────────────────────────────── */
		case 0x06:
			bms_data.mos.charge    = d[0];
			bms_data.mos.discharge = d[1];
			bms_data.mos.precharge = d[2];
			bms_data.mos.heat      = d[3];
			bms_data.mos.fan       = d[4];
			break;

		/* ── fc 0x07: Status Information 1 ────────────────────────────────── */
		case 0x07:
			bms_data.status1.bat_state   = d[0];
			bms_data.status1.chg_detect  = d[1];
			bms_data.status1.load_detect = d[2];
			bms_data.status1.do_state    = d[3];
			bms_data.status1.di_state    = d[4];
			break;

		/* ── fc 0x08: Status Information 2 (Config / Capacity) ────────────── */
		case 0x08:
			bms_data.status2.cell_number         = d[0];
			bms_data.status2.ntc_number          = d[1];
			bms_data.status2.remain_capacity_mAh = be32(&d[2]);
			bms_data.status2.cycle_count         = be16(&d[6]);
			break;

		/* ── fc 0x09: Hardware / Battery Failure (TBD) ────────────────────── */
		case 0x09:
			/* TODO: decode fault bitfields once Aegis docs are available */
			break;

		/* ── fc 0x0A: Equilibrium / Balance State ─────────────────────────── */
		case 0x0A:
			bms_data.balance.balance_state      = d[0];
			bms_data.balance.balance_current_mA = be16(&d[2]);
			bms_data.balance.bal_1_8            = d[4];
			bms_data.balance.bal_9_16           = d[5];
			bms_data.balance.bal_17_24          = d[6];
			bms_data.balance.bal_25_32          = d[7];
			break;

		/* ── fc 0x0B: Charging Information ────────────────────────────────── */
		case 0x0B:
			bms_data.charge_info.remaining_charge_min = be16(&d[0]);
			bms_data.charge_info.wakeup_source        = d[2];
			break;

		/* ── fc 0x0C: BMS Calendar / RTC ──────────────────────────────────── */
		case 0x0C:
			/* Not Doing */
			break;

		/* ── fc 0x0D: Current Limiting / SoH ──────────────────────────────── */
		case 0x0D:
			bms_data.limiting.limit_state      = d[0];
			bms_data.limiting.limit_current_mA = be16(&d[1]);
			bms_data.limiting.soh_pct          = be16(&d[3]);
			bms_data.limiting.pwm_duty         = be16(&d[5]);
			break;

		/* ── fc 0x0E: Fault Codes (TBD) ───────────────────────────────────── */
		case 0x0E:
			/* TODO: decode fault bitfields once Aegis docs are available */
			break;

		/* ── fc 0x0F: AFE Raw Data (TBD) ──────────────────────────────────── */
		case 0x0F:
			/* TODO: decode AFE register dump once format is known */
			break;

		default:
			break;
	}

	bms_data.last_update_ms = HAL_GetTick();
	bms_data.rx_total++;
	bms_data.valid = true;
}

/* ──────────────────────────── ISR Entry Point ────────────────────────── */
void BMS_RxCallback(FDCAN_HandleTypeDef *hfdcan){
    /* Ignore interrupts from FDCAN peripherals we don't own */
    if (hfdcan != bms_hfdcan) return;

    FDCAN_RxHeaderTypeDef rxHeader;
    uint8_t rxData[8];

    /*
     * Drain all available messages — the BMS broadcasts continuously
     * at ~300 ms (fast cycle) and ~1 s (slow cycle), so multiple frames
     * may have accumulated in the FIFO by the time we enter the ISR.
     */
    while (HAL_FDCAN_GetRxFifoFillLevel(bms_hfdcan, FDCAN_RX_FIFO0) > 0) {
        if (HAL_FDCAN_GetRxMessage(bms_hfdcan, FDCAN_RX_FIFO0,
                                    &rxHeader, rxData) == HAL_OK) {
            BMS_ParseFrame(rxHeader.Identifier, rxData);
        }
    }
}

/* ──────────────────────────── Accessors ──────────────────────────────── */
const BMS_Data_t *BMS_GetData(void){
    return &bms_data;
}

bool BMS_IsTimedOut(void){
    /* If we've never received anything, don't report a timeout —
     * the BMS may not have been woken up yet. */
    if (!bms_data.valid) {
        return false;
    }

    return (HAL_GetTick() - bms_data.last_update_ms) > BMS_TIMEOUT_MS;
}
