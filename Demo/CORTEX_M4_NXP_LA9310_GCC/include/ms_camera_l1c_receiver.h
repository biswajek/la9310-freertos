/*
 * Mundo Sense
 */

#ifndef __MS_CAM_L1C_RECEIVER_H
#define __MS_CAM_L1C_RECEIVER_H

#include "ms_camera_l1_controller_fwk.h"
#include "ms_camera_procx_comm.h"

/** @brief Receiver task states. */
typedef enum
{
    CAM_RX_STATE_IDLE     = 0, /**< Idle — LDPC decoder not yet armed. */
    CAM_RX_STATE_WAIT_BCH,     /**< LDPC decoder armed, waiting for BCH decode. */
} cam_rx_state_t;

extern proc_queue_t receiver_q;

void  receiver_cb_xc( procx_comm_id_e src_proc, void *data );
void  l1_camera_receiver_init( uint8_t core_num );
void *receiver_task( void *arg );

#endif /* __MS_CAM_L1C_RECEIVER_H */
