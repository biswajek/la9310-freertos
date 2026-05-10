/*
 * Mundo Sense
 */

#ifndef __MS_CAM_L1C_CAMERA_MGR_H
#define __MS_CAM_L1C_CAMERA_MGR_H

#include <stdint.h>
#include "ms_camera_procx_comm.h"

void  l1_camera_mgr_init( uint8_t core_num );
void  camera_mgr_cb_xc( procx_comm_id_e src_proc, void *data );
void  camera_mgr_ipc_bch_send_cb( void *data );
void  camera_mgr_ipc_start_video_tx_cb( void *data );
void  camera_mgr_get_slot_frame_count( uint32_t *slot_count, uint32_t *frame_count );
void *camera_mgr_task( void *arg );

#endif /* __MS_CAM_L1C_CAMERA_MGR_H */
