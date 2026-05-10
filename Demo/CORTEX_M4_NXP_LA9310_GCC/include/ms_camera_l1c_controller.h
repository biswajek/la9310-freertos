/*
 * Mundo Sense
 */

#ifndef __CAMERA_L1CA_H
#define __CAMERA_L1CA_H

#include <stdint.h>
#include "ms_camera_global_typedef.h"
#include "pal_types.h"

#define MAX_TASKS_ALLOWED       10
#define MAX_TICK_TASKS_PER_CORE 4

void l1_camera_tasks_create( void );
void l1_camera_queues_init( void );
void l1_camera_init( void );
void l1_camera_phytimer_init( uint8_t core_num );
void l1_camera_vspa_in_init( uint8_t core_num );
void l1_camera_vspa_out_init( uint8_t core_num );
void l1_camera_mgr_init( uint8_t core_num );
void l1_camera_rf_manager_init( uint8_t core_num );
void l1_camera_receiver_init( uint8_t core_num );
void l1_camera_transmitter_init( uint8_t core_num );

typedef enum task_id_e
{
    L1_CAMERA_MGR_TASK,
    L1_PHYTIMER_HANDLER_TASK,
    L1_RX_TASK,
    L1_TX_TASK,
    L1_VSPA_IN_TASK,
    L1_VSPA_OUT_TASK,
    L1_RF_TIMER_TASK,
    L1_UNKNOWN_TASK,
    L1_ISR,
    L1_TASK_MAX_ID
} task_id_t;

typedef enum core_id_e
{
    L1_CORE_0,
    L1_CORE_1
} core_id_t;

typedef enum tick_type_e
{
    TICK_DISABLE,
    TICK_ENABLE,
} tick_type_t;

typedef struct task_desc_s
{
    uint32_t    core_id;
    task_id_t   task_id;
    tick_type_t tick;
    uint8_t     tick_sem_idx;
} task_desc_t;

typedef struct task_map_s
{
    task_desc_t tasks[L1_TASK_MAX_ID];
} task_map_t;

Error_t l1_camera_wait_on_tick( task_id_t task_id, uint32_t timeout_ms );
void    l1_camera_signal_tick_tasks_from_isr( void );

#endif /* __CAMERA_L1CA_H */
