/*
 * Mundo Sense
 */

 #ifndef __L1CA_H
#define __L1CA_H

/*------------------------------------------
                INCLUDES
--------------------------------------------*/

/*------------------------------------------
                PROTOTYPES
--------------------------------------------*/
void l1_controller_tasks_create( void ); 
void l1_controller_queues_init( void );
void l1_controller_phytimer_init();
void l1_controller_host_in_init();
void l1_controller_host_out_init();
void l1_controller_modem_mgr_init();
void l1_controller_rf_manager_init();
void l1_controller_receiver_init();
void l1_controller_transmitter_init();
/*------------------------------------------
                VARIABLES
--------------------------------------------*/
#endif /*__L1CA_H */

