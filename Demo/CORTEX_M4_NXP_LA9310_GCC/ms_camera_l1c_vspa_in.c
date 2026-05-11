/*
 * Mundo Sense
 */

/**
 * @file ms_camera_l1c_vspa_in.c
 * @brief Camera VSPA-input — LDPC BCH decoder and TX encoder mailbox interface.
 *
 * State machine:
 *
 *  IDLE ──(BCH_LDPC_CFG from RX)──> send LDPC cfg mailbox to VSPA
 *                                 ──> WAIT_LDPC_CFG_ACK
 *
 *  WAIT_LDPC_CFG_ACK ──(cfg-done ACK from VSPA)──> WAIT_BCH_DECODED
 *                    ──(timeout)──> BCH_DECODE_FAIL to RX ──> IDLE
 *
 *  WAIT_BCH_DECODED ──(BCH decoded from VSPA, carries camera_id)──>
 *                       BCH_DECODED to RX ──> IDLE
 *                   (no timeout — waits indefinitely until VSPA decodes BCH)
 *
 *  IDLE ──(TX_ENC_CFG from TX)──> send encoder cfg mailbox to VSPA
 *                               ──> WAIT_ENC_CFG_ACK
 *
 *  WAIT_ENC_CFG_ACK ──(cfg-done ACK from VSPA)──> TX_ENC_CFG_ACK to TX ──> IDLE
 *                   ──(timeout)──> log error ──> IDLE
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

#define MS_VSPAIN_QUEUE_NAME                "camVspaInQ"

#define VSPA_IN_LDPC_CFG_TIMEOUT_POLLS      10000U
#define VSPA_IN_ENC_CFG_TIMEOUT_POLLS       10000U

/* VSPA mailbox IDs */
#define L1C_CAM_VSPA_BCH_MBOX_ID            0U   /* LDPC BCH decoder mailbox */
#define L1C_CAM_VSPA_ENC_MBOX_ID            1U   /* TX encoder mailbox */

/* VSPA command/ACK tags (upper nibble of msb) */
#define L1C_CAM_VSPA_CMD_LDPC_CFG          0x10000000u
#define L1C_CAM_VSPA_ACK_LDPC_CFG_DONE     0x10000000u
#define L1C_CAM_VSPA_ACK_BCH_DECODED       0x20000000u
#define L1C_CAM_VSPA_CMD_ENC_CFG           0x30000000u
#define L1C_CAM_VSPA_ACK_ENC_CFG_DONE      0x30000000u
#define L1C_CAM_VSPA_CMD_MASK              0xF0000000u

/* camera_id packed in bits[15:8] of the BCH-decoded ACK msb */
#define L1C_CAM_VSPA_BCH_CAMERA_ID_SHIFT   8U
#define L1C_CAM_VSPA_BCH_CAMERA_ID_MASK    0x0000FF00u

proc_queue_t vspa_in_q;

typedef enum
{
    CAM_VSPA_IN_IDLE = 0,
    CAM_VSPA_IN_WAIT_LDPC_CFG_ACK,
    CAM_VSPA_IN_WAIT_BCH_DECODED,
    CAM_VSPA_IN_WAIT_ENC_CFG_ACK,
} cam_vspa_in_state_t;

static void vspa_in_send_bch_fail_to_rx( void )
{
    S_UNIFIED_MSG_BUFF fail = {
        .opcode    = MS_MSG_OPCODE_BCH_DECODE_FAIL,
        .camera_id = 0U,
        .time      = 0U,
    };
    (void) procx_comm( RX_XC_ID, VSPA_IN_XC_ID, &fail );
}

void vspa_in_cb_xc( procx_comm_id_e src_proc, void *data )
{
    Error_t err = Success;

    switch( src_proc )
    {
        case RX_XC_ID:   /* BCH_LDPC_CFG */
        case TX_XC_ID:   /* TX_ENC_CFG   */
            cam_trace_write( 7U, 0x07000000u |
                             (uint32_t)( (S_UNIFIED_MSG_BUFF *) data )->opcode );
            err = send_cmd_to_proc( &vspa_in_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
            {
                g_GlobalDebugInfo.VspaInQFull++;
                log_err( "[CAM VSPA IN] Msg not pushed in queue (src=%d)\r\n",
                         (int) src_proc );
            }
            break;
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
    S_UNIFIED_MSG_BUFF   msg;
    size_t               msg_len;
    MsgQ_Priorities_t    msg_prio;
    void                *avihndl;
    cam_vspa_in_state_t  state      = CAM_VSPA_IN_IDLE;
    uint32_t             poll_count = 0U;

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
        /* Accept new commands only when idle. */
        if( state == CAM_VSPA_IN_IDLE )
        {
            if( pal_msgq_receive( vspa_in_q.queue, &msg, sizeof( msg ),
                                  &msg_len, &msg_prio, 1U ) != Success )
            {
                continue;
            }

            if( msg.opcode == MS_MSG_OPCODE_BCH_LDPC_CFG )
            {
                cam_trace_write( 8U, 0x08000000u );
                log_info( "[CAM VSPA IN] sending LDPC cfg to VSPA\r\n" );

                if( 0 != iLa9310AviHostSendMboxToVspa( avihndl,
                                                        L1C_CAM_VSPA_CMD_LDPC_CFG,
                                                        0U,
                                                        L1C_CAM_VSPA_BCH_MBOX_ID ) )
                {
                    log_err( "[CAM VSPA IN] LDPC cfg mailbox send failed\r\n" );
                    vspa_in_send_bch_fail_to_rx();
                }
                else
                {
                    state      = CAM_VSPA_IN_WAIT_LDPC_CFG_ACK;
                    poll_count = 0U;
                }
            }
            else if( msg.opcode == MS_MSG_OPCODE_TX_ENC_CFG )
            {
                cam_trace_write( 10U, 0x0A000000u );
                log_info( "[CAM VSPA IN] sending TX encoder cfg to VSPA\r\n" );

                if( 0 != iLa9310AviHostSendMboxToVspa( avihndl,
                                                        L1C_CAM_VSPA_CMD_ENC_CFG |
                                                            (uint32_t) msg.camera_id,
                                                        msg.time,
                                                        L1C_CAM_VSPA_ENC_MBOX_ID ) )
                {
                    log_err( "[CAM VSPA IN] TX encoder cfg mailbox send failed\r\n" );
                    /* Notify TX of failure so it can recover. */
                    S_UNIFIED_MSG_BUFF fail = {
                        .opcode    = MS_MSG_OPCODE_TX_ENC_CFG_ACK,
                        .camera_id = msg.camera_id,
                        .time      = msg.time,
                    };
                    (void) procx_comm( TX_XC_ID, VSPA_IN_XC_ID, &fail );
                }
                else
                {
                    state      = CAM_VSPA_IN_WAIT_ENC_CFG_ACK;
                    poll_count = 0U;
                }
            }
            continue;
        }

        /* Poll the VSPA mailbox (non-blocking) based on active state. */
        uint32_t mbox_id = ( state == CAM_VSPA_IN_WAIT_ENC_CFG_ACK )
                           ? L1C_CAM_VSPA_ENC_MBOX_ID
                           : L1C_CAM_VSPA_BCH_MBOX_ID;

        struct avi_mbox rsp;
        if( 0 == iLa9310AviHostRecvMboxFromVspa( avihndl, &rsp, mbox_id ) )
        {
            uint32_t tag = rsp.msb & L1C_CAM_VSPA_CMD_MASK;

            if( ( state == CAM_VSPA_IN_WAIT_LDPC_CFG_ACK ) &&
                ( tag == L1C_CAM_VSPA_ACK_LDPC_CFG_DONE ) )
            {
                log_info( "[CAM VSPA IN] LDPC cfg ACK — waiting for BCH decode\r\n" );
                state      = CAM_VSPA_IN_WAIT_BCH_DECODED;
                poll_count = 0U;
            }
            else if( ( state == CAM_VSPA_IN_WAIT_BCH_DECODED ) &&
                     ( tag == L1C_CAM_VSPA_ACK_BCH_DECODED ) )
            {
                uint8_t camera_id = (uint8_t) ( ( rsp.msb &
                                                   L1C_CAM_VSPA_BCH_CAMERA_ID_MASK ) >>
                                                 L1C_CAM_VSPA_BCH_CAMERA_ID_SHIFT );

                cam_trace_write( 9U, 0x09000000u | (uint32_t) camera_id );
                log_info( "[CAM VSPA IN] BCH decoded (camera_id=%u) — notifying RX\r\n",
                          (unsigned) camera_id );

                S_UNIFIED_MSG_BUFF decoded = {
                    .opcode    = MS_MSG_OPCODE_BCH_DECODED,
                    .camera_id = camera_id,
                    .time      = rsp.lsb,
                };
                (void) procx_comm( RX_XC_ID, VSPA_IN_XC_ID, &decoded );
                state      = CAM_VSPA_IN_IDLE;
                poll_count = 0U;
            }
            else if( ( state == CAM_VSPA_IN_WAIT_ENC_CFG_ACK ) &&
                     ( tag == L1C_CAM_VSPA_ACK_ENC_CFG_DONE ) )
            {
                cam_trace_write( 11U, 0x0B000000u );
                log_info( "[CAM VSPA IN] TX encoder cfg ACK — notifying TX\r\n" );

                S_UNIFIED_MSG_BUFF enc_ack = {
                    .opcode    = MS_MSG_OPCODE_TX_ENC_CFG_ACK,
                    .camera_id = (uint8_t) ( rsp.msb & 0xFFu ),
                    .time      = rsp.lsb,
                };
                (void) procx_comm( TX_XC_ID, VSPA_IN_XC_ID, &enc_ack );
                state      = CAM_VSPA_IN_IDLE;
                poll_count = 0U;
            }
            continue;
        }

        /* BCH-decode wait is unbounded — no timeout. */
        if( state != CAM_VSPA_IN_WAIT_BCH_DECODED )
        {
            poll_count++;
            uint32_t limit = ( state == CAM_VSPA_IN_WAIT_LDPC_CFG_ACK )
                             ? VSPA_IN_LDPC_CFG_TIMEOUT_POLLS
                             : VSPA_IN_ENC_CFG_TIMEOUT_POLLS;

            if( poll_count >= limit )
            {
                log_err( "[CAM VSPA IN] timeout in state %d\r\n", (int) state );

                if( state != CAM_VSPA_IN_WAIT_ENC_CFG_ACK )
                    vspa_in_send_bch_fail_to_rx();

                state      = CAM_VSPA_IN_IDLE;
                poll_count = 0U;
                continue;
            }
        }
        taskYIELD();
    }

    return NULL;
}
