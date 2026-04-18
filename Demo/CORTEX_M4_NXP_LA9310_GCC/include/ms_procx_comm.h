/*
 * Mundo Sense 
 */

#ifndef __PROCX_COMM_H
#define __PROCX_COMM_H

/*****************************************************************
                            INCLUDES
******************************************************************/
#include <stdbool.h>
#include <stdint.h>

/*****************************************************************
                            VARIABLES
******************************************************************/

/** @brief Placeholder message union — extend with real message fields as needed. */
union procxMsg
{
    int x;
};

typedef enum
{
    MDMMGR_XC_ID,
    TIMER_HANDLER_XC_ID,
    HOST_IN_XC_ID,
    HOST_OUT_XC_ID,
    RX_XC_ID,
    TX_XC_ID,
    LOG_XC_ID,
    SLOT_INTR_XC_ID,
    RSSI_INTR_XC_ID,
    L2_TEST_XC_ID,
    NUM_XC_IDS
} procx_comm_id_e;

typedef void (*procx_comm_cb_t)(procx_comm_id_e src_proc, void *data);

/*****************************************************************
                            FUNCTIONS
******************************************************************/
bool procx_comm(procx_comm_id_e dst_proc, procx_comm_id_e src_proc, void *data);

bool procx_comm_reg(procx_comm_cb_t cb, procx_comm_id_e proc_id);

#endif /* __PROCX_COMM_H */
