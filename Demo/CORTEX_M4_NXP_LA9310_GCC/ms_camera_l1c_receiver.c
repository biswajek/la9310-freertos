/*
 * Mundo Sense
 */

/**
 * @file ms_camera_l1c_receiver.c
 * @brief Camera L1 receiver — BCH decode state machine.
 *
 * IDLE ──(init)──> arm LDPC decoder ──> WAIT_BCH
 *
 * WAIT_BCH ──(BCH_DECODED from VSPA_IN)──>
 *   1. Notify camera_mgr (BCH_NOTIFY_HOST) so it forwards to host via IPC
 *   2. Tell transmitter to prepare ACK for slot 1 (PREPARE_ACK_TX)
 *   3. Re-arm LDPC decoder ──> WAIT_BCH
 *
 * WAIT_BCH ──(BCH_DECODE_FAIL from VSPA_IN)──> re-arm ──> WAIT_BCH
 */

#include <stddef.h>
#include "pal_thread.h"
#include "pal_types.h"
#include "ms_camera_global_typedef.h"
#include "ms_camera_globals.h"
#include "ms_camera_logger.h"
#include "ms_camera_l1c_controller.h"
#include "ms_camera_l1_controller_fwk.h"
#include "ms_camera_procx_comm.h"
#include "ms_camera_l1c_receiver.h"

#define MS_RECEIVER_QUEUE_NAME     "camReceiverQ"
#define RECEIVER_Q_WAIT_MS         1U

proc_queue_t receiver_q;

static cam_rx_state_t rx_state;

static void receiver_arm_ldpc_decoder( void )
{
    S_UNIFIED_MSG_BUFF cfg = {
        .opcode    = MS_MSG_OPCODE_BCH_LDPC_CFG,
        .camera_id = 0U,
        .time      = 0U,
    };
    (void) procx_comm( VSPA_IN_XC_ID, RX_XC_ID, &cfg );
    rx_state = CAM_RX_STATE_WAIT_BCH;
    log_info( "[CAM RX] LDPC decoder armed — state WAIT_BCH\r\n" );
}

void receiver_cb_xc( procx_comm_id_e src_proc, void *data )
{
    Error_t err = Success;

    switch( src_proc )
    {
        case VSPA_IN_XC_ID:
            err = send_cmd_to_proc( &receiver_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
                log_err( "[CAM RX] Msg from VSPA_IN not pushed in queue\r\n" );
            break;
        default:
            break;
    }
}

void l1_camera_receiver_init( uint8_t core_num )
{
    init_proc_cmd_queue( &receiver_q, (core_id_t) core_num,
                         sizeof( S_UNIFIED_MSG_BUFF ), MS_RECEIVER_QUEUE_NAME );

    rx_state = CAM_RX_STATE_IDLE;

    procx_comm_reg( receiver_cb_xc, RX_XC_ID );
}

void *receiver_task( void *arg )
{
    S_UNIFIED_MSG_BUFF msg;
    size_t             msg_len;
    MsgQ_Priorities_t  msg_prio;

    (void) arg;

    log_info( "[CAM RX] task started\r\n" );

    /* Arm LDPC decoder at startup. */
    receiver_arm_ldpc_decoder();

    for( ; ; )
    {
        if( pal_msgq_receive( receiver_q.queue, &msg, sizeof( msg ),
                              &msg_len, &msg_prio, RECEIVER_Q_WAIT_MS ) != Success )
        {
            continue;
        }

        switch( msg.opcode )
        {
            case MS_MSG_OPCODE_BCH_DECODED:
                if( rx_state == CAM_RX_STATE_WAIT_BCH )
                {
                    log_info( "[CAM RX] BCH decoded (camera_id=%u time=%lu)\r\n",
                              (unsigned) msg.camera_id, (unsigned long) msg.time );

                    /* 1. Notify camera manager → host IPC. */
                    S_UNIFIED_MSG_BUFF host_notify = {
                        .opcode    = MS_MSG_OPCODE_BCH_NOTIFY_HOST,
                        .camera_id = msg.camera_id,
                        .time      = msg.time,
                    };
                    (void) procx_comm( CAMMGR_XC_ID, RX_XC_ID, &host_notify );

                    /* 2. Tell transmitter to prepare ACK for slot 1. */
                    S_UNIFIED_MSG_BUFF tx_req = {
                        .opcode    = MS_MSG_OPCODE_PREPARE_ACK_TX,
                        .camera_id = msg.camera_id,
                        .time      = msg.time,
                    };
                    (void) procx_comm( TX_XC_ID, RX_XC_ID, &tx_req );

                    /* 3. Re-arm LDPC decoder for the next BCH. */
                    receiver_arm_ldpc_decoder();
                }
                break;

            case MS_MSG_OPCODE_BCH_DECODE_FAIL:
                log_err( "[CAM RX] BCH decode failed — re-arming LDPC decoder\r\n" );
                receiver_arm_ldpc_decoder();
                break;

            default:
                break;
        }
    }

    return NULL;
}
