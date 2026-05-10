/*
 * Mundo Sense
 */

#include <stddef.h>
#include "pal_thread.h"
#include "pal_types.h"
#include "ms_camera_global_typedef.h"
#include "ms_camera_globals.h"
#include "ms_camera_l1c_controller.h"
#include "ms_camera_l1_controller_fwk.h"
#include "ms_camera_procx_comm.h"
#include "ms_camera_l1c_vspa_out.h"
#include "ms_camera_logger.h"

#define MS_VSPAOUT_QUEUE_NAME        "camVspaOutQ"

proc_queue_t vspa_out_q;

static void vspa_out_forward_ctrl_to_vspa_in( const S_UNIFIED_MSG_BUFF *msg )
{
    if( msg == NULL )
        return;

    log_info( "[CAM VSPA OUT] forwarding ctrl request to VSPA_IN (camera_id=%u)\r\n",
              (unsigned) msg->camera_id );
    (void) procx_comm( VSPA_IN_XC_ID, VSPA_OUT_XC_ID, (void *) msg );
}

void vspa_out_cb_xc( procx_comm_id_e src_proc, void *data )
{
    Error_t err = Success;

    switch( src_proc )
    {
        case CAMMGR_XC_ID:
            break;
        case TIMER_HANDLER_XC_ID:
            break;
        case VSPA_IN_XC_ID:
            cam_trace_write( 11U, 0x0B000000u |
                             (uint32_t)( (S_UNIFIED_MSG_BUFF *) data )->opcode );
            err = send_cmd_to_proc( &vspa_out_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
            {
                g_GlobalDebugInfo.VspaOutQFull++;
                log_err( "[CAM VSPA OUT] Msg from VSPA_IN not pushed in queue\r\n" );
            }
            break;
        case TX_XC_ID:
            err = send_cmd_to_proc( &vspa_out_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
            {
                g_GlobalDebugInfo.VspaOutQFull++;
                log_err( "[CAM VSPA OUT] Msg from TX not pushed in queue\r\n" );
            }
            break;
        case RX_XC_ID:
        case VSPA_OUT_XC_ID:
        case LOG_XC_ID:
        case SLOT_INTR_XC_ID:
        case RSSI_INTR_XC_ID:
        case L2_TEST_XC_ID:
        default:
            break;
    }
}

void l1_camera_vspa_out_init( uint8_t core_num )
{
    init_proc_cmd_queue( &vspa_out_q, (core_id_t) core_num,
                         sizeof( S_UNIFIED_MSG_BUFF ), MS_VSPAOUT_QUEUE_NAME );

    procx_comm_reg( vspa_out_cb_xc, VSPA_OUT_XC_ID );
}

void *vspa_out_task( void *arg )
{
    S_UNIFIED_MSG_BUFF msg;
    size_t             msg_len;
    MsgQ_Priorities_t  msg_prio;

    (void) arg;

    log_info( "[CAM VSPA OUT] task started\r\n" );

    for( ; ; )
    {
        if( pal_msgq_receive( vspa_out_q.queue, &msg, sizeof( msg ),
                              &msg_len, &msg_prio, 1U ) == Success )
        {
            switch( msg.opcode )
            {
                case MS_MSG_OPCODE_VSPA_SEND_CTRL:
                    vspa_out_forward_ctrl_to_vspa_in( &msg );
                    break;

                case MS_MSG_OPCODE_VSPA_WAIT_VIDEO_ENC_ACK:
                    log_info( "[CAM VSPA OUT] ignoring legacy video enc ACK wait request\r\n" );
                    break;

                default:
                    break;
            }
        }
    }

    return NULL;
}
