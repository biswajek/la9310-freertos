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
#include "ms_camera_l1c_modem_mgr.h"
#include "ms_camera_procx_comm.h"
#include "ms_camera_l1c_transmitter.h"
#include "ms_camera_l1c_receiver.h"

#define MS_TRANSMITTER_QUEUE_NAME     "camTransmitterQ"
#define TRANSMITTER_Q_WAIT_MS         1U
#define MAX_CAMERA_ID                 5U

proc_queue_t transmitter_q;

static void transmitter_send_bch_over_air( const S_UNIFIED_MSG_BUFF *msg )
{
    UNUSED( msg );
}

void transmitter_cb_xc( procx_comm_id_e src_proc, void *data )
{
    Error_t err = Success;

    switch( src_proc )
    {
        case CAMMGR_XC_ID:
            err = send_cmd_to_proc( &transmitter_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
                log_err( "[CAM TX] Msg from camera manager not pushed in queue\r\n" );
            break;
        case VSPA_IN_XC_ID:
            err = send_cmd_to_proc( &transmitter_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
                log_err( "[CAM TX] Msg from VSPA IN not pushed in queue\r\n" );
            break;
        case TIMER_HANDLER_XC_ID:
        case VSPA_OUT_XC_ID:
        case RX_XC_ID:
        case TX_XC_ID:
        case LOG_XC_ID:
        case SLOT_INTR_XC_ID:
        case RSSI_INTR_XC_ID:
        case L2_TEST_XC_ID:
        default:
            break;
    }
}

void l1_camera_transmitter_init( uint8_t core_num )
{
    init_proc_cmd_queue( &transmitter_q, (core_id_t) core_num,
                         sizeof( S_UNIFIED_MSG_BUFF ), MS_TRANSMITTER_QUEUE_NAME );

    procx_comm_reg( transmitter_cb_xc, TX_XC_ID );
}

void *transmitter_task( void *arg )
{
    S_UNIFIED_MSG_BUFF rx_msg;
    uint32_t           queue_wait_ms;
    size_t             rx_msg_len;
    MsgQ_Priorities_t  rx_msg_prio;

    (void) arg;

    log_info( "[CAM TX] task started\r\n" );

    for( ; ; )
    {
        queue_wait_ms = TRANSMITTER_Q_WAIT_MS;
        while( pal_msgq_receive( transmitter_q.queue, &rx_msg, sizeof( rx_msg ),
                                 &rx_msg_len, &rx_msg_prio, queue_wait_ms ) == Success )
        {
            switch( rx_msg.opcode )
            {
                case MS_MSG_OPCODE_BCH_SEND:
                    /* BCH carries control parameters in camera_id/payload.
                     * Send BCH over the air, then — if control params are present —
                     * signal VSPA_OUT and notify RX to start the ACK wait window. */
                    transmitter_send_bch_over_air( &rx_msg );

                    if( rx_msg.payload != NULL )
                    {
                        S_UNIFIED_MSG_BUFF vspa_ind = {
                            .opcode    = MS_MSG_OPCODE_VSPA_SEND_CTRL,
                            .payload   = rx_msg.payload,
                            .camera_id = rx_msg.camera_id,
                            .time      = rx_msg.time,
                        };
                        (void) procx_comm( VSPA_OUT_XC_ID, TX_XC_ID, &vspa_ind );

                        S_UNIFIED_MSG_BUFF rx_ind = {
                            .opcode    = MS_MSG_OPCODE_CTRL_MSG_SENT,
                            .payload   = NULL,
                            .camera_id = rx_msg.camera_id,
                            .time      = rx_msg.time,
                        };
                        (void) procx_comm( RX_XC_ID, TX_XC_ID, &rx_ind );
                    }
                    break;

                default:
                    break;
            }

            queue_wait_ms = 0U;
        }
    }

    return NULL;
}
