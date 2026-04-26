/*
 * Mundo Sense
 */

/*------------------------------------------
                INCLUDES
--------------------------------------------*/
#include <stdbool.h>
#include <string.h>
#include "pal_sem.h"
#include "bbdev_ipc.h"
#include "ms_global_typedef.h"
#include "ms_procx_comm.h"
#include "ms_l1c_controller.h"
#include "ms_logger.h"
#include "ms_l1c_modem_mgr.h"

/*------------------------------------------
                DEFINES
--------------------------------------------*/
#define MODEM_MGR_TICK_TIMEOUT_MS    10U
#define MAX_CAMERA_ID                5U
#define MODEM_MGR_IPC_DEV_ID         0U
#define MODEM_MGR_HOST_TO_MODEM_Q    0U
#define MODEM_MGR_MODEM_TO_HOST_Q    1U

/*------------------------------------------
                VARIABLES
--------------------------------------------*/
typedef void (*modem_mgr_ipc_msg_cb_t)( void *data );

static uint32_t modem_slot_count;
static uint32_t modem_frame_count;
static bool modem_mgr_ipc_queue_configured;
static bool modem_mgr_m2h_queue_configured;
static modem_mgr_ipc_msg_cb_t modem_mgr_host_ipc_cbs[MS_MSG_OPCODE_MAX];

/*------------------------------------------
                FUNCTIONS
--------------------------------------------*/

/**
 * @brief Sends a message to the host via the Modem->Host bbdev IPC queue.
 *
 * Lazily configures the queue on first use.  Drops the message silently if the
 * host-side queue is not yet ready or no internal buffer is available.
 *
 * @param[in] msg  Message to forward to the host.
 */
static void modem_mgr_send_to_host( const S_UNIFIED_MSG_BUFF *msg )
{
    struct bbdev_ipc_raw_op_t raw_op;
    void    *buf;
    uint32_t buf_len;
    int      ret;

    if( msg == NULL )
    {
        return;
    }

    if( modem_mgr_m2h_queue_configured == false )
    {
        if( bbdev_ipc_is_host_initialized( MODEM_MGR_IPC_DEV_ID ) == 0 )
        {
            return;
        }

        ret = bbdev_ipc_queue_configure( MODEM_MGR_IPC_DEV_ID, MODEM_MGR_MODEM_TO_HOST_Q );
        if( ret == IPC_SUCCESS )
        {
            modem_mgr_m2h_queue_configured = true;
        }
        else
        {
            log_err( "[MODEMMGR] M2H queue not ready\r\n" );
            return;
        }
    }

    buf = bbdev_ipc_get_next_internal_buf( MODEM_MGR_IPC_DEV_ID,
                                           MODEM_MGR_MODEM_TO_HOST_Q, &buf_len );
    if( ( buf == NULL ) || ( buf_len < sizeof( S_UNIFIED_MSG_BUFF ) ) )
    {
        log_err( "[MODEMMGR] No M2H buffer available\r\n" );
        return;
    }

    memcpy( buf, msg, sizeof( S_UNIFIED_MSG_BUFF ) );

    raw_op.in_addr  = (uint32_t) buf;
    raw_op.in_len   = sizeof( S_UNIFIED_MSG_BUFF );
    raw_op.out_addr = 0U;
    raw_op.out_len  = 0U;

    ret = bbdev_ipc_enqueue_raw_op( MODEM_MGR_IPC_DEV_ID, MODEM_MGR_MODEM_TO_HOST_Q, &raw_op );
    if( ret != IPC_SUCCESS )
    {
        log_err( "[MODEMMGR] Failed to enqueue M2H message (ret=%d)\r\n", ret );
    }
    else
    {
        switch( msg->opcode )
        {
            case MS_MSG_OPCODE_CTRL_ACK_FAIL:
                log_err( "[MODEMMGR] CTRL_ACK_FAIL sent to host (camera_id=%u)\r\n",
                         (unsigned) msg->camera_id );
                break;
            case MS_MSG_OPCODE_RX_READY:
                log_info( "[MODEMMGR] RX_READY sent to host (camera_id=%u frame=%lu)\r\n",
                          (unsigned) msg->camera_id, ( unsigned long ) msg->time );
                break;
            default:
                log_info( "[MODEMMGR] CTRL_ACK sent to host (camera_id=%u)\r\n",
                          (unsigned) msg->camera_id );
                break;
        }
    }
}

/**
 * @brief Dequeues and dispatches Host->Modem IPC messages to modem-manager handlers.
 *
 * Lazily configures the bbdev Host->Modem queue on first use, then drains all
 * available raw operations. Each valid payload is interpreted as
 * @c S_UNIFIED_MSG_BUFF and routed by opcode through the modem-manager callback
 * table.
 */
static void modem_mgr_process_host_ipc_messages( void )
{
    struct bbdev_ipc_raw_op_t *raw_op;
    S_UNIFIED_MSG_BUFF *msg;
    modem_mgr_ipc_msg_cb_t cb;

    if( modem_mgr_ipc_queue_configured == false )
    {
        if( bbdev_ipc_is_host_initialized( MODEM_MGR_IPC_DEV_ID ) == 0 )
        {
            return;
        }

        /* Configure queue once; retry on later ticks if host side is not ready yet. */
        if( bbdev_ipc_queue_configure( MODEM_MGR_IPC_DEV_ID, MODEM_MGR_HOST_TO_MODEM_Q ) == IPC_SUCCESS )
        {
            modem_mgr_ipc_queue_configured = true;
        }
        else
        {
            return;
        }
    }

    raw_op = bbdev_ipc_dequeue_raw_op( MODEM_MGR_IPC_DEV_ID, MODEM_MGR_HOST_TO_MODEM_Q );
    while( raw_op != NULL )
    {
        /* Only process payloads that match the expected unified message shape. */
        if( ( raw_op->in_addr != 0U ) &&
            ( raw_op->in_len >= sizeof( S_UNIFIED_MSG_BUFF ) ) )
        {
            msg = (S_UNIFIED_MSG_BUFF *) raw_op->in_addr;

            /* Dispatch by opcode if a callback is registered for that message type. */
            if( msg->opcode < MS_MSG_OPCODE_MAX )
            {
                cb = modem_mgr_host_ipc_cbs[msg->opcode];
                if( cb != NULL )
                {
                    cb( msg );
                }
            }
        }

        (void) bbdev_ipc_consume_raw_op( MODEM_MGR_IPC_DEV_ID, MODEM_MGR_HOST_TO_MODEM_Q, raw_op );
        /* Continue draining until queue is empty for this tick. */
        raw_op = bbdev_ipc_dequeue_raw_op( MODEM_MGR_IPC_DEV_ID, MODEM_MGR_HOST_TO_MODEM_Q );
    }
}

/**
 * @brief Cross-task callback invoked when a message arrives for modem-manager processing.
 *
 * @param[in] src_proc  Source processor ID that sent the message.
 * @param[in] data      Pointer to an @c S_UNIFIED_MSG_BUFF payload.
 */
void modem_mgr_cb_xc( procx_comm_id_e src_proc, void *data )
{
    S_UNIFIED_MSG_BUFF *msg = (S_UNIFIED_MSG_BUFF *) data;

    switch( src_proc )
    {
        case MDMMGR_XC_ID:
            break;
        case TIMER_HANDLER_XC_ID:
            break;
        case VSPA_IN_XC_ID:
            break;
        case VSPA_OUT_XC_ID:
            break;
        case RX_XC_ID:
            if( msg != NULL )
            {
                if( msg->opcode == MS_MSG_OPCODE_CTRL_ACK ||
                    msg->opcode == MS_MSG_OPCODE_CTRL_ACK_FAIL )
                {
                    modem_mgr_send_to_host( msg );
                }
                else if( msg->opcode == MS_MSG_OPCODE_RX_READY )
                {
                    /* Stamp the next frame number so the host knows when
                     * to expect the first video data, then forward. */
                    S_UNIFIED_MSG_BUFF rx_ready = *msg;
                    rx_ready.time = modem_frame_count + 1U;
                    log_info( "[MODEMMGR] RX_READY: notifying host, video starts frame %lu (camera_id=%u)\r\n",
                              ( unsigned long ) rx_ready.time,
                              (unsigned) rx_ready.camera_id );
                    modem_mgr_send_to_host( &rx_ready );
                }
            }
            break;
        case TX_XC_ID:
            break;
        case LOG_XC_ID:
            break;
        case SLOT_INTR_XC_ID:
            break;
        case RSSI_INTR_XC_ID:
            break;
        case L2_TEST_XC_ID:
            break;
        default:
            break;
    }

    if( ( msg != NULL ) && ( msg->opcode == MS_MSG_OPCODE_CONTROL_MSG ) )
    {
        return;
    }
}

/**
 * @brief Handles Host IPC control messages and forwards them to transmitter.
 *
 * @param[in] data  Pointer to @c S_UNIFIED_MSG_BUFF from Host IPC path.
 */
void modem_mgr_ipc_control_msg_cb( void *data )
{
    S_UNIFIED_MSG_BUFF *msg = (S_UNIFIED_MSG_BUFF *) data;

    if( ( msg == NULL ) ||
        ( msg->opcode != MS_MSG_OPCODE_CONTROL_MSG ) ||
        ( msg->camera_id > MAX_CAMERA_ID ) )
    {
        return;
    }

    (void) procx_comm( TX_XC_ID, MDMMGR_XC_ID, msg );
}

/**
 * @brief Handles Host IPC start-video-RX messages and forwards them to the receiver.
 *
 * @param[in] data  Pointer to @c S_UNIFIED_MSG_BUFF from Host IPC path.
 */
void modem_mgr_ipc_start_video_rx_cb( void *data )
{
    S_UNIFIED_MSG_BUFF *msg = (S_UNIFIED_MSG_BUFF *) data;

    if( ( msg == NULL ) ||
        ( msg->opcode != MS_MSG_OPCODE_START_VIDEO_RX ) ||
        ( msg->camera_id > MAX_CAMERA_ID ) )
    {
        return;
    }

    log_info( "[MODEMMGR] START_VIDEO_RX from host (camera_id=%u)\r\n",
              (unsigned) msg->camera_id );

    (void) procx_comm( RX_XC_ID, MDMMGR_XC_ID, msg );
}

/**
 * @brief Returns modem-manager-owned slot and frame counters.
 *
 * @param[out] slot_count   Optional pointer to current slot count.
 * @param[out] frame_count  Optional pointer to current frame count.
 */
void modem_mgr_get_slot_frame_count( uint32_t *slot_count, uint32_t *frame_count )
{
    if( slot_count != NULL )
    {
        *slot_count = modem_slot_count;
    }

    if( frame_count != NULL )
    {
        *frame_count = modem_frame_count;
    }
}

/**
 * @brief Modem manager main task.
 *
 * The task is tick-driven and owns global slot/frame progression. Per tick it
 * ingests Host IPC messages, generates BCH notification at the last slot, and
 * advances slot/frame counters.
 *
 * @param[in] arg  Unused task argument.
 *
 * @return Never returns.
 */
void *modem_mgr_task( void *arg )
{
    S_UNIFIED_MSG_BUFF tx_msg;

    (void) arg;

    log_info( "[MODEM MGR] task started\r\n" );

    tx_msg.payload = NULL;

    for( ; ; )
    {
        if( l1_controller_wait_on_tick( L1_MODEM_MGR_TASK, MODEM_MGR_TICK_TIMEOUT_MS ) == Success )
        {
            /* Keep host IPC ingestion tick-synchronized with modem slot/frame ownership. */
            modem_mgr_process_host_ipc_messages();

            /* Notify TX one slot before frame rollover so BCH goes in next frame slot 0. */
            if( modem_slot_count == ( MAX_NUM_OF_SLOTS_IN_FRAME - 1U ) )
            {
                tx_msg.opcode = MS_MSG_OPCODE_BCH_SEND;
                (void) procx_comm( TX_XC_ID, MDMMGR_XC_ID, &tx_msg );
            }

            /* Advance global timing counters maintained by modem manager. */
            modem_slot_count++;
            if( modem_slot_count >= MAX_NUM_OF_SLOTS_IN_FRAME )
            {
                modem_slot_count = 0U;
                modem_frame_count++;
            }
        }
    }

    return NULL;
}

/**
 * @brief Initializes the L1 modem manager.
 *
 * Sets up the modem manager task and associated resources responsible for
 * coordinating modem state transitions and control-plane signalling between
 * L1 subsystems. Placeholder — implementation to be added when modem
 * management logic is defined.
 */
void l1_controller_modem_mgr_init( uint8_t core_num )
{
    uint32_t i;

    modem_slot_count = 0U;
    modem_frame_count = 0U;
    modem_mgr_ipc_queue_configured = false;
    modem_mgr_m2h_queue_configured = false;

    for( i = 0U; i < MS_MSG_OPCODE_MAX; i++ )
    {
        modem_mgr_host_ipc_cbs[i] = NULL;
    }

    modem_mgr_host_ipc_cbs[MS_MSG_OPCODE_CONTROL_MSG]    = modem_mgr_ipc_control_msg_cb;
    modem_mgr_host_ipc_cbs[MS_MSG_OPCODE_START_VIDEO_RX] = modem_mgr_ipc_start_video_rx_cb;

    UNUSED( core_num );

    procx_comm_reg( modem_mgr_cb_xc, MDMMGR_XC_ID );

    return;
}
