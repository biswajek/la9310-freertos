/*
 * Mundo Sense
 */

/**
 * @file ms_l1c_receiver.h
 * @brief Public interface for the L1 controller receiver subsystem.
 *
 * Declares the cross-task callback, the PAL message queue, and the task
 * entry point for the receiver subsystem.
 */

#ifndef __MS_L1C_RECEIVER_H
#define __MS_L1C_RECEIVER_H

/*------------------------------------------
                INCLUDES
--------------------------------------------*/
#include "ms_l1_controller_fwk.h"
#include "ms_procx_comm.h"

/*------------------------------------------
                DEFINES
--------------------------------------------*/

/**
 * @brief Number of frames the RX thread waits for a CTRL_ACK before giving up.
 *
 * Configurable at compile time via -DCTRL_ACK_TIMEOUT_FRAMES=<n>.
 */
#ifndef CTRL_ACK_TIMEOUT_FRAMES
#define CTRL_ACK_TIMEOUT_FRAMES  10U
#endif

/*------------------------------------------
                VARIABLES
--------------------------------------------*/

/**
 * @brief PAL proc queue that buffers receiver messages for the L1 controller.
 *
 * Defined in ms_l1c_receiver.c.
 */
extern proc_queue_t receiver_q;

/*------------------------------------------
                PROTOTYPES
--------------------------------------------*/

/**
 * @brief Cross-task callback for the receiver processor.
 *
 * Registered with the procx_comm framework for @c RX_XC_ID.
 *
 * @param[in] src_proc  Source processor ID.
 * @param[in] data      Pointer to the message payload (@c S_UNIFIED_MSG_BUFF).
 */
void receiver_cb_xc( procx_comm_id_e src_proc, void *data );

/**
 * @brief Initializes the L1 receiver subsystem.
 *
 * @param[in] core_num  Core number on which this subsystem runs.
 */
void l1_controller_receiver_init( uint8_t core_num );

/**
 * @brief Task entry point for receiver processing.
 *
 * @param[in] arg  Optional thread argument (unused).
 *
 * @return Always NULL.
 */
void *receiver_task( void *arg );

#endif /* __MS_L1C_RECEIVER_H */
