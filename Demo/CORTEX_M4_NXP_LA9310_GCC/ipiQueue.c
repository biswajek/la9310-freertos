/*
 * Mundo Sense
 */

/**
 * @file ipiQueue.c
 * @brief Inter-Processor Interrupt (IPI) queue management for the LA9310 L1 controller.
 *
 * Implements a software-level IPI event dispatch system built on top of PAL
 * message queues and semaphores.  Each event ID can be associated with a PAL
 * message queue, a callback, or both.  An internal circular global queue
 * buffers cross-core elements; on the current single-core LA9310 M4 target
 * the global queue path is exercised only when src and dst core differ.
 */

/***********************************************************************
                                INCLUDES
***********************************************************************/
#include <stdbool.h>
#include <string.h>
#include "ms_logger.h"
#include "ipiQueue.h"


#ifdef TESTFRAMEWORK_ENABLE
#include "test_framework_config.h"
#endif

/***********************************************************************
                                VARIABLES
***********************************************************************/

/** @brief Per-event PAL message queue handles, indexed by IPIEventID. */
MsgQ_Handle_t pxRxQueue[IPI_EVT_ID_MAX];

/** @brief Name string used when creating the event-registration semaphore. */
char* defEventSemName = "EventSemaphore";

/** @brief Name string used when creating the send semaphore. */
char* defSendSemName = "SendSemaphore";

/**
 * @brief A single element stored in the global IPI queue.
 *
 * Carries all routing information and the user payload pointer needed
 * for the receiving core to dispatch the event.
 */
struct IPIQElem
{
    uint8_t          srcCore;  /**< Core that produced this element. */
    uint8_t          dstCore;  /**< Core that should consume this element. */
    enum IPIEventID  eventID;  /**< Event identifier for dispatch look-up. */
    void            *useData;  /**< Opaque pointer to caller-owned payload. */
};

/**
 * @brief Per-destination-core circular queue used for cross-core event delivery.
 *
 * Each destination core owns one instance.  The inner dimension is indexed by
 * source core so that head/tail/counters are kept per (src, dst) pair,
 * avoiding contention between producers.
 */
struct IPIGlobalQueue
{
    /** @brief Ring-buffer storage, [srcCore][slot]. */
    struct IPIQElem elements[ CONFIG_NUM_CORES_IPI ][ IPI_NOTIFY_QUEUE_SIZE ];
    /** @brief Read head per source core. */
    uint8_t head[ CONFIG_NUM_CORES_IPI ];
    /** @brief Write tail per source core. */
    uint8_t tail[ CONFIG_NUM_CORES_IPI ];
    /** @brief Total enqueue count per source core (wraps at 255). */
    uint8_t eqCnt[ CONFIG_NUM_CORES_IPI ];
    /** @brief Total dequeue count per source core (wraps at 255). */
    uint8_t dqCnt[ CONFIG_NUM_CORES_IPI ];
};

/**
 * @brief Private state for the IPI subsystem on one core.
 *
 * A single static instance (@c gIPIQueuePriv) holds all runtime state.
 * Access to registration data is protected by @c IPIEveRegSem; access to the
 * send path is protected by @c IPISendSem.
 */
struct IPIQueue
{
    /** @brief ID of the core that owns this instance. */
    uint8_t currentCore;
    /** @brief Pointer into the shared global-queue memory. */
    struct IPIGlobalQueue * IPIGlobalQ;
    /** @brief Registered callback per event ID, or NULL. */
    pxEventCb IPIEventCBList[ IPI_EVT_ID_MAX ];
    /** @brief Opaque cookie passed to each callback, indexed by event ID. */
    void * IPIEventCookieList[ IPI_EVT_ID_MAX ];
    /** @brief Bitmask of registered event IDs (bit N set == IPI_EVT_IDN registered). */
    uint8_t IPIEventMux;
    /** @brief Binary semaphore protecting IPIEventCBList / IPIEventCookieList. */
    Sem_Handle_t IPIEveRegSem;
    /** @brief Binary semaphore serialising the send path. */
    Sem_Handle_t IPISendSem;
};

/** @brief Singleton IPI private state for this core. */
static struct IPIQueue gIPIQueuePriv;

/** @brief Statically allocated backing store for the global queue, one entry per core. */
struct IPIGlobalQueue IPIGlobalQueueMem[ CONFIG_NUM_CORES_IPI];

/** @brief Global event-ID array exposed for external inspection/debug. */
enum IPIEventID IPIGlobalEventID[ IPI_EVT_ID_MAX ];

/** @brief Per-destination-core count of events successfully sent. */
uint32_t gIPISentStats[ CONFIG_NUM_CORES_IPI ];

/** @brief Per-source-core count of events successfully received. */
uint32_t gIPIRecvStats[ CONFIG_NUM_CORES_IPI ];

/* Forward declaration — defined below in the file. */
bool vIPIGlobalQEmpty( uint8_t head,
                       uint8_t tail );


/***********************************************************************
                            FUNCTIONS
***********************************************************************/

/**
 * @brief Increments the sent-events statistic for a destination core.
 *
 * @param[in] dstCore  Index of the destination core whose counter to bump.
 */
static inline void vIPIUpdateSentStats( uint32_t dstCore )
{
    gIPISentStats[ dstCore ] += 1;
}


/**
 * @brief Increments the received-events statistic for a source core.
 *
 * @param[in] srcCore  Index of the source core whose counter to bump.
 */
static inline void vIPIUpdateRecvStats( uint32_t srcCore )
{
    gIPIRecvStats[ srcCore ] += 1;
}


/**
 * @brief Collects IPI send/receive and queue counters into a caller buffer.
 *
 * Iterates over all cores and copies the current values of the global sent/
 * received stats together with the per-core enqueue and dequeue counts from
 * the global queue.  The @c current_core field in the output is always set
 * to 0 (the only active core on the LA9310 single-M4 platform).
 *
 * @param[out] statsData  Pointer to an @c IPIStatsData_t buffer that will be
 *                        populated with the snapshot.  Must not be NULL.
 */
void vIPIGetStats( void * statsData )
{
    uint8_t i;
    IPIStatsData_t * localStatsData = ( IPIStatsData_t * ) statsData;

    localStatsData->current_core = 0;
    struct IPIGlobalQueue * IPIQ;

    for( i = 0; i < CONFIG_NUM_CORES_IPI; i++ )
    {
        localStatsData->IPISentStats[ i ] = gIPISentStats[ i ];
        localStatsData->IPIRecvStats[ i ] = gIPIRecvStats[ i ];

        IPIQ = &gIPIQueuePriv.IPIGlobalQ[ i ];
        localStatsData->IPIGlobalEnq[ i ] = IPIQ->eqCnt[ localStatsData->current_core ];

        IPIQ = &gIPIQueuePriv.IPIGlobalQ[ localStatsData->current_core ];
        localStatsData->IPIGlobalDeq[ i ] = IPIQ->dqCnt[ i ];
    }
}


/**
 * @brief Tests whether the (src, dst) slot of a global queue is full.
 *
 * The queue is considered full when advancing the tail by one would make it
 * equal to the head, accounting for the wrap-around at @c total.
 *
 * @param[in] head   Current read-head index.
 * @param[in] tail   Current write-tail index.
 * @param[in] total  Capacity of the ring buffer (IPI_NOTIFY_QUEUE_SIZE).
 *
 * @retval true   The queue is full; no more elements can be enqueued.
 * @retval false  The queue has at least one free slot.
 */
bool vIPIGlobalQFull( uint8_t head,
                      uint8_t tail,
                      uint8_t total )
{
    if( ( tail == ( total - 1 ) ) && ( head == 0 ) )
    {
        return true;
    }

    if( ( tail + 1 ) == head )
    {
        return true;
    }

    return false;
}

/**
 * @brief Tests whether the (src, dst) slot of a global queue is empty.
 *
 * The queue is empty when head and tail point to the same slot.
 *
 * @param[in] head  Current read-head index.
 * @param[in] tail  Current write-tail index.
 *
 * @retval true   The queue is empty; no elements are waiting.
 * @retval false  At least one element is available for dequeue.
 */
bool vIPIGlobalQEmpty( uint8_t head,
                       uint8_t tail )
{
    if( head == tail )
    {
        return true;
    }

    return false;
}

/**
 * @brief Returns the number of free slots in the ring buffer.
 *
 * @param[in] head   Current read-head index.
 * @param[in] tail   Current write-tail index.
 * @param[in] total  Capacity of the ring buffer.
 *
 * @return Number of elements that can still be enqueued without overwriting
 *         unconsumed data.
 */
static inline int vIPIGlobalQSpace( uint8_t head,
                                    uint8_t tail,
                                    uint8_t total )
{
    return tail >= head ? ( total - ( tail - head ) ) : ( head - tail - 1 );
}

/**
 * @brief Enqueues one element into the global IPI ring buffer.
 *
 * Copies all fields of @p elem into the slot at the current tail for the
 * (srcCore → dstCore) lane and advances the tail.  The destination core's
 * global queue is selected via @c elem->dstCore; the lane within that queue
 * is indexed by @c elem->srcCore.
 *
 * @param[in] elem  Pointer to the element to copy into the queue.
 *                  All fields (srcCore, dstCore, eventID, useData) must be
 *                  set by the caller.
 *
 * @retval true   Element was enqueued successfully.
 * @retval false  The queue is full; the element was not stored.
 */
bool vIPIGlobalQEnqueue( struct IPIQElem * elem )
{
    uint32_t dstCore = elem->dstCore;
    struct IPIGlobalQueue * IPIQ = &gIPIQueuePriv.IPIGlobalQ[ dstCore ];
    uint8_t * head = &IPIQ->head[ elem->srcCore ];
    uint8_t * tail = &IPIQ->tail[ elem->srcCore ];
    uint8_t * eqCnt = &IPIQ->eqCnt[ elem->srcCore ];

    if( !vIPIGlobalQFull( *head, *tail, IPI_NOTIFY_QUEUE_SIZE ) )
    {
        IPIQ->elements[ elem->srcCore ][ *tail ].srcCore = elem->srcCore;
        IPIQ->elements[ elem->srcCore ][ *tail ].dstCore = elem->dstCore;
        IPIQ->elements[ elem->srcCore ][ *tail ].eventID = elem->eventID;
        IPIQ->elements[ elem->srcCore ][ *tail ].useData = elem->useData;

        if( *tail < ( IPI_NOTIFY_QUEUE_SIZE - 1 ) )
        {
            ( *tail )++;
        }
        else
        {
            ( *tail ) = 0;
        }

        ( *eqCnt )++;
        return true;
    }

    return false;
}

/**
 * @brief Dequeues one element from the global IPI ring buffer for the current core.
 *
 * Reads the element at the head of the lane indexed by @p srcCore inside the
 * current core's global queue, advances the head, and writes the payload
 * pointer and event ID back to the caller.
 *
 * @param[in]  srcCore  Source core whose lane to read from.
 * @param[out] useData  Set to the payload pointer stored in the dequeued element.
 * @param[out] eventID  Set to the event ID stored in the dequeued element.
 *
 * @retval true   An element was dequeued; @p useData and @p eventID are valid.
 * @retval false  The lane is empty; @p useData and @p eventID are unchanged.
 */
bool vIPIGlobalQDequeue( uint8_t srcCore,
                         void ** useData,
                         enum IPIEventID * eventID )
{
    uint8_t dstCore = gIPIQueuePriv.currentCore;
    struct IPIGlobalQueue * IPIQ = &gIPIQueuePriv.IPIGlobalQ[ dstCore ];
    uint8_t * head = &IPIQ->head[ srcCore ];
    uint8_t * tail = &IPIQ->tail[ srcCore ];
    uint8_t * dqCnt = &IPIQ->dqCnt[ srcCore ];

    if( !vIPIGlobalQEmpty( *head, *tail ) )
    {
        *useData = IPIQ->elements[ srcCore ][ *head ].useData;
        *eventID = IPIQ->elements[ srcCore ][ *head ].eventID;

        if( *head < ( IPI_NOTIFY_QUEUE_SIZE - 1 ) )
        {
            ( *head )++;
        }
        else
        {
            ( *head ) = 0;
        }

        ( *dqCnt )++;
        return true;
    }

    return false;
}

/**
 * @brief Registers a handler (queue, callback, or both) for an IPI event ID.
 *
 * Associates @p eventID with a PAL message queue, a callback, or both.
 * Exactly one of @p Queue or @p cb must be non-NULL; passing both NULL is
 * an error.
 *
 * Behaviour per (Queue, cb) combination:
 * - Queue == NULL, cb != NULL : callback-only mode; no queue is created.
 * - Queue != NULL, cb != NULL : queue created with @c sizeof(void*) element
 *   size; callback also registered.
 * - Queue != NULL, cb == NULL : queue created with @p sizeOfElem element
 *   size; no callback registered.
 *
 * The function acquires @c IPIEveRegSem before modifying shared state and
 * releases it before returning.
 *
 * @param[in]  eventID     Event to register.  Must be in [IPI_EVT_ID0, IPI_EVT_ID_MAX).
 * @param[out] Queue       Pointer to a @c MsgQ_Handle_t that will be filled with the
 *                         newly created PAL queue handle, or NULL for callback-only mode.
 * @param[in]  cb          Callback to invoke when the event fires, or NULL for
 *                         queue-only mode.
 * @param[in]  cookie      Opaque value forwarded to @p cb on each invocation.
 * @param[in]  sizeOfElem  Maximum message size (bytes) for the PAL queue when
 *                         @p Queue != NULL and @p cb == NULL.
 * @param[in]  mqName      Null-terminated name for the PAL queue (must remain
 *                         valid for the lifetime of the queue).
 *
 * @return The registered @p eventID on success, or @c IPI_EVT_ID_NULL on
 *         any error (invalid ID, already registered, NULL Queue+cb, queue
 *         creation failure).
 */
enum IPIEventID vIPIEventRegister( enum IPIEventID eventID,
                                   MsgQ_Handle_t * Queue,
                                   pxEventCb cb,
                                   void * cookie,
                                   uint64_t sizeOfElem, char* mqName )
{

    Error_t ret = Failure;

    ret = pal_sem_lock(gIPIQueuePriv.IPIEveRegSem, PAL_WAIT_FOREVER);
    if (ret != Success)
        log_dbg( "[EVREG] 1.Sem Not available \r\n");

    if( ( eventID < IPI_EVT_ID0 ) || ( eventID >= IPI_EVT_ID_MAX ) )
    {
        log_dbg( "Invalid EventID error with event ID %d\r\n", eventID );
        ret = pal_sem_release( gIPIQueuePriv.IPIEveRegSem );
        if (ret != Success)
            log_dbg( "[EVREG] 2.Sem Not released \r\n");
        return IPI_EVT_ID_NULL;
    }

    if( gIPIQueuePriv.IPIEventMux & ( uint32_t ) ( 1 << ( ( int ) eventID ) ) )
    {
        log_dbg( "Event ID %d is already registered\r\n", eventID );
        ret = pal_sem_release( gIPIQueuePriv.IPIEveRegSem );
        if (ret != Success)
            log_dbg( "[EVREG] 3.Sem Not released \r\n");
        return IPI_EVT_ID_NULL;
    }

    gIPIQueuePriv.IPIEventMux |= ( uint32_t ) ( 1 << ( ( int ) eventID ) );

    if( ( Queue == NULL ) && ( cb == NULL ) )
    {
        log_err( "[EVREG] 4.Both Queue and Callback cannot be NULL\r\n" );
        return IPI_EVT_ID_NULL;
    }

    if( ( Queue == NULL ) && ( cb != NULL ) )
    {
        gIPIQueuePriv.IPIEventCBList[ eventID ] = cb;
        gIPIQueuePriv.IPIEventCookieList[ eventID ] = cookie;
    }
    else if( ( Queue != NULL ) && ( cb != NULL ) )
    {
        ret = pal_msgq_create( Queue, mqName, IPI_NOTIFY_QUEUE_SIZE, sizeof( void * ) );
        if( ret != Success)
            log_dbg( "[EVREG] 5.Queue creation failed \r\n");
        gIPIQueuePriv.IPIEventCBList[ eventID ] = cb;
        gIPIQueuePriv.IPIEventCookieList[ eventID ] = cookie;
    }
    else if( ( Queue != NULL ) && ( cb == NULL ) )
    {
        ret = pal_msgq_create( Queue, mqName, IPI_NOTIFY_QUEUE_SIZE, ( size_t ) sizeOfElem );
        if( ret != Success)
            log_dbg( "[EVREG] 6.Queue creation failed \r\n");
        gIPIQueuePriv.IPIEventCBList[ eventID ] = NULL;
        gIPIQueuePriv.IPIEventCookieList[ eventID ] = NULL;
    }
    ret = pal_sem_release( gIPIQueuePriv.IPIEveRegSem );
    if (ret != Success)
        log_dbg( "[EVREG] 7.Sem Not released \r\n");

    return eventID;
}

/**
 * @brief Unregisters a previously registered IPI event ID and frees its resources.
 *
 * Clears the event's bit in @c IPIEventMux, destroys the associated PAL
 * message queue (if any), and nulls the callback and cookie entries.  The
 * function is a no-op if @p eventID is out of range or was never registered;
 * an error is logged in both cases.
 *
 * The function acquires @c IPIEveRegSem before modifying shared state and
 * releases it before returning.
 *
 * @param[in] eventID  Event to unregister.  Must be in [IPI_EVT_ID0, IPI_EVT_ID_MAX).
 */
void vIPIEventUnRegister( enum IPIEventID eventID )
{
    Error_t ret = Success;

    ret = pal_sem_lock( gIPIQueuePriv.IPIEveRegSem, PAL_WAIT_FOREVER );
    if( ret != Success)
        log_dbg( "[EVUNREG] 1.Sem Not locked \r\n");


    if( ( eventID < IPI_EVT_ID0 ) || ( eventID >= IPI_EVT_ID_MAX ) )
    {
        log_err( "[EVUNREG] 2.Invalid Event error with event ID %d\r\n", eventID );
        ret = pal_sem_release( gIPIQueuePriv.IPIEveRegSem );
        if( ret != Success)
            log_dbg( "[EVUNREG] 3.Sem Not released \r\n");
        return;
    }

    if( !( gIPIQueuePriv.IPIEventMux & ( uint32_t ) ( 1 << ( ( int ) eventID ) ) ) )
    {
        log_err( "[EVUNREG] 4.Event ID %d not registered\r\n", eventID );
        ret = pal_sem_release( gIPIQueuePriv.IPIEveRegSem );
        if( ret != Success)
            log_dbg( "[EVUNREG] 5.Sem Not released \r\n");
        return;
    }

    gIPIQueuePriv.IPIEventMux &= ~( ( uint32_t ) ( 1 << ( ( int ) eventID ) ) );

    if( pxRxQueue[ eventID ] != 0 )
    {
        ret = pal_msgq_remove( pxRxQueue[ eventID ] );
        if( ret != Success)
            log_dbg( "[EVUNREG] 6.Queue Not deleted \r\n");
        pxRxQueue[ eventID ] = 0;
    }

    gIPIQueuePriv.IPIEventCBList[ eventID ] = NULL;
    gIPIQueuePriv.IPIEventCookieList[ eventID ] = NULL;

    ret = pal_sem_release( gIPIQueuePriv.IPIEveRegSem );
    if( ret != Success)
        log_dbg( "[EVUNREG] 7.Sem Not released \r\n");

}

/**
 * @brief Initialises the IPI subsystem for the given core.
 *
 * Zeroes the private state, sets the current-core ID, links the shared global
 * queue memory, and creates the two binary semaphores that protect the
 * registration and send paths respectively.  Both semaphores are released
 * (given) immediately after creation so that they start in the unlocked state.
 *
 * Must be called once during system startup before any call to
 * @c vIPIEventRegister() or @c vIPISendData().
 *
 * @param[in] core_id  Index of the core being initialised.
 *
 * @retval true   All resources were created and released successfully.
 * @retval false  One or more PAL calls failed; check debug log for details.
 */
bool vIPICoreInit( uint8_t core_id )
{
    bool ret = true;
    Error_t palret = Success;

    memset( &gIPIQueuePriv, 0, sizeof( struct IPIQueue ) );

    gIPIQueuePriv.currentCore  = core_id;
    gIPIQueuePriv.IPIGlobalQ = IPIGlobalQueueMem;

    palret = pal_sem_create(&gIPIQueuePriv.IPIEveRegSem, defEventSemName, E_SEM_BINARY, E_SEM_EMPTY);
    if( palret != Success)
    {
        log_dbg( "[IPIINIT] 1.Sem Not created \r\n");
        ret = false;
    }

    palret = pal_sem_release( gIPIQueuePriv.IPIEveRegSem );
    if( palret != Success)
    {
        log_dbg( "[IPIINIT] 2.Sem Not released \r\n");
        ret = false;
    }

    palret = pal_sem_create(&gIPIQueuePriv.IPISendSem, defSendSemName, E_SEM_BINARY, E_SEM_EMPTY);
    if ( palret != Success)
    {
        log_dbg( "[IPIINIT] 3.Sem Not created \r\n");
        ret = false;
    }

    palret = pal_sem_release( gIPIQueuePriv.IPISendSem );
    if ( palret != Success)
    {
        log_dbg( "[IPIINIT] 4.Sem Not released \r\n");
        ret = false;
    }
    return ret;
}

/**
 * @brief Sends an IPI event with a payload pointer to a destination core.
 *
 * For same-core delivery the registered handler for @p eventID is invoked
 * immediately under the send semaphore:
 * - Queue + Callback : payload is enqueued in the PAL message queue and the
 *   callback is fired with NULL userData (caller reads from the queue).
 * - Callback only    : callback is fired directly with @p useData.
 * - Queue only       : payload pointer is enqueued in the PAL message queue.
 *
 * For cross-core delivery the element is inserted into the global ring buffer
 * and the destination core is expected to drain it via its ISR (not yet
 * implemented on the single-core LA9310 target).
 *
 * Sent and received statistics are updated on successful delivery.
 *
 * @param[in] dstCore  Destination core index.
 * @param[in] eventID  Event to fire.  Must be in [IPI_EVT_ID0, IPI_EVT_ID_MAX)
 *                     and previously registered with @c vIPIEventRegister().
 * @param[in] useData  Opaque payload pointer forwarded to the handler.
 *                     Ownership is not transferred; the caller must ensure
 *                     the data remains valid until the handler has consumed it.
 *
 * @retval true   Event was delivered (same-core) or enqueued (cross-core).
 * @retval false  Delivery failed: invalid event ID, unregistered event,
 *                PAL queue send error, or global ring buffer full.
 */
bool vIPISendData( uint8_t dstCore,
                   enum IPIEventID eventID,
                   void * useData )
{
    Error_t palret = Success;
    bool ret = false;

    struct IPIQElem elem;

    if( ( eventID < IPI_EVT_ID0 ) || ( eventID >= IPI_EVT_ID_MAX ) )
    {
        log_dbg( "[IPISENDDATA] 1. Invalid Event error with event ID %d\r\n", eventID );
        palret = pal_sem_release( gIPIQueuePriv.IPISendSem );
        if( palret != Success)
            log_dbg( "[IPISENDDATA] 2. Sem Not released \r\n" );
        return false;
    }

    elem.srcCore = gIPIQueuePriv.currentCore;
    elem.dstCore = dstCore;
    elem.eventID = eventID;
    elem.useData = useData;

    palret = pal_sem_lock( gIPIQueuePriv.IPISendSem, PAL_WAIT_FOREVER );
    if( palret != Success)
        log_dbg( "[IPISENDDATA] 3. Sem Not locked \r\n" );


    if( dstCore == gIPIQueuePriv.currentCore )
    {

        if( !( gIPIQueuePriv.IPIEventMux & ( uint32_t ) ( 1 << eventID ) ) )
        {
            log_err( "[IPISENDDATA] 4.Event ID %d not registered\r\n", eventID );
            palret = pal_sem_release( gIPIQueuePriv.IPISendSem );
            if( palret != Success)
                log_dbg( "[IPISENDDATA] 5. Sem Not released \r\n" );
            return false;
        }

        if( ( pxRxQueue[ eventID ] != 0 ) && ( gIPIQueuePriv.IPIEventCBList[ eventID ] != NULL ) )
        {
            ret = true;
            palret = pal_msgq_send( pxRxQueue[ eventID ], &useData, sizeof( void * ), E_PRI_LEVEL_1, 0 );
            if( palret != Success)
            {
                log_dbg( "[IPISENDDATA] 6.Queue Send Failed \r\n" );
                ret = false;
            }
            gIPIQueuePriv.IPIEventCBList[ eventID ]( eventID, NULL, gIPIQueuePriv.IPIEventCookieList[ eventID ] );
        }
        else if( ( pxRxQueue[ eventID ] == 0 ) && ( gIPIQueuePriv.IPIEventCBList[ eventID ] != NULL ) )
        {
            ret = true;
            gIPIQueuePriv.IPIEventCBList[ eventID ]( eventID, useData, gIPIQueuePriv.IPIEventCookieList[ eventID ] );
        }
        else if( ( pxRxQueue[ eventID ] != 0 ) && ( gIPIQueuePriv.IPIEventCBList[ eventID ] == NULL ) )
        {
            ret = true;
            palret = pal_msgq_send( pxRxQueue[ eventID ], &useData, sizeof( void * ), E_PRI_LEVEL_1, 0 );
            if( palret != Success)
            {
                log_dbg( "[IPISENDDATA] 7.Queue Send Failed \r\n" );
                ret = false;
            }
        }

        palret = pal_sem_release( gIPIQueuePriv.IPISendSem );
        if( palret != Success)
        {
            log_dbg( "[IPISENDDATA] 8.Sem Not released \r\n" );
        }
        if( ret == true )
        {
            vIPIUpdateSentStats( dstCore );
            vIPIUpdateRecvStats( gIPIQueuePriv.currentCore );
        }

        return ret;
    }

    if( vIPIGlobalQEnqueue( &elem ) == false )
    {
        log_err( "[IPISENDDATA] 9.Failed to enqueue data to global queue with event ID: %d\r\n", eventID );

        palret = pal_sem_release( gIPIQueuePriv.IPISendSem );
        if( palret != Success)
        {
            log_dbg( "[IPISENDDATA] 10.Sem Not released \r\n" );
        }
        return false;
    }

    /* Cross-core path: on a multi-core platform an interrupt would be raised
     * here to wake the destination core's ISR, which would call
     * vIPIGlobalQDequeue() and dispatch to the registered handler.
     * Not required on the current single-M4 LA9310 target.
     */
    vIPIUpdateSentStats( dstCore );

    return true;
}

#ifdef IPI_GLOBAL_Q_DBG
/**
 * @brief Prints the enqueue and dequeue counters of the global IPI queue.
 *
 * Outputs a human-readable snapshot of @c eqCnt and @c dqCnt for up to four
 * source cores for the specified destination and source core indices.  Useful
 * for live debugging when @c IPI_GLOBAL_Q_DBG is defined at build time.
 *
 * @param[in] dstCore  Index of the destination core whose queue to display,
 *                     or a negative value to skip the destination-side log.
 * @param[in] srcCore  Index of the source core whose lanes to display across
 *                     all destination queues, or a negative value to skip.
 */
void vIPIGlobalQStatusCheck( int dstCore,
                                int srcCore )
{
    struct IPIGlobalQueue * IPIQ;

    if( ( dstCore >= 0 ) && ( dstCore < CONFIG_NUM_CORES_IPI ) )
    {
        IPIQ = &gIPIQueuePriv.IPIGlobalQ[ dstCore ];

        log_info( "GlobalQ[%d]: %d %d, %d %d, %d %d, %d %d\r\n",
                    dstCore, IPIQ->eqCnt[ 0 ], IPIQ->dqCnt[ 0 ], IPIQ->eqCnt[ 1 ], IPIQ->dqCnt[ 1 ],
                    IPIQ->eqCnt[ 2 ], IPIQ->dqCnt[ 2 ], IPIQ->eqCnt[ 3 ], IPIQ->dqCnt[ 3 ] );
    }

    if( ( srcCore >= 0 ) && ( srcCore < CONFIG_NUM_CORES_IPI ) )
    {
        IPIQ = gIPIQueuePriv.IPIGlobalQ;

        log_info( "GlobalQ from %d: %d %d, %d %d, %d %d, %d %d\r\n",
                    srcCore, IPIQ[ 0 ].eqCnt[ srcCore ], IPIQ[ 0 ].dqCnt[ srcCore ],
                    IPIQ[ 1 ].eqCnt[ srcCore ], IPIQ[ 1 ].dqCnt[ srcCore ],
                    IPIQ[ 2 ].eqCnt[ srcCore ], IPIQ[ 2 ].dqCnt[ srcCore ],
                    IPIQ[ 3 ].eqCnt[ srcCore ], IPIQ[ 3 ].dqCnt[ srcCore ] );
    }
}
#endif /* ifdef IPI_GLOBAL_Q_DBG */
