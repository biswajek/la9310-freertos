/*
 * Mundo Sense
 */

/**
 * @file ms_camera_l1c_modem_mgr.c
 * @brief Camera manager — receives BCH decode notifications from receiver
 *        and forwards them to the upper layer (host) via IPC.
 */

#include <stdbool.h>
#include <string.h>
#include "pal_thread.h"
#include "pal_types.h"
#include "bbdev_ipc.h"
#include "ms_camera_global_typedef.h"
#include "ms_camera_globals.h"
#include "ms_camera_logger.h"
#include "ms_camera_l1c_controller.h"
#include "ms_camera_l1_controller_fwk.h"
#include "ms_camera_procx_comm.h"
#include "ms_camera_l1c_modem_mgr.h"

#define CAMERA_MGR_QUEUE_NAME        "camMgrQ"
#define CAMERA_MGR_Q_WAIT_MS         1U
#define CAMERA_MGR_IPC_DEV_ID        0U
#define CAMERA_MGR_HOST_TO_MODEM_Q   0U
#define CAMERA_MGR_MODEM_TO_HOST_Q   1U

static proc_queue_t camera_mgr_q;
static bool         camera_mgr_h2m_queue_configured;
static bool         camera_mgr_m2h_queue_configured;
static uint32_t     camera_slot_count;
static uint32_t     camera_frame_count;

static void camera_mgr_send_to_host( const S_UNIFIED_MSG_BUFF *msg )
{
    struct bbdev_ipc_raw_op_t raw_op;
    void    *buf;
    uint32_t buf_len;
    int      ret;

    if( msg == NULL )
        return;

    if( camera_mgr_m2h_queue_configured == false )
    {
        if( bbdev_ipc_is_host_initialized( CAMERA_MGR_IPC_DEV_ID ) == 0 )
            return;

        ret = bbdev_ipc_queue_configure( CAMERA_MGR_IPC_DEV_ID,
                                         CAMERA_MGR_MODEM_TO_HOST_Q );
        if( ret == IPC_SUCCESS )
            camera_mgr_m2h_queue_configured = true;
        else
        {
            log_err( "[CAMERA MGR] M2H IPC queue not ready\r\n" );
            return;
        }
    }

    buf = bbdev_ipc_get_next_internal_buf( CAMERA_MGR_IPC_DEV_ID,
                                           CAMERA_MGR_MODEM_TO_HOST_Q, &buf_len );
    if( ( buf == NULL ) || ( buf_len < sizeof( S_UNIFIED_MSG_BUFF ) ) )
    {
        log_err( "[CAMERA MGR] No M2H buffer available\r\n" );
        return;
    }

    memcpy( buf, msg, sizeof( S_UNIFIED_MSG_BUFF ) );

    raw_op.in_addr  = (uint32_t) buf;
    raw_op.in_len   = sizeof( S_UNIFIED_MSG_BUFF );
    raw_op.out_addr = 0U;
    raw_op.out_len  = 0U;

    ret = bbdev_ipc_enqueue_raw_op( CAMERA_MGR_IPC_DEV_ID,
                                    CAMERA_MGR_MODEM_TO_HOST_Q, &raw_op );
    if( ret != IPC_SUCCESS )
        log_err( "[CAMERA MGR] Failed to enqueue M2H message (ret=%d)\r\n", ret );
    else if( msg->opcode == MS_MSG_OPCODE_ACK_SENT )
        log_info( "[CAMERA MGR] ACK sent to host (camera_id=%u)\r\n",
                  (unsigned) msg->camera_id );
    else
        log_info( "[CAMERA MGR] BCH notified to host (camera_id=%u frame=%lu)\r\n",
                  (unsigned) msg->camera_id, (unsigned long) msg->time );
}

/**
 * @brief Drains the Host-to-Modem IPC queue and dispatches each message.
 *
 * Called every task loop so host commands are processed promptly even when
 * the camera manager is idle between BCH/ACK events.
 */
static void camera_mgr_process_host_ipc_messages( void )
{
    struct bbdev_ipc_raw_op_t *raw_op;
    S_UNIFIED_MSG_BUFF        *msg;

    if( camera_mgr_h2m_queue_configured == false )
    {
        if( bbdev_ipc_is_host_initialized( CAMERA_MGR_IPC_DEV_ID ) == 0 )
            return;

        if( bbdev_ipc_queue_configure( CAMERA_MGR_IPC_DEV_ID,
                                       CAMERA_MGR_HOST_TO_MODEM_Q ) == IPC_SUCCESS )
            camera_mgr_h2m_queue_configured = true;
        else
            return;
    }

    raw_op = bbdev_ipc_dequeue_raw_op( CAMERA_MGR_IPC_DEV_ID,
                                       CAMERA_MGR_HOST_TO_MODEM_Q );
    while( raw_op != NULL )
    {
        if( ( raw_op->in_addr != 0U ) &&
            ( raw_op->in_len >= sizeof( S_UNIFIED_MSG_BUFF ) ) )
        {
            msg = (S_UNIFIED_MSG_BUFF *) raw_op->in_addr;

            if( msg->opcode == MS_MSG_OPCODE_START_VIDEO_TX )
            {
                log_info( "[CAMERA MGR] START_VIDEO_TX from host (camera_id=%u)\r\n",
                          (unsigned) msg->camera_id );
                /* Stub: initiate video capture and transmission pipeline here. */
            }
        }

        (void) bbdev_ipc_consume_raw_op( CAMERA_MGR_IPC_DEV_ID,
                                         CAMERA_MGR_HOST_TO_MODEM_Q, raw_op );
        raw_op = bbdev_ipc_dequeue_raw_op( CAMERA_MGR_IPC_DEV_ID,
                                           CAMERA_MGR_HOST_TO_MODEM_Q );
    }
}

void camera_mgr_cb_xc( procx_comm_id_e src_proc, void *data )
{
    Error_t err = Success;

    switch( src_proc )
    {
        case RX_XC_ID:   /* BCH_NOTIFY_HOST */
        case TX_XC_ID:   /* ACK_SENT        */
            err = send_cmd_to_proc( &camera_mgr_q, data, sizeof( S_UNIFIED_MSG_BUFF ) );
            if( err != Success )
                log_err( "[CAMERA MGR] Msg not pushed in queue (src=%d)\r\n",
                         (int) src_proc );
            break;
        default:
            break;
    }
}

void camera_mgr_get_slot_frame_count( uint32_t *slot_count, uint32_t *frame_count )
{
    if( slot_count != NULL )
        *slot_count = camera_slot_count;

    if( frame_count != NULL )
        *frame_count = camera_frame_count;
}

void camera_mgr_on_phytick( void )
{
    camera_slot_count++;
    if( camera_slot_count >= MAX_NUM_OF_SLOTS_IN_FRAME )
    {
        camera_slot_count = 0U;
        camera_frame_count++;
    }
}

void *camera_mgr_task( void *arg )
{
    S_UNIFIED_MSG_BUFF msg;
    size_t             msg_len;
    MsgQ_Priorities_t  msg_prio;

    (void) arg;

    log_info( "[CAMERA MGR] task started\r\n" );

    for( ; ; )
    {
        /* Poll host IPC on every iteration so START_VIDEO_TX is picked up
         * promptly regardless of whether an internal message is queued. */
        camera_mgr_process_host_ipc_messages();

        if( pal_msgq_receive( camera_mgr_q.queue, &msg, sizeof( msg ),
                              &msg_len, &msg_prio, CAMERA_MGR_Q_WAIT_MS ) != Success )
        {
            continue;
        }

        switch( msg.opcode )
        {
            case MS_MSG_OPCODE_BCH_NOTIFY_HOST:
                /* Forward BCH decode notification to the upper layer. */
                camera_mgr_send_to_host( &msg );
                break;

            case MS_MSG_OPCODE_ACK_SENT:
                /* ACK was transmitted — notify the upper layer. */
                log_info( "[CAMERA MGR] ACK sent (camera_id=%u) — notifying host\r\n",
                          (unsigned) msg.camera_id );
                camera_mgr_send_to_host( &msg );
                break;

            default:
                break;
        }
    }

    return NULL;
}

void l1_camera_mgr_init( uint8_t core_num )
{
    init_proc_cmd_queue( &camera_mgr_q, (core_id_t) core_num,
                         sizeof( S_UNIFIED_MSG_BUFF ), CAMERA_MGR_QUEUE_NAME );

    camera_mgr_h2m_queue_configured = false;
    camera_mgr_m2h_queue_configured = false;
    camera_slot_count  = 0U;
    camera_frame_count = 0U;

    procx_comm_reg( camera_mgr_cb_xc, CAMMGR_XC_ID );
}
