/*
 * Mundo Sense
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#ifdef MS_TARGET_CAMERA
#include "ms_camera_logger.h"
#include "ms_camera_procx_comm.h"
#else
#include "ms_controller_logger.h"
#include "ms_controller_procx_comm.h"
#endif

procx_comm_cb_t procx_comm_cbs[NUM_XC_IDS];

bool procx_comm( procx_comm_id_e dst_proc, procx_comm_id_e src_proc, void *data )
{
    if( dst_proc >= NUM_XC_IDS )
    {
        log_err( "[PROCX_COMM] ERROR: Invalid destination %d.\r\n", dst_proc );
        return false;
    }

    if( src_proc >= NUM_XC_IDS )
    {
        log_err( "[PROCX_COMM] ERROR: Invalid source %d.\r\n", src_proc );
        return false;
    }

    if( procx_comm_cbs[ dst_proc ] == NULL )
    {
        log_err( "[PROCX_COMM] ERROR: Callback not initialized for %d.\r\n", dst_proc );
        return false;
    }

    procx_comm_cbs[ dst_proc ]( src_proc, data );
    return true;
}

bool procx_comm_reg( procx_comm_cb_t cb, procx_comm_id_e proc_id )
{
    if( proc_id >= NUM_XC_IDS )
    {
        log_err( "[PROCX_COMM] ERROR: Invalid procedure ID %d.\r\n", proc_id );
        return false;
    }

    procx_comm_cbs[ proc_id ] = cb;
    return true;
}
