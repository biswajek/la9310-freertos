/*
 * Mundo Sense
 */

/*------------------------------------------
                INCLUDES
--------------------------------------------*/


/*------------------------------------------
                PROTOTYPES
--------------------------------------------*/

/*------------------------------------------
                VARIABLES
--------------------------------------------*/

/*------------------------------------------
                FUNCTIONS
--------------------------------------------*/

/**
 * @brief Initializes the physical layer timer handler.
 *
 * Configures the LA9310 phy-timer peripheral and registers the tick ISR used
 * to drive time-sensitive L1 tasks. Must be called before any tick-driven
 * tasks are created. Placeholder — full timer configuration to be added
 * when the phy-timer data path is defined.
 */
void l1_controller_phytimer_init(){
    return;
}