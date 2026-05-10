/*
 * Mundo Sense
 */

#ifndef __MS_CAM_L1C_RECEIVER_H
#define __MS_CAM_L1C_RECEIVER_H

#include "ms_camera_l1_controller_fwk.h"
#include "ms_camera_procx_comm.h"

#ifndef CTRL_ACK_TIMEOUT_FRAMES
#define CTRL_ACK_TIMEOUT_FRAMES  10U
#endif

extern proc_queue_t receiver_q;

void  receiver_cb_xc( procx_comm_id_e src_proc, void *data );
void  l1_camera_receiver_init( uint8_t core_num );
void *receiver_task( void *arg );

#endif /* __MS_CAM_L1C_RECEIVER_H */
