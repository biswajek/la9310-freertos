/*
 * Mundo Sense
 */

/**
 * @file ms_camera_l1c_transmitter.c
 * @brief Camera L1 transmitter — ACK transmission for slot 1.
 *
 * IDLE ──(PREPARE_ACK_TX from RX)──> send TX_ENC_CFG to VSPA_IN
 *                                 ──> WAIT_ENC_CFG_ACK
 *
 * WAIT_ENC_CFG_ACK ──(TX_ENC_CFG_ACK from VSPA_IN)──> record bch_frame
 *                                                  ──> WAIT_SLOT1
 *
 * WAIT_SLOT1 ──(frame > bch_frame && slot == 1)──>
 *   transmit ACK over the air ──> IDLE
 */

#include <stdbool.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "task.h"
#include "pal_thread.h"
#include "pal_types.h"
#include "ms_camera_global_typedef.h"
#include "ms_camera_globals.h"
#include "ms_camera_logger.h"
#include "ms_camera_l1c_controller.h"
#include "ms_camera_l1_controller_fwk.h"
#include "ms_camera_procx_comm.h"
#include "ms_camera_l1c_transmitter.h"
#include "ms_camera_l1c_modem_mgr.h"

#define MS_TRANSMITTER_QUEUE_NAME     "camTransmitterQ"
#define TRANSMITTER_Q_WAIT_MS         1U

proc_queue_t transmitter_q;

typedef enum
{
    CAM_TX_STATE_IDLE = 0,
    CAM_TX_STATE_WAIT_ENC_CFG_ACK,
    CAM_TX_STATE_WAIT_SLOT1,
} cam_tx_state_t;

static cam_tx_state_t tx_state;
static uint8_t        pending_camera_id;
static uint32_t       pending_time;
static uint32_t       pending_bch_frame;

static void transmitter_send_ack_over_air( uint8_t camera_id, uint32_t time )
{
    log_info( "[CAM TX] ACK transmitted (slot 1 of next frame, camera_id=%u time=%lu)\r\n",
              (unsigned) camera_id, (unsigned long) time );
    /* Stub: wire up the radio TX path here. */
    UNUSED( camera_id );
    UNUSED( time );
}

void transmitter_cb_xc( procx_comm_id_e src_proc, void *data )
{
    Error_t err = Success;

    switch( src_proc )
    {
        case RX_XC_ID:       /* PREPARE_ACK_TX */
        case VSPA_IN_XC_ID:  /* TX_ENC_CFG_ACK */
            err = send_cmd_to_proc( &transmitter_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
                log_err( "[CAM TX] Msg not pushed in queue (src=%d)\r\n",
                         (int) src_proc );
            break;
        default:
            break;
    }
}

void l1_camera_transmitter_init( uint8_t core_num )
{
    init_proc_cmd_queue( &transmitter_q, (core_id_t) core_num,
                         sizeof( S_UNIFIED_MSG_BUFF ), MS_TRANSMITTER_QUEUE_NAME );

    tx_state          = CAM_TX_STATE_IDLE;
    pending_camera_id = 0U;
    pending_time      = 0U;
    pending_bch_frame = 0U;

    procx_comm_reg( transmitter_cb_xc, TX_XC_ID );
}

void *transmitter_task( void *arg )
{
    S_UNIFIED_MSG_BUFF msg;
    size_t             msg_len;
    MsgQ_Priorities_t  msg_prio;

    (void) arg;

    log_info( "[CAM TX] task started\r\n" );

    for( ; ; )
    {
        /* When waiting for slot 1 of the next frame, poll the slot/frame
         * counter instead of blocking on the queue. */
        if( tx_state == CAM_TX_STATE_WAIT_SLOT1 )
        {
            uint32_t cur_slot, cur_frame;
            camera_mgr_get_slot_frame_count( &cur_slot, &cur_frame );

            if( ( cur_frame > pending_bch_frame ) && ( cur_slot == 1U ) )
            {
                transmitter_send_ack_over_air( pending_camera_id, pending_time );
                tx_state = CAM_TX_STATE_IDLE;

                S_UNIFIED_MSG_BUFF ack_sent = {
                    .opcode    = MS_MSG_OPCODE_ACK_SENT,
                    .camera_id = pending_camera_id,
                    .time      = pending_time,
                };
                (void) procx_comm( CAMMGR_XC_ID, TX_XC_ID, &ack_sent );
            }
            else
            {
                taskYIELD();
            }
            continue;
        }

        if( pal_msgq_receive( transmitter_q.queue, &msg, sizeof( msg ),
                              &msg_len, &msg_prio, TRANSMITTER_Q_WAIT_MS ) != Success )
        {
            continue;
        }

        switch( msg.opcode )
        {
            case MS_MSG_OPCODE_PREPARE_ACK_TX:
                /* BCH decoded — configure VSPA TX encoder so the ACK can go
                 * out in slot 1 of the next frame. */
                log_info( "[CAM TX] PREPARE_ACK_TX (camera_id=%u) — configuring TX encoder for next frame slot 1\r\n",
                          (unsigned) msg.camera_id );

                pending_camera_id = msg.camera_id;
                pending_time      = msg.time;
                tx_state          = CAM_TX_STATE_WAIT_ENC_CFG_ACK;

                {
                    S_UNIFIED_MSG_BUFF enc_cfg = {
                        .opcode    = MS_MSG_OPCODE_TX_ENC_CFG,
                        .camera_id = msg.camera_id,
                        .time      = msg.time,
                    };
                    (void) procx_comm( VSPA_IN_XC_ID, TX_XC_ID, &enc_cfg );
                }
                break;

            case MS_MSG_OPCODE_TX_ENC_CFG_ACK:
                /* VSPA TX encoder is configured — wait for slot 1 of next frame. */
                if( tx_state == CAM_TX_STATE_WAIT_ENC_CFG_ACK )
                {
                    camera_mgr_get_slot_frame_count( NULL, &pending_bch_frame );
                    tx_state = CAM_TX_STATE_WAIT_SLOT1;
                    log_info( "[CAM TX] encoder ready — waiting for slot 1 of frame %lu\r\n",
                              (unsigned long)( pending_bch_frame + 1U ) );
                }
                break;

            default:
                break;
        }
    }

    return NULL;
}
