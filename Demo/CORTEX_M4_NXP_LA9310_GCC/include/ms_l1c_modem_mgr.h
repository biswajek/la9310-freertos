/*
 * Mundo Sense
 */

#ifndef __MS_L1C_MODEM_MGR_H
#define __MS_L1C_MODEM_MGR_H

#include <stdint.h>
#include "ms_procx_comm.h"

void l1_controller_modem_mgr_init( uint8_t core_num );
void modem_mgr_cb_xc( procx_comm_id_e src_proc, void *data );
void modem_mgr_ipc_control_msg_cb( void *data );
void modem_mgr_get_slot_frame_count( uint32_t *slot_count, uint32_t *frame_count );
void *modem_mgr_task( void *arg );

#endif /* __MS_L1C_MODEM_MGR_H */
