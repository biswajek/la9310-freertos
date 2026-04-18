/*
 * Mundo Sense
 */

/**
 * @file ms_procx_comm.c
 * @brief Cross-processor / cross-task communication dispatcher.
 *
 * Provides a lightweight publish-subscribe bus that routes messages between
 * named logical processors (identified by @c procx_comm_id_e).  Each logical
 * processor registers a receive callback via @c procx_comm_reg(); senders
 * deliver data by calling @c procx_comm() with a source and destination ID.
 */

/*****************************************************************
                            INCLUDES
******************************************************************/
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "ms_logger.h"
#include "ms_procx_comm.h"

/*****************************************************************
                            VARIABLES
******************************************************************/

/** @brief Per-processor receive callback table, indexed by @c procx_comm_id_e. */
procx_comm_cb_t procx_comm_cbs[NUM_XC_IDS];


/*****************************************************************
                            FUNCTIONS
******************************************************************/

/**
 * @brief Dispatches a message from one logical processor to another.
 *
 * Validates that both @p dst_proc and @p src_proc are within the legal range
 * and that a callback has been registered for the destination, then invokes
 * that callback with the source ID and payload pointer.
 *
 * @param[in] dst_proc  Destination processor ID.  Must be less than
 *                      @c NUM_XC_IDS and have a registered callback.
 * @param[in] src_proc  Source processor ID.  Must be less than @c NUM_XC_IDS.
 * @param[in] data      Opaque pointer to the message payload.  Ownership is
 *                      not transferred; the caller must keep it valid until
 *                      the callback returns.
 *
 * @retval true   The callback was found and invoked successfully.
 * @retval false  @p dst_proc or @p src_proc is out of range, or no callback
 *                is registered for @p dst_proc.
 */
bool procx_comm( procx_comm_id_e dst_proc, procx_comm_id_e src_proc, void *data )
{
    bool ret = true;

    if( dst_proc >= NUM_XC_IDS )
    {
        log_err( "[PROCX_COMM] ERROR: Invalid destination %d.\r\n", dst_proc );
        return false;
    }

    if( src_proc >= NUM_XC_IDS )
    {
        log_err( "[PROCX_COMM] ERROR: Invalid source %d.\r\n", src_proc );
        return false;
    }

    if( procx_comm_cbs[ dst_proc ] == NULL )
    {
        log_err( "[PROCX_COMM] ERROR: Callback not initialized for %d.\r\n", dst_proc );
        return false;
    }

    procx_comm_cbs[ dst_proc ]( src_proc, data );

    return ret;
}

/**
 * @brief Registers a receive callback for a logical processor.
 *
 * Associates @p cb with @p proc_id so that subsequent calls to
 * @c procx_comm() with that destination ID will invoke @p cb.
 * Registering a new callback for an already-registered ID silently
 * replaces the previous one.
 *
 * @param[in] cb       Callback function to invoke when a message arrives for
 *                     @p proc_id.  Must not be NULL.
 * @param[in] proc_id  Logical processor ID to register the callback for.
 *                     Must be less than @c NUM_XC_IDS.
 *
 * @retval true   Registration succeeded.
 * @retval false  @p proc_id is out of range; the callback was not stored.
 */
bool procx_comm_reg( procx_comm_cb_t cb, procx_comm_id_e proc_id )
{
    if( proc_id >= NUM_XC_IDS )
    {
        log_err( "[PROCX_COMM] ERROR: Invalid procedure ID %d.\r\n", proc_id );
        return false;
    }

    procx_comm_cbs[ proc_id ] = cb;

    return true;
}
