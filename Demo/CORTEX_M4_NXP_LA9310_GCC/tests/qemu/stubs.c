/*
 * Hardware and board stubs for the QEMU IPC test build.
 *
 * This file replaces all LA9310-specific peripherals with no-ops so the
 * messaging layer (ipiQueue, procx_comm, ms_l1_controller_fwk) can be
 * tested without any real hardware.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include "FreeRTOS.h"

/* ------------------------------------------------------------------ */
/* debug_printf — ms_logger routes all output through this symbol.     */
/* Redirect to semihosting printf so output appears in the terminal.   */
/* ------------------------------------------------------------------ */
int debug_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vprintf(fmt, args);
    va_end(args);
    return n;
}

/* ------------------------------------------------------------------ */
/* SystemCoreClock — required by FreeRTOS port internals.              */
/* ------------------------------------------------------------------ */
uint32_t SystemCoreClock = 25000000ul;

/* ------------------------------------------------------------------ */
/* Phy timer stubs                                                      */
/* ------------------------------------------------------------------ */
void vPhyTimerReset(void)              {}
void vPhyTimerEnable(uint8_t d)        { (void)d; }
void vPhyTimerDisable(void)            {}
void vPhyTimerPPSOUTConfig(void)       {}
void vPhyTimerPPSOUTHandler(void)      {}
void vPhyTimerPPSINEnable(void)        {}
void vPhyTimerPPSOUTSetPeriodMs(uint32_t ms) { (void)ms; }
uint32_t ulPhyTimerCapture(uint8_t c)  { (void)c; return 0; }
void vPhyTimerComparatorConfig(uint8_t c, uint32_t cfg,
                                int trig, uint32_t val)
{ (void)c; (void)cfg; (void)trig; (void)val; }
void vPhyTimerComparatorDisable(uint8_t c) { (void)c; }
void vPhyTimerComparatorForce(uint8_t c, int v) { (void)c; (void)v; }

/* ------------------------------------------------------------------ */
/* RF manager stubs                                                     */
/* ------------------------------------------------------------------ */
int l1_controller_rf_manager_init(void) { return 0; }

/* ------------------------------------------------------------------ */
/* RFIC stubs                                                           */
/* ------------------------------------------------------------------ */
void rfic_init(void)   {}
void rfic_deinit(void) {}

/* ------------------------------------------------------------------ */
/* bbdev / host IPC stubs                                               */
/* ------------------------------------------------------------------ */
int ipc_send_msg(void *msg)       { (void)msg; return 0; }
int ipc_recv_msg(void *msg)       { (void)msg; return -1; }

/* ------------------------------------------------------------------ */
/* DCS / AVI / GPIO / EDMA stubs — weak aliases not needed since these */
/* translation units are simply excluded from the test build.           */
/* ------------------------------------------------------------------ */
