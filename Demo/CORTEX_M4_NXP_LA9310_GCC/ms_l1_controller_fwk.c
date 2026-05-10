/*
 * Mundo Sense
 */

#include <stdbool.h>
#include <stddef.h>
#include "pal_types.h"
#include "pal_msgq.h"
#include "ipiQueue.h"
#ifdef MS_TARGET_CAMERA
#include "ms_camera_l1_controller_fwk.h"
#include "ms_camera_globals.h"
#include "ms_camera_logger.h"
#else
#include "ms_controller_l1_controller_fwk.h"
#include "ms_controller_globals.h"
#include "ms_controller_logger.h"
#endif

IPIEventID_t last_assigned_ipi_id[ L1_CORE_1 + 1 ] = { IPI_EVT_ID0, IPI_EVT_ID0 };

Error_t init_proc_cmd_queue( proc_queue_t *queue, core_id_t core_id,
                              size_t sizeOfElem, const char *mqName )
{
    IPIEventID_t recv_ev_id;
    IPIEventID_t next_ipi_ev_id;

    queue->core_id = (uint8_t) core_id;

    next_ipi_ev_id = (IPIEventID_t) ( last_assigned_ipi_id[ core_id ] + 1 );

    if( core_id == (core_id_t) g_GlobalDebugInfo.CoreNum )
    {
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

Error_t send_cmd_to_proc( proc_queue_t *queue, const void *cmd, size_t cmdSize )
{
    Error_t ret = pal_msgq_send( queue->queue, (void *) cmd, cmdSize,
                                 E_PRI_LEVEL_1, 0 );
    if( ret != Success )
        log_err( "[FWKQ] Could not send data to the process queue\r\n" );

    return ret;
}
