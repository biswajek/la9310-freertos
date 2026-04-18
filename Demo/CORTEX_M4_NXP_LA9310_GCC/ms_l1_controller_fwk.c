/*
 * Mundo Sense
 */

/***********************************************************************
                                INCLUDES
***********************************************************************/
#include <stdbool.h>
#include <stddef.h>
#include "pal_types.h"
#include "pal_msgq.h"
#include "ms_l1_controller_fwk.h"
#include "ms_globals.h"
#include "ms_logger.h"
#include "ipiQueue.h"


/***********************************************************************
                                VARIABLES
***********************************************************************/
/* Tracks the last IPI event ID allocated per core, one entry per core_id_t value */
IPIEventID_t last_assigned_ipi_id[ L1_CORE_1 + 1 ] = { IPI_EVT_ID0, IPI_EVT_ID0 };


/***********************************************************************
                                FUNCTIONS
***********************************************************************/

/***********************************************************************

***********************************************************************/
/**
 * @brief Initializes a procedure command queue for inter-task/core communication.
 *
 * Allocates the next available IPI event ID for the given core and registers
 * it with the IPI subsystem backed by a PAL message queue sized to hold
 * @p sizeOfElem-byte messages.  The resulting queue handle and IPI event ID
 * are stored in @p queue so that callers can use @c send_cmd_to_proc()
 * without tracking the IPI internals.
 *
 * The registration and queue creation are only performed when @p core_id
 * matches the currently executing core; on other cores only the tracking
 * state is updated.
 *
 * @param[out] queue       Procedure queue descriptor to initialise.
 * @param[in]  core_id     Core on which the receiving task runs.
 * @param[in]  sizeOfElem  Maximum message size in bytes for the PAL queue.
 * @param[in]  mqName      Null-terminated name for the underlying PAL queue.
 *
 * @return @c Success on successful registration, or @c Failure if the IPI
 *         subsystem could not allocate the requested event ID.
 */
Error_t init_proc_cmd_queue( proc_queue_t *queue, core_id_t core_id,
                              size_t sizeOfElem, const char *mqName )
{
    IPIEventID_t recv_ev_id;
    IPIEventID_t next_ipi_ev_id;

    /* Core on which the procedure will run */
    queue->core_id = (uint8_t) core_id;

    /* Get the next available IPI event ID for this core */
    next_ipi_ev_id = (IPIEventID_t) ( last_assigned_ipi_id[ core_id ] + 1 );

    if( core_id == (core_id_t) g_GlobalDebugInfo.CoreNum )
    {
        /* Register the IPI event ID and create the backing PAL message queue */
        recv_ev_id = vIPIEventRegister( next_ipi_ev_id,
                                        &pxRxQueue[ next_ipi_ev_id ],
                                        NULL, NULL,
                                        (uint64_t) sizeOfElem, (char *) mqName );

        if( recv_ev_id != next_ipi_ev_id )
        {
            log_err( "[FWKQ] No event ID left for allocation\r\n" );
            return Failure;
        }

        queue->queue = pxRxQueue[ next_ipi_ev_id ];
        log_info( "[FWKQ] Created proc cmd queue 0x%x with IPI ev id %d\r\n",
                  queue->queue, next_ipi_ev_id );
    }

    last_assigned_ipi_id[ core_id ] = next_ipi_ev_id;
    queue->ipi_ev_id = next_ipi_ev_id;

    return Success;
}

/***********************************************************************

***********************************************************************/
/**
 * @brief Sends a command to a procedure queue.
 *
 * Enqueues @p cmdSize bytes starting at @p cmd into the PAL message queue
 * associated with @p queue using priority level 1 and no blocking timeout.
 * Must not be called from an ISR context.
 *
 * @param[in] queue    Procedure queue initialised by @c init_proc_cmd_queue().
 * @param[in] cmd      Pointer to the command data to copy into the queue.
 * @param[in] cmdSize  Size of the command in bytes.
 *
 * @return @c Success if the message was enqueued, or the PAL error code on
 *         failure (e.g. @c MsgTimeout if the queue is full).
 */
Error_t send_cmd_to_proc( proc_queue_t *queue, const void *cmd, size_t cmdSize )
{
    Error_t ret = pal_msgq_send( queue->queue, (void *) cmd, cmdSize,
                                 E_PRI_LEVEL_1, 0 );
    if( ret != Success )
        log_err( "[FWKQ] Could not send data to the process queue\r\n" );

    return ret;
}
