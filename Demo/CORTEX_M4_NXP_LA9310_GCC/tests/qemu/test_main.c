/*
 * QEMU test entry point.
 *
 * Initialises the IPC subsystems, runs all test cases in a FreeRTOS task,
 * and reports PASS / FAIL to the QEMU console via semihosting printf.
 */

#include <stdio.h>
#include <stdlib.h>
extern void initialise_monitor_handles(void);
#include "FreeRTOS.h"
#include "task.h"
#include "ipiQueue.h"
#include "ms_globals.h"
#include "ms_logger.h"
#include "test_ipc.h"

/* ------------------------------------------------------------------ */
/* QEMU semihosting exit                                               */
/* ------------------------------------------------------------------ */
static void qemu_exit(int code)
{
    /* ARM semihosting SYS_EXIT */
    register uint32_t r0 __asm("r0") = 0x18;          /* SYS_EXIT */
    register uint32_t r1 __asm("r1") = (code == 0)    /* ADP_Stopped reason */
                                        ? 0x20026u     /* ApplicationExit */
                                        : 0x20023u;    /* RunTimeError */
    __asm volatile ("bkpt 0xab" :: "r"(r0), "r"(r1) : "memory");
    for (;;);
}

/* ------------------------------------------------------------------ */
/* Test runner task                                                     */
/* ------------------------------------------------------------------ */
static void test_runner_task(void *pvParams)
{
    (void)pvParams;

    ms_log_init(MS_LOG_LEVEL_DBG);
    log_info("\r\n=== IPC Test Suite (QEMU mps2-an386) ===\r\n");

    int rc = test_ipc_run_all();

    if (rc == 0)
        log_info("\r\n=== ALL TESTS PASSED ===\r\n");
    else
        log_err("\r\n=== TESTS FAILED (rc=%d) ===\r\n", rc);

    qemu_exit(rc == 0 ? 0 : 1);
}

/* ------------------------------------------------------------------ */
/* FreeRTOS application hooks                                           */
/* ------------------------------------------------------------------ */
void vApplicationMallocFailedHook(void)
{
    log_err("Malloc failed\r\n");
    for (;;);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    log_err("Stack overflow in task: %s\r\n", pcTaskName);
    for (;;);
}

void vApplicationIdleHook(void)  {}
void vApplicationTickHook(void)  {}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */
int main(void)
{
    initialise_monitor_handles();   /* enable semihosting I/O */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Initialise IPI subsystem for core 0 */
    vIPICoreInit(0);

    xTaskCreate(test_runner_task, "TestRunner", 1024, NULL, 3, NULL);
    vTaskStartScheduler();

    /* Never reached */
    return 0;
}
