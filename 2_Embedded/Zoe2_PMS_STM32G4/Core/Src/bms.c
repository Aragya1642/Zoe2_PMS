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


