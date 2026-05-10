/*
 * Mundo Sense
 */

#include <stdbool.h>
#include <string.h>
#include "pal_sem.h"
#include "bbdev_ipc.h"
#include "ms_camera_global_typedef.h"
#include "ms_camera_globals.h"
#include "ms_camera_procx_comm.h"
#include "ms_camera_l1c_controller.h"
#include "ms_camera_logger.h"
#include "ms_camera_l1c_modem_mgr.h"

#define CAMERA_MGR_TICK_TIMEOUT_MS    10U
#define MAX_CAMERA_ID                  5U
#define CAMERA_MGR_IPC_DEV_ID          0U
#define CAMERA_MGR_HOST_TO_MODEM_Q     0U
#define CAMERA_MGR_MODEM_TO_HOST_Q     1U

typedef void (*camera_mgr_ipc_msg_cb_t)( void *data );

static uint32_t camera_slot_count;
static uint32_t camera_frame_count;
static bool     camera_mgr_ipc_queue_configured;
static bool     camera_mgr_m2h_queue_configured;
static camera_mgr_ipc_msg_cb_t camera_mgr_host_ipc_cbs[MS_MSG_OPCODE_MAX];

static bool    pending_bch_ctrl_valid;
static uint8_t pending_bch_ctrl_camera_id;
static void   *pending_bch_ctrl_payload;

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

        ret = bbdev_ipc_queue_configure( CAMERA_MGR_IPC_DEV_ID, CAMERA_MGR_MODEM_TO_HOST_Q );
        if( ret == IPC_SUCCESS )
            camera_mgr_m2h_queue_configured = true;
        else
        {
            log_err( "[CAMERA MGR] M2H queue not ready\r\n" );
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

    ret = bbdev_ipc_enqueue_raw_op( CAMERA_MGR_IPC_DEV_ID, CAMERA_MGR_MODEM_TO_HOST_Q, &raw_op );
    if( ret != IPC_SUCCESS )
    {
        log_err( "[CAMERA MGR] Failed to enqueue M2H message (ret=%d)\r\n", ret );
    }
    else
    {
        switch( msg->opcode )
        {
            case MS_MSG_OPCODE_CTRL_ACK_FAIL:
                log_err( "[CAMERA MGR] CTRL_ACK_FAIL sent to host (camera_id=%u)\r\n",
                         (unsigned) msg->camera_id );
                break;
            case MS_MSG_OPCODE_TX_READY:
                log_info( "[CAMERA MGR] TX_READY sent to host (camera_id=%u frame=%lu)\r\n",
                          (unsigned) msg->camera_id, (unsigned long) msg->time );
                break;
            case MS_MSG_OPCODE_HOST_TEST_REPLY:
                log_info( "[CAMERA MGR] HOST_TEST_REPLY sent to host (camera_id=%u)\r\n",
                          (unsigned) msg->camera_id );
                break;
            default:
                log_info( "[CAMERA MGR] CTRL_ACK sent to host (camera_id=%u)\r\n",
                          (unsigned) msg->camera_id );
                break;
        }
    }
}

static void camera_mgr_process_host_ipc_messages( void )
{
    struct bbdev_ipc_raw_op_t *raw_op;
    S_UNIFIED_MSG_BUFF        *msg;
    camera_mgr_ipc_msg_cb_t    cb;

    if( ( camera_mgr_ipc_queue_configured == false ) ||
        ( camera_mgr_m2h_queue_configured == false ) )
    {
        if( bbdev_ipc_is_host_initialized( CAMERA_MGR_IPC_DEV_ID ) == 0 )
            return;

        if( camera_mgr_ipc_queue_configured == false )
        {
            if( bbdev_ipc_queue_configure( CAMERA_MGR_IPC_DEV_ID,
                                           CAMERA_MGR_HOST_TO_MODEM_Q ) == IPC_SUCCESS )
                camera_mgr_ipc_queue_configured = true;
            else
                return;
        }

        if( camera_mgr_m2h_queue_configured == false )
        {
            if( bbdev_ipc_queue_configure( CAMERA_MGR_IPC_DEV_ID,
                                           CAMERA_MGR_MODEM_TO_HOST_Q ) == IPC_SUCCESS )
                camera_mgr_m2h_queue_configured = true;
            else
                return;
        }
    }

    raw_op = bbdev_ipc_dequeue_raw_op( CAMERA_MGR_IPC_DEV_ID, CAMERA_MGR_HOST_TO_MODEM_Q );
    while( raw_op != NULL )
    {
        if( ( raw_op->in_addr != 0U ) &&
            ( raw_op->in_len >= sizeof( S_UNIFIED_MSG_BUFF ) ) )
        {
            msg = (S_UNIFIED_MSG_BUFF *) raw_op->in_addr;

            if( msg->opcode < MS_MSG_OPCODE_MAX )
            {
                cb = camera_mgr_host_ipc_cbs[msg->opcode];
                if( cb != NULL )
                    cb( msg );
            }
        }

        (void) bbdev_ipc_consume_raw_op( CAMERA_MGR_IPC_DEV_ID,
                                         CAMERA_MGR_HOST_TO_MODEM_Q, raw_op );
        raw_op = bbdev_ipc_dequeue_raw_op( CAMERA_MGR_IPC_DEV_ID, CAMERA_MGR_HOST_TO_MODEM_Q );
    }
}

void camera_mgr_cb_xc( procx_comm_id_e src_proc, void *data )
{
    S_UNIFIED_MSG_BUFF *msg = (S_UNIFIED_MSG_BUFF *) data;

    switch( src_proc )
    {
        case CAMMGR_XC_ID:
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
                    camera_mgr_send_to_host( msg );
                }
                else if( msg->opcode == MS_MSG_OPCODE_TX_READY )
                {
                    S_UNIFIED_MSG_BUFF tx_ready = *msg;
                    tx_ready.time = camera_frame_count + 1U;
                    log_info( "[CAMERA MGR] TX_READY: notifying host, video starts frame %lu (camera_id=%u)\r\n",
                              (unsigned long) tx_ready.time, (unsigned) tx_ready.camera_id );
                    camera_mgr_send_to_host( &tx_ready );
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
}

void camera_mgr_ipc_bch_send_cb( void *data )
{
    S_UNIFIED_MSG_BUFF *msg = (S_UNIFIED_MSG_BUFF *) data;

    if( ( msg == NULL ) ||
        ( msg->opcode != MS_MSG_OPCODE_BCH_SEND ) ||
        ( msg->camera_id > MAX_CAMERA_ID ) )
        return;

    pending_bch_ctrl_camera_id = msg->camera_id;
    pending_bch_ctrl_payload   = msg->payload;
    pending_bch_ctrl_valid     = true;
}

void camera_mgr_ipc_start_video_tx_cb( void *data )
{
    S_UNIFIED_MSG_BUFF *msg = (S_UNIFIED_MSG_BUFF *) data;

    if( ( msg == NULL ) ||
        ( msg->opcode != MS_MSG_OPCODE_START_VIDEO_TX ) ||
        ( msg->camera_id > MAX_CAMERA_ID ) )
        return;

    log_info( "[CAMERA MGR] START_VIDEO_TX from host (camera_id=%u)\r\n",
              (unsigned) msg->camera_id );

    (void) procx_comm( RX_XC_ID, CAMMGR_XC_ID, msg );
}

void camera_mgr_get_slot_frame_count( uint32_t *slot_count, uint32_t *frame_count )
{
    if( slot_count != NULL )
        *slot_count = camera_slot_count;

    if( frame_count != NULL )
        *frame_count = camera_frame_count;
}

void *camera_mgr_task( void *arg )
{
    S_UNIFIED_MSG_BUFF tx_msg;

    (void) arg;

    log_info( "[CAMERA MGR] task started\r\n" );

    for( ; ; )
    {
        if( l1_camera_wait_on_tick( L1_CAMERA_MGR_TASK, CAMERA_MGR_TICK_TIMEOUT_MS ) == Success )
        {
            camera_mgr_process_host_ipc_messages();

            /* Notify TX one slot before frame rollover so BCH goes in next frame slot 0.
             * Carry any pending control parameters (camera_id/payload) from the host. */
            if( camera_slot_count == ( MAX_NUM_OF_SLOTS_IN_FRAME - 1U ) )
            {
                tx_msg.opcode    = MS_MSG_OPCODE_BCH_SEND;
                tx_msg.time      = camera_frame_count;
                tx_msg.camera_id = pending_bch_ctrl_valid ? pending_bch_ctrl_camera_id : 0U;
                tx_msg.payload   = pending_bch_ctrl_valid ? pending_bch_ctrl_payload    : NULL;
                pending_bch_ctrl_valid = false;
                (void) procx_comm( TX_XC_ID, CAMMGR_XC_ID, &tx_msg );
            }

            camera_slot_count++;
            if( camera_slot_count >= MAX_NUM_OF_SLOTS_IN_FRAME )
            {
                camera_slot_count = 0U;
                camera_frame_count++;
            }
        }
        else
        {
            camera_mgr_process_host_ipc_messages();
        }
    }

    return NULL;
}

void l1_camera_mgr_init( uint8_t core_num )
{
    uint32_t i;

    camera_slot_count  = 0U;
    camera_frame_count = 0U;
    camera_mgr_ipc_queue_configured = false;
    camera_mgr_m2h_queue_configured = false;

    pending_bch_ctrl_valid     = false;
    pending_bch_ctrl_camera_id = 0U;
    pending_bch_ctrl_payload   = NULL;

    for( i = 0U; i < MS_MSG_OPCODE_MAX; i++ )
        camera_mgr_host_ipc_cbs[i] = NULL;

    camera_mgr_host_ipc_cbs[MS_MSG_OPCODE_BCH_SEND]       = camera_mgr_ipc_bch_send_cb;
    camera_mgr_host_ipc_cbs[MS_MSG_OPCODE_START_VIDEO_TX] = camera_mgr_ipc_start_video_tx_cb;

    UNUSED( core_num );

    procx_comm_reg( camera_mgr_cb_xc, CAMMGR_XC_ID );
}
