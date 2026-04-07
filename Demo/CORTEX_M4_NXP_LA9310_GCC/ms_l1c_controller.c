/*
 * Mundo Sense
 */

/*------------------------------------------
                INCLUDES
--------------------------------------------*/
#include "ms_l1c_controller.h"

/*------------------------------------------
                PROTOTYPES
--------------------------------------------*/

/*------------------------------------------
                VARIABLES
--------------------------------------------*/

/*------------------------------------------
                FUNCTIONS
--------------------------------------------*/

void l1_controller_tasks_create( ){
    
    l1_controller_phytimer_init();
    l1_controller_host_in_init();
    l1_controller_host_out_init();
    l1_controller_modem_mgr_init();
    l1_controller_rf_manager_init();
    l1_controller_receiver_init();
    l1_controller_transmitter_init();
    
    return;
} 

void l1_controller_queues_init( ){

    return;

}



