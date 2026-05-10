/*
 * Mundo Sense
 */

#ifndef __MS_CAM_L1C_TRANSMITTER_H
#define __MS_CAM_L1C_TRANSMITTER_H

#include "ms_camera_l1_controller_fwk.h"
#include "ms_camera_procx_comm.h"

extern proc_queue_t transmitter_q;

void  transmitter_cb_xc( procx_comm_id_e src_proc, void *data );
void *transmitter_task( void *arg );

#endif /* __MS_CAM_L1C_TRANSMITTER_H */
