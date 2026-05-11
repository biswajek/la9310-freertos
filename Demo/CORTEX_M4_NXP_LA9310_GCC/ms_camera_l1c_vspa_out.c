/*
 * Mundo Sense
 */

#include <stddef.h>
#include "pal_thread.h"
#include "pal_types.h"
#include "ms_camera_global_typedef.h"
#include "ms_camera_l1c_controller.h"
#include "ms_camera_l1_controller_fwk.h"
#include "ms_camera_procx_comm.h"
#include "ms_camera_l1c_vspa_out.h"
#include "ms_camera_logger.h"

#define MS_VSPAOUT_QUEUE_NAME    "camVspaOutQ"

proc_queue_t vspa_out_q;

void vspa_out_cb_xc( procx_comm_id_e src_proc, void *data )
{
    UNUSED( src_proc );
    UNUSED( data );
}

void l1_camera_vspa_out_init( uint8_t core_num )
{
    init_proc_cmd_queue( &vspa_out_q, (core_id_t) core_num,
                         sizeof( S_UNIFIED_MSG_BUFF ), MS_VSPAOUT_QUEUE_NAME );

    procx_comm_reg( vspa_out_cb_xc, VSPA_OUT_XC_ID );
}

void *vspa_out_task( void *arg )
{
    (void) arg;

    log_info( "[CAM VSPA OUT] task started\r\n" );

    for( ; ; )
    {
        (void) pal_m_sleep( 100 );
    }

    return NULL;
}
