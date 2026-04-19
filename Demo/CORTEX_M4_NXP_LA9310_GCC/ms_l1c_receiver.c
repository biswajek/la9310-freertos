/*
 * Mundo Sense
 */

/**
 * @file ms_l1c_receiver.c
 * @brief L1 controller receiver subsystem.
 *
 * Provides a PAL message queue for receiver-bound messages and a cross-task
 * callback that forwards messages from registered sources into that queue.
 *
 * CTRL_ACK flow
 * -------------
 * When the TX thread sends a control message over the air it notifies RX via
 * MS_MSG_OPCODE_CTRL_MSG_SENT.  RX then:
 *   1. Configures the VSPA demodulator (stub).
 *   2. Waits up to CTRL_ACK_TIMEOUT_FRAMES frames for an MS_MSG_OPCODE_CTRL_ACK.
 * If the ACK does not arrive within the timeout, RX clears the demodulator
 * configuration (stub) and stops waiting.
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
#include "ms_l1c_modem_mgr.h"
#include "ms_l1c_receiver.h"

/*------------------------------------------
                DEFINES
--------------------------------------------*/

/** @brief PAL queue name for the receiver message queue. */
#define MS_RECEIVER_QUEUE_NAME     "receiverQ"
#define RECEIVER_Q_WAIT_MS         1U

/*------------------------------------------
                VARIABLES
--------------------------------------------*/

/** @brief PAL proc queue that buffers receiver messages for the L1 controller. */
proc_queue_t receiver_q;

/** @brief True while waiting for a CTRL_ACK after a control message was sent. */
static bool     rx_ctrl_ack_pending;

/** @brief Frame number at or after which the ACK wait expires. */
static uint32_t rx_ctrl_ack_deadline;

/** @brief camera_id carried by the pending control message. */
static uint8_t  rx_ctrl_ack_camera_id;

/*------------------------------------------
                STUBS
--------------------------------------------*/

/**
 * @brief Stub: configure the VSPA demodulator when a control message is sent.
 *
 * @param[in] camera_id  Camera channel the control message targets.
 */
static void rx_vspa_demod_configure( uint8_t camera_id )
{
    UNUSED( camera_id );
    log_info( "[RECEIVER] VSPA demod configure (camera_id=%u) [stub]\r\n",
              (unsigned) camera_id );
}

/**
 * @brief Stub: clear the VSPA demodulator configuration on ACK timeout.
 *
 * @param[in] camera_id  Camera channel whose configuration is cleared.
 */
static void rx_vspa_demod_clear_config( uint8_t camera_id )
{
    UNUSED( camera_id );
    log_info( "[RECEIVER] VSPA demod clear config (camera_id=%u) [stub]\r\n",
              (unsigned) camera_id );
}

/*------------------------------------------
                FUNCTIONS
--------------------------------------------*/

/**
 * @brief Cross-task callback invoked when a message arrives for receiver processing.
 *
 * @param[in] src_proc  Source processor ID that sent the message.
 * @param[in] data      Pointer to an @c S_UNIFIED_MSG_BUFF payload.
 */
void receiver_cb_xc( procx_comm_id_e src_proc, void *data )
{
    Error_t err = Success;

    switch( src_proc )
    {
        case MDMMGR_XC_ID:
            break;
        case TIMER_HANDLER_XC_ID:
            break;
        case VSPA_IN_XC_ID:
            err = send_cmd_to_proc( &receiver_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
            {
                log_err( "[RECEIVER] Msg from VSPA IN not pushed in queue\r\n" );
            }
            break;
        case TX_XC_ID:
            /* TX thread signals that a control message has gone over the air. */
            err = send_cmd_to_proc( &receiver_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
            {
                log_err( "[RECEIVER] Msg from TX not pushed in queue\r\n" );
            }
            break;
        case VSPA_OUT_XC_ID:
            break;
        case RX_XC_ID:
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
 * @brief Initializes the L1 receiver subsystem.
 *
 * Creates the receiver PAL message queue and registers the inter-task callback
 * for @c RX_XC_ID.
 *
 * @param[in] core_num  Core number on which this subsystem runs.
 */
void l1_controller_receiver_init( uint8_t core_num )
{
    init_proc_cmd_queue( &receiver_q, (core_id_t) core_num,
                         sizeof( S_UNIFIED_MSG_BUFF ), MS_RECEIVER_QUEUE_NAME );

    rx_ctrl_ack_pending   = false;
    rx_ctrl_ack_deadline  = 0U;
    rx_ctrl_ack_camera_id = 0U;

    procx_comm_reg( receiver_cb_xc, RX_XC_ID );
}

/**
 * @brief Receiver task main loop.
 *
 * @param[in] arg  Unused task argument.
 *
 * @return Never returns.
 */
void *receiver_task( void *arg )
{
    S_UNIFIED_MSG_BUFF rx_msg;
    uint32_t current_frame = 0U;
    uint32_t queue_wait_ms;
    size_t   rx_msg_len;
    MsgQ_Priorities_t rx_msg_prio;

    (void) arg;

    log_info( "[RECEIVER] task started\r\n" );

    for( ; ; )
    {
        /* Drain all currently queued messages. */
        queue_wait_ms = RECEIVER_Q_WAIT_MS;
        while( pal_msgq_receive( receiver_q.queue, &rx_msg, sizeof( rx_msg ),
                                 &rx_msg_len, &rx_msg_prio, queue_wait_ms ) == Success )
        {
            switch( rx_msg.opcode )
            {
                case MS_MSG_OPCODE_CTRL_MSG_SENT:
                    /* TX sent the control message; configure demodulator and
                     * start the ACK wait window. */
                    rx_ctrl_ack_camera_id = rx_msg.camera_id;
                    rx_ctrl_ack_deadline  = rx_msg.time + CTRL_ACK_TIMEOUT_FRAMES;
                    rx_ctrl_ack_pending   = true;
                    rx_vspa_demod_configure( rx_ctrl_ack_camera_id );
                    log_info( "[RECEIVER] Ctrl msg sent indication: "
                              "waiting ACK for %u frames (deadline frame %u)\r\n",
                              (unsigned) CTRL_ACK_TIMEOUT_FRAMES,
                              (unsigned) rx_ctrl_ack_deadline );
                    break;

                case MS_MSG_OPCODE_CTRL_ACK:
                    if( rx_ctrl_ack_pending )
                    {
                        log_info( "[RECEIVER] CTRL_ACK received\r\n" );
                        rx_ctrl_ack_pending = false;
                        /* Notify modem manager so it can relay the ACK to the host. */
                        (void) procx_comm( RX_XC_ID, MDMMGR_XC_ID, &rx_msg );
                    }
                    break;

                default:
                    break;
            }

            queue_wait_ms = 0U;
        }

        /* Check ACK timeout. */
        if( rx_ctrl_ack_pending )
        {
            modem_mgr_get_slot_frame_count( NULL, &current_frame );
            if( current_frame >= rx_ctrl_ack_deadline )
            {
                log_err( "[RECEIVER] CTRL_ACK timeout (camera_id=%u)\r\n",
                         (unsigned) rx_ctrl_ack_camera_id );
                rx_ctrl_ack_pending = false;
                rx_vspa_demod_clear_config( rx_ctrl_ack_camera_id );
            }
        }
    }

    return NULL;
}
