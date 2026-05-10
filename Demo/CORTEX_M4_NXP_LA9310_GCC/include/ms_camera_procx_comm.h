/*
 * Mundo Sense
 */

#ifndef __CAMERA_PROCX_COMM_H
#define __CAMERA_PROCX_COMM_H

#include <stdbool.h>
#include <stdint.h>

union procxMsg
{
    int x;
};

typedef enum
{
    CAMMGR_XC_ID,
    TIMER_HANDLER_XC_ID,
    VSPA_IN_XC_ID,
    VSPA_OUT_XC_ID,
    RX_XC_ID,
    TX_XC_ID,
    LOG_XC_ID,
    SLOT_INTR_XC_ID,
    RSSI_INTR_XC_ID,
    L2_TEST_XC_ID,
    NUM_XC_IDS
} procx_comm_id_e;

typedef void (*procx_comm_cb_t)( procx_comm_id_e src_proc, void *data );

bool procx_comm( procx_comm_id_e dst_proc, procx_comm_id_e src_proc, void *data );
bool procx_comm_reg( procx_comm_cb_t cb, procx_comm_id_e proc_id );

#endif /* __CAMERA_PROCX_COMM_H */
