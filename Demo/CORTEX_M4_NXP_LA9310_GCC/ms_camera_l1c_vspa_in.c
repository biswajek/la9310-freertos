/*
 * Mundo Sense
 */

#include <stdbool.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "task.h"
#include "pal_thread.h"
#include "pal_types.h"
#include "ms_camera_global_typedef.h"
#include "ms_camera_globals.h"
#include "ms_camera_l1c_controller.h"
#include "ms_camera_l1_controller_fwk.h"
#include "ms_camera_procx_comm.h"
#include "ms_camera_l1c_vspa_in.h"
#include "la9310_avi.h"
#include "drivers/avi/la9310_avi_ds.h"
#include "ms_camera_logger.h"

#define MS_VSPAIN_QUEUE_NAME              "camVspaInQ"

/* Nonblocking poll counts before VSPA_IN gives up waiting for each ACK type. */
#define VSPA_IN_ACK_TIMEOUT_POLLS         10000U
#define VSPA_IN_VIDEO_ENC_TIMEOUT_POLLS   10000U

/* Camera VSPA mailbox IDs */
#define L1C_CAM_VSPA_CTRL_MBOX_ID         0U
#define L1C_CAM_VSPA_ENC_MBOX_ID          1U

/* Camera VSPA command/ACK tags (upper nibble of msb) */
#define L1C_CAM_VSPA_CMD_CTRL             0xC0000000u
#define L1C_CAM_VSPA_CMD_VIDEO_ENC_CFG    0xD0000000u
#define L1C_CAM_VSPA_CMD_VIDEO_ENC_RUN    0xE0000000u
#define L1C_CAM_VSPA_ACK_CTRL             0xC0000000u
#define L1C_CAM_VSPA_ACK_VIDEO_ENC_CFG    0xD0000000u
#define L1C_CAM_VSPA_ACK_VIDEO_ENC_RUN    0xE0000000u
#define L1C_CAM_VSPA_CMD_MASK             0xF0000000u
#define L1C_CAM_VSPA_STATUS_PASS          0x00000001u

/* Status bits for error reporting */
#define L1C_CAM_VSPA_STATUS_CTRL_SEND_FAIL     0x00000010u
#define L1C_CAM_VSPA_STATUS_CTRL_ACK_TIMEOUT   0x00000020u
#define L1C_CAM_VSPA_STATUS_ENC_SEND_FAIL      0x00000040u
#define L1C_CAM_VSPA_STATUS_ENC_CFG_TIMEOUT    0x00000080u
#define L1C_CAM_VSPA_STATUS_ENC_RUN_TIMEOUT    0x00000100u

proc_queue_t vspa_in_q;

typedef struct
{
    bool     active;
    uint8_t  camera_id;
    uint32_t request_time;
    uint32_t poll_count;
} vspa_in_ack_state_t;

static bool vspa_in_any_ack_wait_active( const vspa_in_ack_state_t *ctrl,
                                         const vspa_in_ack_state_t *enc_cfg,
                                         const vspa_in_ack_state_t *enc_run )
{
    return ( ctrl->active || enc_cfg->active || enc_run->active );
}

static void vspa_in_start_ack_wait( vspa_in_ack_state_t *state,
                                    const S_UNIFIED_MSG_BUFF *msg )
{
    state->active       = true;
    state->camera_id    = msg->camera_id;
    state->request_time = msg->time;
    state->poll_count   = 0U;
}

static void vspa_in_send_fail_to_rx( uint8_t camera_id, uint32_t time, uint32_t status )
{
    S_UNIFIED_MSG_BUFF fail = {
        .opcode    = MS_MSG_OPCODE_CTRL_ACK_FAIL,
        .payload   = (void *)(uintptr_t) status,
        .camera_id = camera_id,
        .time      = time,
    };
    (void) procx_comm( RX_XC_ID, VSPA_IN_XC_ID, &fail );
}

static void vspa_in_send_ctrl_to_vspa( void *avihndl,
                                       const S_UNIFIED_MSG_BUFF *msg,
                                       vspa_in_ack_state_t *ctrl_state )
{
    vspa_in_start_ack_wait( ctrl_state, msg );

    if( 0 != iLa9310AviHostSendMboxToVspa( avihndl,
                                            L1C_CAM_VSPA_CMD_CTRL |
                                                (uint32_t) msg->camera_id,
                                            msg->time,
                                            L1C_CAM_VSPA_CTRL_MBOX_ID ) )
    {
        ctrl_state->active = false;
        log_err( "[CAM VSPA IN] ctrl mailbox send failed (camera_id=%u)\r\n",
                 (unsigned) msg->camera_id );
        vspa_in_send_fail_to_rx( msg->camera_id, msg->time,
                                 L1C_CAM_VSPA_STATUS_CTRL_SEND_FAIL );
        return;
    }

    log_info( "[CAM VSPA IN] ctrl mailbox sent to VSPA (camera_id=%u)\r\n",
              (unsigned) msg->camera_id );
}

static void vspa_in_send_video_enc_cfg( void *avihndl,
                                        const S_UNIFIED_MSG_BUFF *msg,
                                        vspa_in_ack_state_t *enc_cfg_state )
{
    cam_trace_write( 8U, 0x08000000u | (uint32_t) msg->camera_id );
    vspa_in_start_ack_wait( enc_cfg_state, msg );

    if( 0 != iLa9310AviHostSendMboxToVspa( avihndl,
                                            L1C_CAM_VSPA_CMD_VIDEO_ENC_CFG |
                                                (uint32_t) msg->camera_id,
                                            msg->time,
                                            L1C_CAM_VSPA_ENC_MBOX_ID ) )
    {
        enc_cfg_state->active = false;
        log_err( "[CAM VSPA IN] video enc cfg mailbox send failed (camera_id=%u)\r\n",
                 (unsigned) msg->camera_id );
        vspa_in_send_fail_to_rx( msg->camera_id, msg->time,
                                 L1C_CAM_VSPA_STATUS_ENC_SEND_FAIL );
        return;
    }

    log_info( "[CAM VSPA IN] video enc cfg sent to VSPA (camera_id=%u)\r\n",
              (unsigned) msg->camera_id );
}

static void vspa_in_run_video_enc( void *avihndl,
                                   const S_UNIFIED_MSG_BUFF *msg,
                                   vspa_in_ack_state_t *enc_run_state )
{
    vspa_in_start_ack_wait( enc_run_state, msg );

    if( 0 != iLa9310AviHostSendMboxToVspa( avihndl,
                                            L1C_CAM_VSPA_CMD_VIDEO_ENC_RUN |
                                                (uint32_t) msg->camera_id,
                                            msg->time,
                                            L1C_CAM_VSPA_ENC_MBOX_ID ) )
    {
        enc_run_state->active = false;
        log_err( "[CAM VSPA IN] video enc run mailbox send failed (camera_id=%u)\r\n",
                 (unsigned) msg->camera_id );
        vspa_in_send_fail_to_rx( msg->camera_id, msg->time,
                                 L1C_CAM_VSPA_STATUS_ENC_SEND_FAIL );
        return;
    }

    log_info( "[CAM VSPA IN] video enc run sent to VSPA (camera_id=%u)\r\n",
              (unsigned) msg->camera_id );
}

static void vspa_in_handle_vspa_ack( const struct avi_mbox *rsp_mbox,
                                     vspa_in_ack_state_t *ctrl_state,
                                     vspa_in_ack_state_t *enc_cfg_state,
                                     vspa_in_ack_state_t *enc_run_state )
{
    uint32_t ack_tag = rsp_mbox->msb & L1C_CAM_VSPA_CMD_MASK;

    switch( ack_tag )
    {
        case L1C_CAM_VSPA_ACK_CTRL:
            if( ctrl_state->active )
            {
                S_UNIFIED_MSG_BUFF ack = {
                    .opcode    = ( ( rsp_mbox->msb & L1C_CAM_VSPA_STATUS_PASS ) != 0u )
                                    ? MS_MSG_OPCODE_CTRL_ACK
                                    : MS_MSG_OPCODE_CTRL_ACK_FAIL,
                    .payload   = (void *)(uintptr_t) rsp_mbox->msb,
                    .camera_id = ctrl_state->camera_id,
                    .time      = rsp_mbox->lsb,
                };
                ctrl_state->active = false;
                (void) procx_comm( RX_XC_ID, VSPA_IN_XC_ID, &ack );
            }
            break;

        case L1C_CAM_VSPA_ACK_VIDEO_ENC_CFG:
            if( enc_cfg_state->active )
            {
                S_UNIFIED_MSG_BUFF cfg_ack = {
                    .opcode    = ( ( rsp_mbox->msb & L1C_CAM_VSPA_STATUS_PASS ) != 0u )
                                    ? MS_MSG_OPCODE_VIDEO_ENC_CFG_ACK
                                    : MS_MSG_OPCODE_CTRL_ACK_FAIL,
                    .payload   = (void *)(uintptr_t) rsp_mbox->msb,
                    .camera_id = enc_cfg_state->camera_id,
                    .time      = rsp_mbox->lsb,
                };
                enc_cfg_state->active = false;
                (void) procx_comm( RX_XC_ID, VSPA_IN_XC_ID, &cfg_ack );
            }
            break;

        case L1C_CAM_VSPA_ACK_VIDEO_ENC_RUN:
            if( enc_run_state->active )
            {
                S_UNIFIED_MSG_BUFF done = {
                    .opcode    = ( ( rsp_mbox->msb & L1C_CAM_VSPA_STATUS_PASS ) != 0u )
                                    ? MS_MSG_OPCODE_VIDEO_ENC_DONE
                                    : MS_MSG_OPCODE_CTRL_ACK_FAIL,
                    .payload   = (void *)(uintptr_t) rsp_mbox->msb,
                    .camera_id = enc_run_state->camera_id,
                    .time      = enc_run_state->request_time,
                };
                enc_run_state->active = false;
                (void) procx_comm( RX_XC_ID, VSPA_IN_XC_ID, &done );
            }
            break;

        default:
            log_info( "[CAM VSPA IN] ignoring mailbox msb=0x%08lx lsb=0x%08lx\r\n",
                      (unsigned long) rsp_mbox->msb, (unsigned long) rsp_mbox->lsb );
            break;
    }
}

static void vspa_in_poll_one_mailbox( void *avihndl, uint32_t mbox_id,
                                      vspa_in_ack_state_t *ctrl_state,
                                      vspa_in_ack_state_t *enc_cfg_state,
                                      vspa_in_ack_state_t *enc_run_state )
{
    struct avi_mbox rsp;

    if( 0 == iLa9310AviHostRecvMboxFromVspa( avihndl, &rsp, mbox_id ) )
        vspa_in_handle_vspa_ack( &rsp, ctrl_state, enc_cfg_state, enc_run_state );
}

static void vspa_in_update_timeout( void *avihndl,
                                    vspa_in_ack_state_t *state,
                                    uint32_t limit,
                                    uint32_t status_bit,
                                    const char *label )
{
    if( !state->active )
        return;

    state->poll_count++;
    if( state->poll_count >= limit )
    {
        log_err( "[CAM VSPA IN] %s timeout (camera_id=%u)\r\n",
                 label, (unsigned) state->camera_id );
        vspa_in_send_fail_to_rx( state->camera_id, state->poll_count, status_bit );
        state->active     = false;
        state->poll_count = 0U;
    }
}

static void vspa_in_poll_vspa_mailbox( void *avihndl,
                                       vspa_in_ack_state_t *ctrl_state,
                                       vspa_in_ack_state_t *enc_cfg_state,
                                       vspa_in_ack_state_t *enc_run_state )
{
    if( !vspa_in_any_ack_wait_active( ctrl_state, enc_cfg_state, enc_run_state ) )
        return;

    if( ctrl_state->active )
        vspa_in_poll_one_mailbox( avihndl, L1C_CAM_VSPA_CTRL_MBOX_ID,
                                  ctrl_state, enc_cfg_state, enc_run_state );

    if( enc_cfg_state->active || enc_run_state->active )
        vspa_in_poll_one_mailbox( avihndl, L1C_CAM_VSPA_ENC_MBOX_ID,
                                  ctrl_state, enc_cfg_state, enc_run_state );

    vspa_in_update_timeout( avihndl, ctrl_state,
                            VSPA_IN_ACK_TIMEOUT_POLLS,
                            L1C_CAM_VSPA_STATUS_CTRL_ACK_TIMEOUT, "ctrl ACK" );
    vspa_in_update_timeout( avihndl, enc_cfg_state,
                            VSPA_IN_ACK_TIMEOUT_POLLS,
                            L1C_CAM_VSPA_STATUS_ENC_CFG_TIMEOUT, "video enc cfg ACK" );
    vspa_in_update_timeout( avihndl, enc_run_state,
                            VSPA_IN_VIDEO_ENC_TIMEOUT_POLLS,
                            L1C_CAM_VSPA_STATUS_ENC_RUN_TIMEOUT, "video enc run ACK" );

    if( vspa_in_any_ack_wait_active( ctrl_state, enc_cfg_state, enc_run_state ) )
        taskYIELD();
}

void vspa_in_cb_xc( procx_comm_id_e src_proc, void *data )
{
    Error_t err = Success;

    switch( src_proc )
    {
        case CAMMGR_XC_ID:
            break;
        case VSPA_OUT_XC_ID:
            err = send_cmd_to_proc( &vspa_in_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
            {
                g_GlobalDebugInfo.VspaInQFull++;
                log_err( "[CAM VSPA IN] Msg from VSPA_OUT not pushed in queue\r\n" );
            }
            break;
        case RX_XC_ID:
            if( data != NULL )
                cam_trace_write( 7U, 0x07000000u |
                                 (uint32_t)( (S_UNIFIED_MSG_BUFF *) data )->opcode );
            err = send_cmd_to_proc( &vspa_in_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
            {
                g_GlobalDebugInfo.VspaInQFull++;
                log_err( "[CAM VSPA IN] Msg from RX not pushed in queue\r\n" );
            }
            break;
        case TIMER_HANDLER_XC_ID:
        case TX_XC_ID:
        case VSPA_IN_XC_ID:
        case LOG_XC_ID:
        case SLOT_INTR_XC_ID:
        case RSSI_INTR_XC_ID:
        case L2_TEST_XC_ID:
        default:
            break;
    }
}

void l1_camera_vspa_in_init( uint8_t core_num )
{
    init_proc_cmd_queue( &vspa_in_q, (core_id_t) core_num,
                         sizeof( S_UNIFIED_MSG_BUFF ), MS_VSPAIN_QUEUE_NAME );

    procx_comm_reg( vspa_in_cb_xc, VSPA_IN_XC_ID );
}

void *vspa_in_task( void *arg )
{
    S_UNIFIED_MSG_BUFF  msg;
    size_t              msg_len;
    MsgQ_Priorities_t   msg_prio;
    void               *avihndl;
    vspa_in_ack_state_t ctrl_state    = { false, 0U, 0U, 0U };
    vspa_in_ack_state_t enc_cfg_state = { false, 0U, 0U, 0U };
    vspa_in_ack_state_t enc_run_state = { false, 0U, 0U, 0U };

    (void) arg;

    log_info( "[CAM VSPA IN] task started\r\n" );

    avihndl = iLa9310AviInit();
    if( NULL == avihndl )
    {
        log_err( "[CAM VSPA IN] AVI init failed\r\n" );
        for( ; ; ) { (void) pal_m_sleep( 100 ); }
    }

    for( ; ; )
    {
        if( !vspa_in_any_ack_wait_active( &ctrl_state, &enc_cfg_state, &enc_run_state ) &&
            ( pal_msgq_receive( vspa_in_q.queue, &msg, sizeof( msg ),
                                &msg_len, &msg_prio, 1U ) == Success ) )
        {
            switch( msg.opcode )
            {
                case MS_MSG_OPCODE_VSPA_SEND_CTRL:
                    vspa_in_send_ctrl_to_vspa( avihndl, &msg, &ctrl_state );
                    break;

                case MS_MSG_OPCODE_VSPA_WAIT_ACK:
                    log_err( "[CAM VSPA IN] ignoring legacy VSPA_WAIT_ACK request\r\n" );
                    break;

                case MS_MSG_OPCODE_VIDEO_ENC_CFG:
                    cam_trace_write( 10U, 0x0A000000u | (uint32_t) msg.opcode );
                    vspa_in_send_video_enc_cfg( avihndl, &msg, &enc_cfg_state );
                    break;

                case MS_MSG_OPCODE_VIDEO_ENC_RUN:
                    cam_trace_write( 21U, 0x15000000u | (uint32_t) msg.opcode );
                    vspa_in_run_video_enc( avihndl, &msg, &enc_run_state );
                    break;

                default:
                    break;
            }
        }

        vspa_in_poll_vspa_mailbox( avihndl, &ctrl_state, &enc_cfg_state, &enc_run_state );
    }

    return NULL;
}
