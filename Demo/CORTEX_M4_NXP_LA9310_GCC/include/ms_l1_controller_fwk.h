/*
 * Mundo Sense
 */

#ifndef __L1CA_FWK_H
#define __L1CA_FWK_H

/*------------------------------------------
                INCLUDES
--------------------------------------------*/
#include <stddef.h>
#include <stdint.h>
#include "pal_types.h"
#include "pal_msgq.h"
#include "ms_l1c_controller.h"
#include "ipiQueue.h"

/*------------------------------------------
                DEFINES
--------------------------------------------*/

/*------------------------------------------
                VARIABLES
--------------------------------------------*/
/* Task command queue */
typedef struct
{
    /* IPI event ID used for inter-task/core routing */
    IPIEventID_t    ipi_ev_id;
    /* Core id for inter-core routing */
    uint8_t        core_id;
    /* PAL message queue handle */
    MsgQ_Handle_t   queue;
} proc_queue_t;

/*------------------------------------------
                PROTOTYPES
--------------------------------------------*/
Error_t send_cmd_to_proc(proc_queue_t *queue, const void *cmd, size_t cmdSize);
Error_t init_proc_cmd_queue(proc_queue_t *queue, core_id_t core_id, size_t sizeOfElem, const char *mqName);

#endif /* __L1CA_FWK_H */
