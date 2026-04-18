/*
 * Inter-task IPC test cases for QEMU mps2-an386.
 *
 * Three layers tested:
 *   1. procx_comm  — synchronous callback dispatch between named processors
 *   2. ipiQueue    — IPI event delivery with PAL queue backing
 *   3. fwk queue   — init_proc_cmd_queue / send_cmd_to_proc / pal_msgq_receive
 *
 * Test 4 exercises the full cross-task flow:
 *   A producer task injects a message; a consumer task unblocks and receives it.
 */

#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "pal_msgq.h"
#include "ipiQueue.h"
#include "ms_procx_comm.h"
#include "ms_l1_controller_fwk.h"
#include "ms_global_typedef.h"
#include "ms_globals.h"
#include "ms_logger.h"
#include "test_ipc.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

#define TEST_PASS  0
#define TEST_FAIL -1

static int g_result;

static void report(const char *name, int rc)
{
    if (rc == TEST_PASS)
        log_info("PASS: %s\r\n", name);
    else
        log_err("FAIL: %s\r\n", name);
    g_result |= rc;
}

/* ------------------------------------------------------------------ */
/* Test 1 — procx_comm synchronous dispatch                            */
/* ------------------------------------------------------------------ */

static volatile int   t1_cb_count;
static volatile int   t1_src_id;
static volatile MS_MSG_OPCODE t1_opcode;

static void t1_callback(procx_comm_id_e src, void *data)
{
    S_UNIFIED_MSG_BUFF *msg = (S_UNIFIED_MSG_BUFF *)data;
    t1_cb_count++;
    t1_src_id  = (int)src;
    t1_opcode  = msg->opcode;
}

static int test_procx_comm(void)
{
    S_UNIFIED_MSG_BUFF msg = {
        .opcode  = MS_MSG_OPCODE_CONTROL_MSG,
        .payload = NULL,
    };

    t1_cb_count = 0;
    t1_src_id   = -1;

    procx_comm_reg(t1_callback, VSPA_IN_XC_ID);
    procx_comm(VSPA_IN_XC_ID, TX_XC_ID, &msg);

    if (t1_cb_count != 1)              return TEST_FAIL;
    if (t1_src_id   != (int)TX_XC_ID) return TEST_FAIL;
    if (t1_opcode   != MS_MSG_OPCODE_CONTROL_MSG) return TEST_FAIL;
    return TEST_PASS;
}

/* ------------------------------------------------------------------ */
/* Test 2 — IPI queue: same-core send then queue receive               */
/* ------------------------------------------------------------------ */

static int test_ipi_queue(void)
{
    MsgQ_Handle_t q = 0;
    S_UNIFIED_MSG_BUFF send_msg = { .opcode = MS_MSG_OPCODE_BCH_SEND };
    MsgQ_Priorities_t prio_out;

    enum IPIEventID ev = vIPIEventRegister(
        IPI_EVT_TESTFRAMEWORK,
        &q,
        NULL,           /* queue-only mode: no callback */
        NULL,
        sizeof(S_UNIFIED_MSG_BUFF),
        "t2q");

    if (ev == IPI_EVT_ID_NULL) return TEST_FAIL;

    /* Same-core send: use the actually-assigned event ID from registration */
    if (!vIPISendData(0, ev, &send_msg)) return TEST_FAIL;

    /* IPI queue-only mode enqueues a void* pointer, not the message directly */
    void *ptr = NULL;
    size_t ptr_size = sizeof(void *);
    Error_t rc2 = pal_msgq_receive(q, &ptr, sizeof(void *),
                                   &ptr_size, &prio_out, 100);
    if (rc2 != Success)                                             return TEST_FAIL;
    if (((S_UNIFIED_MSG_BUFF *)ptr)->opcode != MS_MSG_OPCODE_BCH_SEND) return TEST_FAIL;

    vIPIEventUnRegister(ev);
    return TEST_PASS;
}

/* ------------------------------------------------------------------ */
/* Test 3 — Framework queue: init / send / receive                     */
/* ------------------------------------------------------------------ */

static int test_framework_queue(void)
{
    proc_queue_t q;
    S_UNIFIED_MSG_BUFF send_msg = { .opcode = MS_MSG_OPCODE_CONTROL_MSG, .time = 42 };
    S_UNIFIED_MSG_BUFF recv_msg;
    size_t recv_size = sizeof(recv_msg);
    MsgQ_Priorities_t prio_out;

    Error_t err = init_proc_cmd_queue(&q, 0, sizeof(S_UNIFIED_MSG_BUFF), "t3q");
    if (err != Success) return TEST_FAIL;

    err = send_cmd_to_proc(&q, &send_msg, sizeof(send_msg));
    if (err != Success) return TEST_FAIL;

    memset(&recv_msg, 0, sizeof(recv_msg));
    Error_t rc3 = pal_msgq_receive(q.queue, &recv_msg, sizeof(recv_msg),
                                   &recv_size, &prio_out, 100);
    if (rc3 != Success)                                return TEST_FAIL;
    if (recv_msg.opcode != MS_MSG_OPCODE_CONTROL_MSG)  return TEST_FAIL;
    if (recv_msg.time   != 42)                         return TEST_FAIL;

    return TEST_PASS;
}

/* ------------------------------------------------------------------ */
/* Test 4 — Cross-task: producer sends, consumer receives              */
/* ------------------------------------------------------------------ */

static proc_queue_t t4_queue;
static SemaphoreHandle_t t4_done_sem;
static volatile MS_MSG_OPCODE t4_recv_opcode;
static volatile uint32_t t4_recv_time;

static void consumer_task(void *pvParams)
{
    S_UNIFIED_MSG_BUFF msg;
    size_t sz = sizeof(msg);
    MsgQ_Priorities_t prio;

    (void)pvParams;
    Error_t rc = pal_msgq_receive(t4_queue.queue, &msg, sizeof(msg),
                                  &sz, &prio, portMAX_DELAY);
    if (rc == Success) {
        t4_recv_opcode = msg.opcode;
        t4_recv_time   = msg.time;
    }
    xSemaphoreGive(t4_done_sem);
    vTaskDelete(NULL);
}

static int test_cross_task_message(void)
{
    S_UNIFIED_MSG_BUFF msg = { .opcode = MS_MSG_OPCODE_BCH_SEND, .time = 0xBEEF };

    t4_done_sem = xSemaphoreCreateBinary();
    if (!t4_done_sem) return TEST_FAIL;

    Error_t err = init_proc_cmd_queue(&t4_queue, 0, sizeof(S_UNIFIED_MSG_BUFF), "t4q");
    if (err != Success) return TEST_FAIL;

    /* Start consumer first so it blocks on the queue */
    if (xTaskCreate(consumer_task, "Consumer", 256, NULL, 2, NULL) != pdPASS)
        return TEST_FAIL;

    /* Give scheduler a tick to let consumer block */
    vTaskDelay(pdMS_TO_TICKS(5));

    err = send_cmd_to_proc(&t4_queue, &msg, sizeof(msg));
    if (err != Success) return TEST_FAIL;

    /* Wait up to 500 ms for consumer to confirm receipt */
    if (xSemaphoreTake(t4_done_sem, pdMS_TO_TICKS(500)) != pdTRUE)
        return TEST_FAIL;

    if (t4_recv_opcode != MS_MSG_OPCODE_BCH_SEND) return TEST_FAIL;
    if (t4_recv_time   != 0xBEEF)                 return TEST_FAIL;

    vSemaphoreDelete(t4_done_sem);
    return TEST_PASS;
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                   */
/* ------------------------------------------------------------------ */

int test_ipc_run_all(void)
{
    g_result = TEST_PASS;

    report("procx_comm dispatch",       test_procx_comm());
    report("ipiQueue send/receive",     test_ipi_queue());
    report("framework queue",           test_framework_queue());
    report("cross-task message",        test_cross_task_message());

    return g_result;
}
