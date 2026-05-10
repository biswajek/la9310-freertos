/*
 * Mundo Sense
 */

#include <stdbool.h>
#include <stddef.h>
#include "pal_thread.h"
#include "pal_types.h"
#include "ms_camera_global_typedef.h"
#include "ms_camera_globals.h"
#include "ms_camera_logger.h"
#include "ms_camera_l1c_controller.h"
#include "ms_camera_l1_controller_fwk.h"
#include "ms_camera_procx_comm.h"
#include "ms_camera_l1c_modem_mgr.h"
#include "ms_camera_l1c_receiver.h"

#define MS_RECEIVER_QUEUE_NAME     "camReceiverQ"
#define RECEIVER_Q_WAIT_MS         1U

proc_queue_t receiver_q;

static bool    rx_ctrl_ack_pending;
static uint8_t rx_ctrl_ack_camera_id;

static void rx_vspa_demod_configure( uint8_t camera_id )
{
    UNUSED( camera_id );
    log_info( "[CAM RX] VSPA demod configure (camera_id=%u) [stub]\r\n",
              (unsigned) camera_id );
}

static void rx_vspa_demod_clear_config( uint8_t camera_id )
{
    UNUSED( camera_id );
    log_info( "[CAM RX] VSPA demod clear config (camera_id=%u) [stub]\r\n",
              (unsigned) camera_id );
}

void receiver_cb_xc( procx_comm_id_e src_proc, void *data )
{
    Error_t err = Success;

    switch( src_proc )
    {
        case CAMMGR_XC_ID:
            err = send_cmd_to_proc( &receiver_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
                log_err( "[CAM RX] Msg from CAMERA MGR not pushed in queue\r\n" );
            break;
        case VSPA_IN_XC_ID:
            err = send_cmd_to_proc( &receiver_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
                log_err( "[CAM RX] Msg from VSPA IN not pushed in queue\r\n" );
            break;
        case TX_XC_ID:
            err = send_cmd_to_proc( &receiver_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
                log_err( "[CAM RX] Msg from TX not pushed in queue\r\n" );
            break;
        case VSPA_OUT_XC_ID:
            err = send_cmd_to_proc( &receiver_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
                log_err( "[CAM RX] Msg from VSPA_OUT not pushed in queue\r\n" );
            break;
        case TIMER_HANDLER_XC_ID:
        case RX_XC_ID:
        case LOG_XC_ID:
        case SLOT_INTR_XC_ID:
        case RSSI_INTR_XC_ID:
        case L2_TEST_XC_ID:
        default:
            break;
    }
}

void l1_camera_receiver_init( uint8_t core_num )
{
    init_proc_cmd_queue( &receiver_q, (core_id_t) core_num,
                         sizeof( S_UNIFIED_MSG_BUFF ), MS_RECEIVER_QUEUE_NAME );

    rx_ctrl_ack_pending   = false;
    rx_ctrl_ack_camera_id = 0U;

    procx_comm_reg( receiver_cb_xc, RX_XC_ID );
}

void *receiver_task( void *arg )
{
    S_UNIFIED_MSG_BUFF rx_msg;
    uint32_t           queue_wait_ms;
    size_t             rx_msg_len;
    MsgQ_Priorities_t  rx_msg_prio;

    (void) arg;

    log_info( "[CAM RX] task started\r\n" );

    for( ; ; )
    {
        queue_wait_ms = RECEIVER_Q_WAIT_MS;
        while( pal_msgq_receive( receiver_q.queue, &rx_msg, sizeof( rx_msg ),
                                 &rx_msg_len, &rx_msg_prio, queue_wait_ms ) == Success )
        {
            switch( rx_msg.opcode )
            {
                case MS_MSG_OPCODE_START_VIDEO_TX:
                    log_info( "[CAM RX] START_VIDEO_TX: signalling VSPA_IN to configure video encoder (camera_id=%u)\r\n",
                              (unsigned) rx_msg.camera_id );
                    {
                        S_UNIFIED_MSG_BUFF enc_cfg = {
                            .opcode    = MS_MSG_OPCODE_VIDEO_ENC_CFG,
                            .payload   = NULL,
                            .camera_id = rx_msg.camera_id,
                            .time      = rx_msg.time,
                        };
                        (void) procx_comm( VSPA_IN_XC_ID, RX_XC_ID, &enc_cfg );
                    }
                    break;

                case MS_MSG_OPCODE_VIDEO_ENC_CFG_ACK:
                    log_info( "[CAM RX] Video encoder cfg ACK: triggering video encoding (camera_id=%u)\r\n",
                              (unsigned) rx_msg.camera_id );
                    {
                        S_UNIFIED_MSG_BUFF run = {
                            .opcode    = MS_MSG_OPCODE_VIDEO_ENC_RUN,
                            .payload   = NULL,
                            .camera_id = rx_msg.camera_id,
                            .time      = rx_msg.time,
                        };
                        (void) procx_comm( VSPA_IN_XC_ID, RX_XC_ID, &run );
                    }
                    break;

                case MS_MSG_OPCODE_VIDEO_ENC_DONE:
                    log_info( "[CAM RX] Video encoding done on VSPA (camera_id=%u status=0x%08lx)\r\n",
                              (unsigned) rx_msg.camera_id,
                              (unsigned long) (uintptr_t) rx_msg.payload );
                    {
                        S_UNIFIED_MSG_BUFF tx_ready = {
                            .opcode    = MS_MSG_OPCODE_TX_READY,
                            .payload   = rx_msg.payload,
                            .camera_id = rx_msg.camera_id,
                            .time      = rx_msg.time,
                        };
                        (void) procx_comm( CAMMGR_XC_ID, RX_XC_ID, &tx_ready );
                    }
                    break;

                case MS_MSG_OPCODE_CTRL_MSG_SENT:
                    rx_ctrl_ack_camera_id = rx_msg.camera_id;
                    rx_ctrl_ack_pending   = true;
                    rx_vspa_demod_configure( rx_ctrl_ack_camera_id );
                    log_info( "[CAM RX] Ctrl msg sent, waiting for ACK from controller (camera_id=%u)\r\n",
                              (unsigned) rx_ctrl_ack_camera_id );
                    break;

                case MS_MSG_OPCODE_CTRL_ACK:
                    if( rx_ctrl_ack_pending )
                    {
                        log_info( "[CAM RX] CTRL_ACK received from controller\r\n" );
                        rx_ctrl_ack_pending = false;
                        (void) procx_comm( CAMMGR_XC_ID, RX_XC_ID, &rx_msg );
                    }
                    break;

                case MS_MSG_OPCODE_CTRL_ACK_FAIL:
                    rx_ctrl_ack_pending = false;
                    rx_vspa_demod_clear_config( rx_msg.camera_id );
                    log_err( "[CAM RX] CTRL_ACK_FAIL from controller (camera_id=%u status=0x%08lx)\r\n",
                             (unsigned) rx_msg.camera_id,
                             (unsigned long) (uintptr_t) rx_msg.payload );
                    (void) procx_comm( CAMMGR_XC_ID, RX_XC_ID, &rx_msg );
                    break;

                default:
                    break;
            }

            queue_wait_ms = 0U;
        }
    }

    return NULL;
}
