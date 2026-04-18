/*
 * Mundo Sense
 */

 #ifndef __L1CA_H
#define __L1CA_H

/*------------------------------------------
                INCLUDES
--------------------------------------------*/
#include <stdint.h>
#include "ms_global_typedef.h"

/*------------------------------------------
                DEFINES
--------------------------------------------*/
#define MAX_TASKS_ALLOWED       10
#define MAX_TICK_TASKS_PER_CORE 4

/*------------------------------------------
                PROTOTYPES
--------------------------------------------*/
void l1_controller_tasks_create( void );
void l1_controller_queues_init( void );
void l1_controller_init( void );
void l1_controller_phytimer_init( uint8_t core_num );
void l1_controller_host_in_init( uint8_t core_num );
void l1_controller_host_out_init( uint8_t core_num );
void l1_controller_modem_mgr_init( uint8_t core_num );
void l1_controller_rf_manager_init( uint8_t core_num );
void l1_controller_receiver_init( uint8_t core_num );
void l1_controller_transmitter_init( uint8_t core_num );
/*------------------------------------------
                VARIABLES
--------------------------------------------*/
typedef enum task_id_e
{
    L1_MODEM_MGR_TASK,
    L1_PHYTIMER_HANDLER_TASK,
    L1_RX_TASK,
    L1_TX_TASK,
    L1_HOST_IN_TASK,
    L1_HOST_OUT_TASK,
    L1_RF_TIMER_TASK,
    L1_UNKNOWN_TASK,
    L1_ISR,
    L1_TASK_MAX_ID
} task_id_t;

typedef enum core_id_e
{
    L1_CORE_0,
    L1_CORE_1   
}core_id_t;

typedef enum tick_type_e
{
    TICK_DISABLE,
    TICK_ENABLE,
}tick_type_t;

typedef struct task_desc_s
{
    uint32_t     core_id;
    task_id_t    task_id;
    tick_type_t  tick;
    uint8_t      tick_sem_idx;
} task_desc_t;

typedef struct task_map_s
{
    task_desc_t tasks[L1_TASK_MAX_ID];
} task_map_t;




#endif /*__L1CA_H */

