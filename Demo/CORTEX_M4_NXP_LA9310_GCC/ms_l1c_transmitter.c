/*
 * Mundo Sense
 */

/**
 * @file ms_l1c_transmitter.c
 * @brief L1 controller transmitter subsystem.
 *
 * Provides a PAL message queue for transmitter-bound messages and a cross-task
 * callback that forwards messages from registered sources into that queue.
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
#include "ms_l1c_modem_mgr.h"
#include "ms_procx_comm.h"
#include "ms_l1c_transmitter.h"
#include "ms_l1c_receiver.h"

/*------------------------------------------
                DEFINES
--------------------------------------------*/

/** @brief PAL queue name for the transmitter message queue. */
#define MS_TRANSMITTER_QUEUE_NAME     "transmitterQ"
#define TRANSMITTER_Q_WAIT_MS         1U
#define MAX_CAMERA_ID                 5U

/*------------------------------------------
                VARIABLES
--------------------------------------------*/

/** @brief PAL proc queue that buffers transmitter messages for the L1 controller. */
proc_queue_t transmitter_q;
static bool pending_ctrl_msg_valid;
static uint32_t pending_ctrl_msg_frame;
static S_UNIFIED_MSG_BUFF pending_ctrl_msg;

/*------------------------------------------
                FUNCTIONS
--------------------------------------------*/

/**
 * @brief Schedules a control message for transmission in slot 1 of next frame.
 *
 * @param[in] msg  Pointer to validated control message.
 */
static void transmitter_schedule_control_msg( const S_UNIFIED_MSG_BUFF *msg )
{
    uint32_t current_frame = 0U;

    if( ( msg == NULL ) || ( msg->camera_id > MAX_CAMERA_ID ) )
    {
        return;
    }

    modem_mgr_get_slot_frame_count( NULL, &current_frame );

    pending_ctrl_msg = *msg;
    pending_ctrl_msg_valid = true;
    pending_ctrl_msg_frame = current_frame + 1U;
}

/**
 * @brief Stub for control-message over-air transmit path.
 *
 * @param[in] msg  Pointer to control message to transmit.
 */
static void transmitter_send_control_over_air( const S_UNIFIED_MSG_BUFF *msg )
{
    UNUSED( msg );
}

/**
 * @brief Stub for BCH over-air transmit path.
 *
 * @param[in] msg  Pointer to BCH message to transmit.
 */
static void transmitter_send_bch_over_air( const S_UNIFIED_MSG_BUFF *msg )
{
    UNUSED( msg );
}

/**
 * @brief Cross-task callback invoked when a message arrives for transmitter processing.
 *
 * @param[in] src_proc  Source processor ID that sent the message.
 * @param[in] data      Pointer to an @c S_UNIFIED_MSG_BUFF payload.
 */
void transmitter_cb_xc( procx_comm_id_e src_proc, void *data )
{
    Error_t err = Success;

    switch( src_proc )
    {
        case MDMMGR_XC_ID:
            err = send_cmd_to_proc( &transmitter_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
            {
                log_err( "[TRANSMITTER] Msg from modem manager not pushed in queue\r\n" );
            }
            break;
        case TIMER_HANDLER_XC_ID:
            break;
        case VSPA_IN_XC_ID:
            err = send_cmd_to_proc( &transmitter_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
            {
                log_err( "[TRANSMITTER] Msg from HOST IN not pushed in queue\r\n" );
            }
            break;
        case VSPA_OUT_XC_ID:
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
            break;
        default:
            break;
    }
}

/**
 * @brief Initializes the L1 transmitter subsystem.
 *
 * Creates the transmitter PAL message queue and registers the inter-task callback
 * for @c TX_XC_ID.
 *
 * @param[in] core_num  Core number on which this subsystem runs.
 */
void l1_controller_transmitter_init( uint8_t core_num )
{
    init_proc_cmd_queue( &transmitter_q, (core_id_t) core_num,
                         sizeof( S_UNIFIED_MSG_BUFF ), MS_TRANSMITTER_QUEUE_NAME );

    pending_ctrl_msg_valid = false;
    pending_ctrl_msg_frame = 0U;
    pending_ctrl_msg.opcode = MS_MSG_OPCODE_CONTROL_MSG;
    pending_ctrl_msg.payload = NULL;
    pending_ctrl_msg.camera_id = 0U;
    pending_ctrl_msg.time = 0U;

    procx_comm_reg( transmitter_cb_xc, TX_XC_ID );
}

void *transmitter_task( void *arg )
{
    S_UNIFIED_MSG_BUFF rx_msg;
    uint32_t current_slot = 0U;
    uint32_t current_frame = 0U;
    uint32_t queue_wait_ms;
    size_t rx_msg_len;
    MsgQ_Priorities_t rx_msg_prio;

    (void) arg;

    log_info( "[TRANSMITTER] task started\r\n" );

    for( ; ; )
    {
        /* Wait for first queue message, then drain all currently queued messages. */
        queue_wait_ms = TRANSMITTER_Q_WAIT_MS;
        while( pal_msgq_receive( transmitter_q.queue, &rx_msg, sizeof( rx_msg ),
                                 &rx_msg_len, &rx_msg_prio, queue_wait_ms ) == Success )
        {
            switch( rx_msg.opcode )
            {
                case MS_MSG_OPCODE_CONTROL_MSG:
                    transmitter_schedule_control_msg( &rx_msg );
                    break;

                case MS_MSG_OPCODE_BCH_SEND:
                    transmitter_send_bch_over_air( &rx_msg );
                    break;

                default:
                    break;
            }

            queue_wait_ms = 0U;
        }

        /* Read globally shared slot/frame maintained by modem manager. */
        modem_mgr_get_slot_frame_count( &current_slot, &current_frame );

        /* Control message must go in slot 1 of its scheduled frame. */
        if( pending_ctrl_msg_valid &&
            ( current_slot == 1U ) &&
            ( current_frame == pending_ctrl_msg_frame ) )
        {
            transmitter_send_control_over_air( &pending_ctrl_msg );
            pending_ctrl_msg_valid = false;

            /* Notify RX thread that the control message has gone over the air. */
            S_UNIFIED_MSG_BUFF ind = {
                .opcode    = MS_MSG_OPCODE_CTRL_MSG_SENT,
                .payload   = NULL,
                .camera_id = pending_ctrl_msg.camera_id,
                .time      = current_frame,
            };
            (void) procx_comm( TX_XC_ID, RX_XC_ID, &ind );
        }

    }

    return NULL;
}