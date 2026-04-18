/*
 * Mundo Sense
 */

/**
 * @file ms_l1c_receiver.c
 * @brief L1 controller receiver subsystem.
 *
 * Provides a PAL message queue for receiver-bound messages and a cross-task
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
#include "ms_procx_comm.h"
#include "ms_l1c_receiver.h"

/*------------------------------------------
                DEFINES
--------------------------------------------*/

/** @brief PAL queue name for the receiver message queue. */
#define MS_RECEIVER_QUEUE_NAME     "receiverQ"

/*------------------------------------------
                VARIABLES
--------------------------------------------*/

/** @brief PAL proc queue that buffers receiver messages for the L1 controller. */
proc_queue_t receiver_q;

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
                log_err( "[RECEIVER] Msg from HOST IN not pushed in queue\r\n" );
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
    (void) arg;

    for( ; ; )
    {
        /* Placeholder loop until receiver processing pipeline is integrated. */
        (void) pal_m_sleep( 10 );
    }

    return NULL;
}