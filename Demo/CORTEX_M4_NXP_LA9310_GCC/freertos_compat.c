/*
 * FreeRTOS API compatibility shims for libPAL.a
 *
 * libPAL.a was compiled against a newer FreeRTOS version where:
 *   - xQueueReceive()      is an exported function
 *   - xQueueSemaphoreTake() is an exported function
 *
 * In FreeRTOS V9 (used by this project):
 *   - xQueueReceive is a preprocessor macro wrapping xQueueGenericReceive()
 *   - xQueueSemaphoreTake does not exist (semaphore take goes through
 *     xQueueGenericReceive() directly)
 *
 * This file provides the two symbols as thin wrapper functions so that the
 * linker can resolve the references from libPAL.a.
 */

#include "FreeRTOS.h"
#include "queue.h"

/* Undefine the macro so that we can declare the function with the same name. */
#undef xQueueReceive

/**
 * @brief Receive an item from a queue (function wrapper for libPAL.a).
 *
 * Delegates to xQueueGenericReceive() with xJustPeek = pdFALSE, matching the
 * behaviour of the xQueueReceive macro defined in queue.h.
 */
BaseType_t xQueueReceive( QueueHandle_t xQueue,
                          void * const pvBuffer,
                          TickType_t xTicksToWait )
{
    return xQueueGenericReceive( xQueue, pvBuffer, xTicksToWait, pdFALSE );
}

/**
 * @brief Take a semaphore (function wrapper for libPAL.a).
 *
 * In FreeRTOS V9 semaphore take is implemented through xQueueGenericReceive()
 * with a NULL buffer pointer.  This wrapper provides the xQueueSemaphoreTake
 * symbol expected by libPAL.a.
 */
BaseType_t xQueueSemaphoreTake( QueueHandle_t xQueue,
                                TickType_t xTicksToWait )
{
    return xQueueGenericReceive( xQueue, NULL, xTicksToWait, pdFALSE );
}
