/*
 * Mundo Sense
 */

/**
 * @file ms_l1c_vspa_in.c
 * @brief L1 controller VSPA-input subsystem.
 *
 * Manages inbound data for the VSPA input path in the L1
 * controller.  Provides a PAL message queue (@c vspa_in_q) that buffers
 * incoming @c S_UNIFIED_MSG_BUFF messages and a cross-task callback
 * (@c vspa_in_cb_xc) that dispatches to the queue based on the source
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
#include "ms_l1c_vspa_in.h"
#include "la9310_avi.h"
#include "drivers/avi/la9310_avi_ds.h"
#include "vspa_fft_service/src/l1c_vspa_fft_service.h"

/*------------------------------------------
                DEFINES
--------------------------------------------*/

/** @brief PAL queue name for the VSPA-input message queue. */
#define MS_VSPAIN_QUEUE_NAME        "vspaInQ"

/** @brief How long VSPA_IN polls for an ACK before giving up (milliseconds). */
#define VSPA_IN_ACK_TIMEOUT_MS      100U

/*------------------------------------------
                VARIABLES
--------------------------------------------*/

/** @brief PAL proc queue that buffers VSPA-input messages for the L1 controller. */
proc_queue_t vspa_in_q;

/*------------------------------------------
                FUNCTIONS
--------------------------------------------*/

/**
 * @brief Cross-task callback invoked when a message arrives for the VSPA-in processor.
 *
 * Dispatches on @p src_proc.  For @c L2_TEST_XC_ID the message payload is
 * forwarded into @c vspa_in_q via @c send_cmd_to_proc().  All other source
 * IDs are silently ignored (placeholder for future handling).
 *
 * @param[in] src_proc  Source processor ID that sent the message.
 * @param[in] data      Pointer to an @c S_UNIFIED_MSG_BUFF payload.
 *                      Caller retains ownership; the contents are copied
 *                      into the PAL queue before this function returns.
 */
void vspa_in_cb_xc( procx_comm_id_e src_proc, void *data )
{
    Error_t err = Success;

    switch( src_proc )
    {
        case MDMMGR_XC_ID:
            break;
        case TIMER_HANDLER_XC_ID:
            break;
        case VSPA_IN_XC_ID:
            break;
        case VSPA_OUT_XC_ID:
            err = send_cmd_to_proc( &vspa_in_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
            {
                g_GlobalDebugInfo.VspaInQFull++;
                log_err( "[VSPA IN] Msg from VSPA_OUT not pushed in queue\r\n" );
            }
            break;
        case RX_XC_ID:
            break;
        case TX_XC_ID:
            break;
        case LOG_XC_ID:
            break;
        case SLOT_INTR_XC_ID:
            break;
        case RSSI_INTR_XC_ID:
            break;
        case L2_TEST_XC_ID:
            err = send_cmd_to_proc( &vspa_in_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
            {
                g_GlobalDebugInfo.VspaInQFull++;
                log_err( "[VSPA IN] Msg from TEST FW not pushed in queue\r\n" );
            }
            break;
        default:
            break;
    }
}

/**
 * @brief Initializes the VSPA-input subsystem for the L1 controller.
 *
 * Creates the VSPA-in PAL message queue via @c init_proc_cmd_queue() and
 * registers @c vspa_in_cb_xc as the cross-task receive handler for
 * @c VSPA_IN_XC_ID.  Must be called once during system startup on the core
 * identified by @p core_num before the FreeRTOS scheduler starts.
 *
 * @param[in] core_num  Core number on which this subsystem runs.
 */
void l1_controller_vspa_in_init( uint8_t core_num )
{
    init_proc_cmd_queue( &vspa_in_q, (core_id_t) core_num,
                         sizeof( S_UNIFIED_MSG_BUFF ), MS_VSPAIN_QUEUE_NAME );

    /* Register inter-task communication handler */
    procx_comm_reg( vspa_in_cb_xc, VSPA_IN_XC_ID );
}

/* -----------------------------------------------------------------------
 * Per-task state for tracking an in-progress control-message ACK wait.
 * ----------------------------------------------------------------------- */
typedef struct
{
    bool     active;       /**< True while waiting for a CTRL_ACK from VSPA. */
    uint8_t  camera_id;    /**< Camera channel the pending control message targets. */
    uint32_t poll_count;   /**< Milliseconds elapsed since the wait started. */
} vspa_in_ctrl_ack_state_t;

/* -----------------------------------------------------------------------
 * Helpers — one function per logical action performed by the task.
 * ----------------------------------------------------------------------- */

/** Records that VSPA_OUT asked us to wait for a control-message ACK. */
static void vspa_in_start_ctrl_ack_wait( vspa_in_ctrl_ack_state_t *state,
                                         const S_UNIFIED_MSG_BUFF  *msg )
{
    state->active     = true;
    state->camera_id  = msg->camera_id;
    state->poll_count = 0U;
    log_info( "[VSPA IN] waiting for ctrl ACK (camera_id=%u)\r\n",
              (unsigned) msg->camera_id );
}

/** Sends the LDPC FECU configuration command to VSPA then asks VSPA_OUT
 *  to watch for the configuration-done ACK. */
static void vspa_in_send_ldpc_cfg_to_vspa( void *avihndl,
                                            const S_UNIFIED_MSG_BUFF *msg )
{
    if( 0 != iLa9310AviHostSendMboxToVspa( avihndl,
                                            L1C_VSPA_CMD_LDPC_FECU_TEST |
                                                ( uint32_t ) msg->camera_id,
                                            ( uint32_t ) msg->camera_id, 0u ) )
    {
        log_err( "[VSPA IN] LDPC cfg mailbox send failed (camera_id=%u)\r\n",
                 (unsigned) msg->camera_id );
        return;
    }

    log_info( "[VSPA IN] LDPC cfg sent to VSPA (camera_id=%u)\r\n",
              (unsigned) msg->camera_id );

    S_UNIFIED_MSG_BUFF ldpc_wait = {
        .opcode    = MS_MSG_OPCODE_VSPA_WAIT_LDPC_CFG_DONE_ACK,
        .payload   = NULL,
        .camera_id = msg->camera_id,
        .time      = msg->time,
    };
    (void) procx_comm( VSPA_OUT_XC_ID, VSPA_IN_XC_ID, &ldpc_wait );
}

/** Sends the LDPC trigger command to VSPA then immediately notifies the
 *  receiver task that the trigger has been dispatched. */
static void vspa_in_send_ldpc_trigger_to_vspa( void *avihndl,
                                                const S_UNIFIED_MSG_BUFF *msg )
{
    if( 0 != iLa9310AviHostSendMboxToVspa( avihndl,
                                            L1C_VSPA_CMD_LDPC_FECU_TEST |
                                                ( uint32_t ) msg->camera_id,
                                            ( uint32_t ) msg->camera_id, 0u ) )
    {
        log_err( "[VSPA IN] LDPC trigger mailbox send failed (camera_id=%u)\r\n",
                 (unsigned) msg->camera_id );
        return;
    }

    log_info( "[VSPA IN] LDPC trigger sent to VSPA (camera_id=%u)\r\n",
              (unsigned) msg->camera_id );

    S_UNIFIED_MSG_BUFF triggered = {
        .opcode    = MS_MSG_OPCODE_LDPC_TRIGGERED,
        .payload   = NULL,
        .camera_id = msg->camera_id,
        .time      = msg->time,
    };
    (void) procx_comm( RX_XC_ID, VSPA_IN_XC_ID, &triggered );
}

/** Polls the VSPA incoming mailbox once for a control-message ACK.
 *  Clears @p state on success or timeout and notifies the receiver. */
static void vspa_in_poll_ctrl_ack( void *avihndl,
                                    vspa_in_ctrl_ack_state_t *state )
{
    struct avi_mbox rsp_mbox;

    if( !state->active )
    {
        return;
    }

    if( 0 == iLa9310AviHostRecvMboxFromVspa( avihndl, &rsp_mbox, 0u ) )
    {
        state->active = false;
        S_UNIFIED_MSG_BUFF ack_msg = {
            .opcode    = MS_MSG_OPCODE_CTRL_ACK,
            .payload   = NULL,
            .camera_id = state->camera_id,
            .time      = rsp_mbox.lsb,
        };
        (void) procx_comm( RX_XC_ID, VSPA_IN_XC_ID, &ack_msg );
        return;
    }

    state->poll_count++;
    if( state->poll_count >= VSPA_IN_ACK_TIMEOUT_MS )
    {
        log_err( "[VSPA IN] ctrl ACK timeout (camera_id=%u)\r\n",
                 (unsigned) state->camera_id );
        state->active     = false;
        state->poll_count = 0U;
        S_UNIFIED_MSG_BUFF fail_msg = {
            .opcode    = MS_MSG_OPCODE_CTRL_ACK_FAIL,
            .payload   = NULL,
            .camera_id = state->camera_id,
            .time      = 0U,
        };
        (void) procx_comm( RX_XC_ID, VSPA_IN_XC_ID, &fail_msg );
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
 * @brief VSPA-input task main loop.
 *
 * Responsibilities:
 *   1. On @c VSPA_WAIT_ACK      — arm ctrl-ACK polling (requested by VSPA_OUT).
 *   2. On @c LDPC_CFG           — send LDPC FECU command to VSPA via mailbox
 *                                  and ask VSPA_OUT to wait for the cfg-done ACK.
 *   3. While ctrl-ACK is pending — poll the VSPA incoming mailbox each ms;
 *                                   forward result (ACK or timeout) to receiver.
 *
 * @param[in] arg  Unused task argument.
 * @return Never returns.
 */
void *vspa_in_task( void *arg )
{
    S_UNIFIED_MSG_BUFF       msg;
    size_t                   msg_len;
    MsgQ_Priorities_t        msg_prio;
    void                    *avihndl;
    vspa_in_ctrl_ack_state_t ctrl_ack_state = { false, 0U, 0U };

    (void) arg;

    log_info( "[VSPA IN] task started\r\n" );

    avihndl = iLa9310AviInit();
    if( NULL == avihndl )
    {
        log_err( "[VSPA IN] AVI init failed\r\n" );
        for( ; ; ) { (void) pal_m_sleep( 100 ); }
    }

    for( ; ; )
    {
        /* Block on the queue only when idle; poll without blocking when
         * a ctrl-ACK wait is in progress so we don't miss the response. */
        uint32_t recv_timeout_ms = ctrl_ack_state.active ? 0U : 1U;
        while( pal_msgq_receive( vspa_in_q.queue, &msg, sizeof( msg ),
                                 &msg_len, &msg_prio, recv_timeout_ms ) == Success )
        {
            switch( msg.opcode )
            {
                case MS_MSG_OPCODE_VSPA_WAIT_ACK:
                    vspa_in_start_ctrl_ack_wait( &ctrl_ack_state, &msg );
                    break;

                case MS_MSG_OPCODE_LDPC_CFG:
                    vspa_in_send_ldpc_cfg_to_vspa( avihndl, &msg );
                    break;

                case MS_MSG_OPCODE_LDPC_TRIG:
                    vspa_in_send_ldpc_trigger_to_vspa( avihndl, &msg );
                    break;

                default:
                    break;
            }
            recv_timeout_ms = 0U;
        }

        vspa_in_poll_ctrl_ack( avihndl, &ctrl_ack_state );
    }

    return NULL;
}
