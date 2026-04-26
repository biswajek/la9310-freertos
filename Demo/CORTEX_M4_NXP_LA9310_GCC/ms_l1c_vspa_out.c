/*
 * Mundo Sense
 */

/**
 * @file ms_l1c_vspa_out.c
 * @brief L1 controller VSPA-output subsystem.
 *
 * Manages outbound data from the L1 controller for the VSPA output
 * interface.  Provides a PAL message queue (@c vspa_out_q) that buffers
 * outgoing @c S_UNIFIED_MSG_BUFF messages and a cross-task callback
 * (@c vspa_out_cb_xc) that dispatches to the queue based on the source
 * processor ID.
 */

/*------------------------------------------
                INCLUDES
--------------------------------------------*/
#include <stdbool.h>
#include <stddef.h>
#include "pal_thread.h"
#include "pal_types.h"
#include "ms_global_typedef.h"
#include "ms_globals.h"
#include "ms_logger.h"
#include "ms_l1c_controller.h"
#include "ms_l1_controller_fwk.h"
#include "ms_procx_comm.h"
#include "ms_l1c_vspa_out.h"
#include "la9310_avi.h"
#include "drivers/avi/la9310_avi_ds.h"
#include "vspa_fft_service/src/l1c_vspa_fft_service.h"

/*------------------------------------------
                DEFINES
--------------------------------------------*/

/** @brief PAL queue name for the VSPA-output message queue. */
#define MS_VSPAOUT_QUEUE_NAME        "vspaOutQ"

/** @brief How long VSPA_OUT polls for a LDPC config ACK before giving up (milliseconds). */
#define VSPA_OUT_LDPC_TIMEOUT_MS     100U

/*------------------------------------------
                VARIABLES
--------------------------------------------*/

/** @brief PAL proc queue that buffers VSPA-output messages for the L1 controller. */
proc_queue_t vspa_out_q;

/*------------------------------------------
                FUNCTIONS
--------------------------------------------*/

/**
 * @brief Cross-task callback invoked when a message arrives for the VSPA-out processor.
 *
 * Dispatches on @p src_proc.  For @c RX_XC_ID the message payload is
 * forwarded into @c vspa_out_q via @c send_cmd_to_proc().  All other source
 * IDs are silently ignored (placeholder for future handling).
 *
 * @param[in] src_proc  Source processor ID that sent the message.
 * @param[in] data      Pointer to an @c S_UNIFIED_MSG_BUFF payload.
 *                      Caller retains ownership; the contents are copied
 *                      into the PAL queue before this function returns.
 */
void vspa_out_cb_xc( procx_comm_id_e src_proc, void *data )
{
    Error_t err = Success;

    switch( src_proc )
    {
        case MDMMGR_XC_ID:
            break;
        case TIMER_HANDLER_XC_ID:
            break;
        case VSPA_IN_XC_ID:
            err = send_cmd_to_proc( &vspa_out_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
            {
                g_GlobalDebugInfo.VspaOutQFull++;
                log_err( "[VSPA OUT] Msg from VSPA_IN not pushed in queue\r\n" );
            }
            break;
        case VSPA_OUT_XC_ID:
            break;
        case RX_XC_ID:
            err = send_cmd_to_proc( &vspa_out_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
            {
                g_GlobalDebugInfo.VspaOutQFull++;
                log_err( "[VSPA OUT] Msg from RX not pushed in queue\r\n" );
            }
            break;
        case TX_XC_ID:
            err = send_cmd_to_proc( &vspa_out_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
            {
                g_GlobalDebugInfo.VspaOutQFull++;
                log_err( "[VSPA OUT] Msg from TX not pushed in queue\r\n" );
            }
            break;
        case LOG_XC_ID:
            break;
        case SLOT_INTR_XC_ID:
            break;
        case RSSI_INTR_XC_ID:
            break;
        case L2_TEST_XC_ID:
            break;
        default:
            break;
    }
}

/**
 * @brief Initializes the VSPA-output subsystem for the L1 controller.
 *
 * Creates the VSPA-out PAL message queue via @c init_proc_cmd_queue() and
 * registers @c vspa_out_cb_xc as the cross-task receive handler for
 * @c VSPA_OUT_XC_ID.  Must be called once during system startup on the core
 * identified by @p core_num before the FreeRTOS scheduler starts.
 *
 * @param[in] core_num  Core number on which this subsystem runs.
 */
void l1_controller_vspa_out_init( uint8_t core_num )
{
    init_proc_cmd_queue( &vspa_out_q, (core_id_t) core_num,
                         sizeof( S_UNIFIED_MSG_BUFF ), MS_VSPAOUT_QUEUE_NAME );

    /* Register inter-task communication handler */
    procx_comm_reg( vspa_out_cb_xc, VSPA_OUT_XC_ID );
}

/* -----------------------------------------------------------------------
 * Per-task state for tracking an in-progress LDPC cfg-done ACK wait.
 * ----------------------------------------------------------------------- */
typedef struct
{
    bool     active;       /**< True while waiting for the LDPC cfg-done ACK from VSPA. */
    uint8_t  camera_id;    /**< Camera channel whose LDPC configuration is pending. */
    uint32_t poll_count;   /**< Milliseconds elapsed since the wait started. */
} vspa_out_ldpc_ack_state_t;

/* -----------------------------------------------------------------------
 * Helpers — one function per logical action performed by the task.
 * ----------------------------------------------------------------------- */

/** Sends the control message to VSPA via mailbox then arms VSPA_IN to
 *  watch for the over-air control ACK. */
static void vspa_out_send_ctrl_to_vspa( void *avihndl,
                                         const S_UNIFIED_MSG_BUFF *msg )
{
    if( 0 != iLa9310AviHostSendMboxToVspa( avihndl,
                                            ( uint32_t ) msg->camera_id,
                                            msg->time, 0u ) )
    {
        log_err( "[VSPA OUT] ctrl mailbox send failed (camera_id=%u)\r\n",
                 (unsigned) msg->camera_id );
        return;
    }

    S_UNIFIED_MSG_BUFF ack_req = {
        .opcode    = MS_MSG_OPCODE_VSPA_WAIT_ACK,
        .payload   = NULL,
        .camera_id = msg->camera_id,
        .time      = msg->time,
    };
    (void) procx_comm( VSPA_IN_XC_ID, VSPA_OUT_XC_ID, &ack_req );
}

/** Records that VSPA_IN sent the LDPC cfg command and we should now
 *  watch for VSPA's configuration-done ACK. */
static void vspa_out_start_ldpc_cfg_done_ack_wait( vspa_out_ldpc_ack_state_t *state,
                                                    const S_UNIFIED_MSG_BUFF  *msg )
{
    state->active     = true;
    state->camera_id  = msg->camera_id;
    state->poll_count = 0U;
    log_info( "[VSPA OUT] waiting for LDPC cfg-done ACK (camera_id=%u)\r\n",
              (unsigned) msg->camera_id );
}

/** Polls the VSPA incoming mailbox once for the LDPC cfg-done ACK.
 *  Clears @p state on receipt or timeout. */
static void vspa_out_poll_ldpc_cfg_done_ack( void *avihndl,
                                              vspa_out_ldpc_ack_state_t *state )
{
    struct avi_mbox rsp_mbox;

    if( !state->active )
    {
        return;
    }

    if( 0 == iLa9310AviHostRecvMboxFromVspa( avihndl, &rsp_mbox, 0u ) )
    {
        if( ( rsp_mbox.msb & L1C_VSPA_CMD_MASK ) == L1C_VSPA_ACK_LDPC_FECU_TEST )
        {
            log_info( "[VSPA OUT] LDPC cfg-done ACK received (camera_id=%u status=0x%08lx)\r\n",
                      (unsigned) state->camera_id, ( unsigned long ) rsp_mbox.msb );
            state->active = false;

            /* Inform the receiver task so it can advance to the trigger step. */
            S_UNIFIED_MSG_BUFF cfg_ack = {
                .opcode    = MS_MSG_OPCODE_LDPC_CFG_ACK,
                .payload   = NULL,
                .camera_id = state->camera_id,
                .time      = rsp_mbox.lsb,
            };
            (void) procx_comm( RX_XC_ID, VSPA_OUT_XC_ID, &cfg_ack );
        }
        else
        {
            log_info( "[VSPA OUT] LDPC poll: ignoring mailbox msb=0x%08lx lsb=0x%08lx\r\n",
                      ( unsigned long ) rsp_mbox.msb, ( unsigned long ) rsp_mbox.lsb );
        }
        return;
    }

    state->poll_count++;
    if( state->poll_count >= VSPA_OUT_LDPC_TIMEOUT_MS )
    {
        log_err( "[VSPA OUT] LDPC cfg-done ACK timeout (camera_id=%u)\r\n",
                 (unsigned) state->camera_id );
        state->active = false;
    }
    else
    {
        (void) pal_m_sleep( 1 );
    }
}

/* -----------------------------------------------------------------------
 * Task entry point
 * ----------------------------------------------------------------------- */

/**
 * @brief VSPA-output task main loop.
 *
 * Responsibilities:
 *   1. On @c VSPA_SEND_CTRL              — send the control message to VSPA via
 *                                           mailbox and ask VSPA_IN to wait for the
 *                                           over-air control ACK.
 *   2. On @c VSPA_WAIT_LDPC_CFG_DONE_ACK — arm LDPC cfg-done ACK polling
 *                                           (requested by VSPA_IN after sending the
 *                                           LDPC configuration mailbox).
 *   3. While LDPC ACK wait is active      — poll the VSPA incoming mailbox each ms
 *                                           until the cfg-done ACK arrives or
 *                                           the wait times out.
 *
 * @param[in] arg  Unused task argument.
 * @return Never returns.
 */
void *vspa_out_task( void *arg )
{
    S_UNIFIED_MSG_BUFF        msg;
    size_t                    msg_len;
    MsgQ_Priorities_t         msg_prio;
    void                     *avihndl;
    vspa_out_ldpc_ack_state_t ldpc_ack_state = { false, 0U, 0U };

    (void) arg;

    log_info( "[VSPA OUT] task started\r\n" );

    avihndl = iLa9310AviInit();
    if( NULL == avihndl )
    {
        log_err( "[VSPA OUT] AVI init failed\r\n" );
        for( ; ; ) { (void) pal_m_sleep( 100 ); }
    }

    for( ; ; )
    {
        /* Block on the queue only when idle; poll without blocking when
         * an LDPC cfg-done ACK wait is in progress. */
        uint32_t recv_timeout_ms = ldpc_ack_state.active ? 0U : 1U;
        while( pal_msgq_receive( vspa_out_q.queue, &msg, sizeof( msg ),
                                 &msg_len, &msg_prio, recv_timeout_ms ) == Success )
        {
            switch( msg.opcode )
            {
                case MS_MSG_OPCODE_VSPA_SEND_CTRL:
                    vspa_out_send_ctrl_to_vspa( avihndl, &msg );
                    break;

                case MS_MSG_OPCODE_VSPA_WAIT_LDPC_CFG_DONE_ACK:
                    vspa_out_start_ldpc_cfg_done_ack_wait( &ldpc_ack_state, &msg );
                    break;

                default:
                    break;
            }
            recv_timeout_ms = 0U;
        }

        vspa_out_poll_ldpc_cfg_done_ack( avihndl, &ldpc_ack_state );
    }

    return NULL;
}
