/*
 * Mundo Sense
 */

#ifndef __CAMERA_L1CA_FWK_H
#define __CAMERA_L1CA_FWK_H

#include <stddef.h>
#include <stdint.h>
#include "pal_types.h"
#include "pal_msgq.h"
#include "ms_camera_l1c_controller.h"
#include "ipiQueue.h"

typedef struct
{
    IPIEventID_t    ipi_ev_id;
    uint8_t         core_id;
    MsgQ_Handle_t   queue;
} proc_queue_t;

Error_t send_cmd_to_proc( proc_queue_t *queue, const void *cmd, size_t cmdSize );
Error_t init_proc_cmd_queue( proc_queue_t *queue, core_id_t core_id, size_t sizeOfElem, const char *mqName );

#endif /* __CAMERA_L1CA_FWK_H */
