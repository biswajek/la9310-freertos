/*
 * Mundo Sense
 */

#include <phytimer.h>
#include "ms_camera_global_typedef.h"
#include "ms_camera_l1c_controller.h"
#include "ms_camera_l1c_modem_mgr.h"

#define L1C_CAMERA_TIMER_TICK_MS     10U

void vL1cPhyTimerTickHook( void )
{
    camera_mgr_on_phytick();
    l1_camera_signal_tick_tasks_from_isr();
}

void l1_camera_phytimer_init( uint8_t core_num )
{
    UNUSED( core_num );

    vPhyTimerPPSOUTSetPeriodMs( L1C_CAMERA_TIMER_TICK_MS );
    vPhyTimerPPSOUTConfig();
}
