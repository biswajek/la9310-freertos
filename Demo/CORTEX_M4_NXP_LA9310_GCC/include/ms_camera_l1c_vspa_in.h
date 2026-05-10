/*
 * Mundo Sense
 */

#ifndef __MS_CAM_L1C_VSPA_IN_H
#define __MS_CAM_L1C_VSPA_IN_H

#include "ms_camera_l1_controller_fwk.h"
#include "ms_camera_procx_comm.h"

extern proc_queue_t vspa_in_q;

void  vspa_in_cb_xc( procx_comm_id_e src_proc, void *data );
void *vspa_in_task( void *arg );

#endif /* __MS_CAM_L1C_VSPA_IN_H */
