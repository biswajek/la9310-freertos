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

/*------------------------------------------
                DEFINES
--------------------------------------------*/

/** @brief PAL queue name for the VSPA-output message queue. */
#define MS_VSPAOUT_QUEUE_NAME     "vspaOutQ"

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

/**
 * @brief VSPA-output task main loop.
 *
 * @param[in] arg  Unused task argument.
 *
 * @return Never returns.
 */
void *vspa_out_task( void *arg )
{
    (void) arg;

    log_info( "[VSPA OUT] task started\r\n" );

    for( ; ; )
    {
        /* Placeholder loop until VSPA output processing is wired. */
        (void) pal_m_sleep( 10 );
    }

    return NULL;
}
