/*
 * Mundo Sense
 */

/**
 * @file ms_l1c_transmitter.h
 * @brief Public interface for the L1 controller transmitter subsystem.
 *
 * Declares the cross-task callback, the PAL message queue, and the task
 * entry point for the transmitter subsystem.
 */

#ifndef __MS_L1C_TRANSMITTER_H
#define __MS_L1C_TRANSMITTER_H

/*------------------------------------------
                INCLUDES
--------------------------------------------*/
#include "ms_l1_controller_fwk.h"
#include "ms_procx_comm.h"

/*------------------------------------------
                VARIABLES
--------------------------------------------*/

/**
 * @brief PAL proc queue that buffers transmitter messages for the L1 controller.
 *
 * Defined in ms_l1c_transmitter.c.
 */
extern proc_queue_t transmitter_q;

/*------------------------------------------
                PROTOTYPES
--------------------------------------------*/

/**
 * @brief Cross-task callback for the transmitter processor.
 *
 * Registered with the procx_comm framework for @c TX_XC_ID.
 *
 * @param[in] src_proc  Source processor ID.
 * @param[in] data      Pointer to the message payload (@c S_UNIFIED_MSG_BUFF).
 */
void transmitter_cb_xc( procx_comm_id_e src_proc, void *data );

/**
 * @brief Task entry point for transmitter processing.
 *
 * @param[in] arg  Optional thread argument (unused).
 *
 * @return Always NULL.
 */
void *transmitter_task( void *arg );

#endif /* __MS_L1C_TRANSMITTER_H */
