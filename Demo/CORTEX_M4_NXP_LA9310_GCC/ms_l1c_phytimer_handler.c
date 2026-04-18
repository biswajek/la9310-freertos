/*
 * Mundo Sense
 */

/**
 * @file ms_l1c_phytimer_handler.c
 * @brief L1 phy-timer integration and tick hook.
 */

/*------------------------------------------
                INCLUDES
--------------------------------------------*/
#include <phytimer.h>
#include "ms_global_typedef.h"
#include "ms_l1c_controller.h"

/*------------------------------------------
                DEFINES
--------------------------------------------*/
#define L1C_TIMER_TICK_MS     10U

/*------------------------------------------
                FUNCTIONS
--------------------------------------------*/

/**
 * @brief Tick hook invoked from phy-timer ISR path.
 */
void vL1cPhyTimerTickHook( void )
{
    /* Fan out tick to all L1 tasks registered for tick synchronization. */
    l1_controller_signal_tick_tasks_from_isr();
}

/**
 * @brief Initializes the physical layer timer handler.
 *
 * Configures the LA9310 phy-timer peripheral and registers the tick ISR used
 * to drive time-sensitive L1 tasks. Must be called before any tick-driven
 * tasks are created. Placeholder — full timer configuration to be added
 * when the phy-timer data path is defined.
 */
void l1_controller_phytimer_init( uint8_t core_num )
{
    UNUSED( core_num );

    vPhyTimerPPSOUTSetPeriodMs( L1C_TIMER_TICK_MS );
    vPhyTimerPPSOUTConfig();
    return;
}