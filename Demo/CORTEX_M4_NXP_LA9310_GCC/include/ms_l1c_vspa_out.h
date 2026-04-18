/*
 * Mundo Sense
 */

/**
 * @file ms_l1c_vspa_out.h
 * @brief Public interface for the L1 controller VSPA-output subsystem.
 *
 * Declares the cross-task callback and the PAL message queue used by the
 * VSPA-output subsystem.  Other subsystems that need to send data through
 * the VSPA-out path may reference @c vspa_out_q directly via
 * @c send_cmd_to_proc(), or trigger the path through the @c procx_comm()
 * mechanism using @c VSPA_OUT_XC_ID as the destination.
 */

#ifndef __MS_L1C_VSPA_OUT_H
#define __MS_L1C_VSPA_OUT_H

/*------------------------------------------
                INCLUDES
--------------------------------------------*/
#include "ms_l1_controller_fwk.h"
#include "ms_procx_comm.h"

/*------------------------------------------
                VARIABLES
--------------------------------------------*/

/**
 * @brief PAL proc queue that buffers VSPA-output messages for the L1 controller.
 *
 * Defined in ms_l1c_vspa_out.c.  Subsystems that push data directly into
 * the VSPA-out queue should use @c send_cmd_to_proc() with this handle.
 */
extern proc_queue_t vspa_out_q;

/*------------------------------------------
                PROTOTYPES
--------------------------------------------*/

/**
 * @brief Cross-task callback for the VSPA-output processor.
 *
 * Registered with the procx_comm framework for @c VSPA_OUT_XC_ID.
 * Called automatically when another processor sends a message to this
 * subsystem via @c procx_comm().  Not intended to be called directly.
 *
 * @param[in] src_proc  Source processor ID.
 * @param[in] data      Pointer to the message payload (@c S_UNIFIED_MSG_BUFF).
 */
void vspa_out_cb_xc( procx_comm_id_e src_proc, void *data );

/**
 * @brief Task entry point for VSPA-output processing.
 *
 * @param[in] arg  Optional thread argument (unused).
 *
 * @return Always NULL.
 */
void *vspa_out_task( void *arg );

#endif /* __MS_L1C_VSPA_OUT_H */
