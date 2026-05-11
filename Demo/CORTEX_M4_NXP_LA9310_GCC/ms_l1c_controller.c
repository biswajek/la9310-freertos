/*
 * Mundo Sense
 */

#include <stdio.h>
#include "pal_types.h"
#include "pal_sem.h"
#include "pal_thread.h"

#ifdef MS_TARGET_CAMERA
#include "ms_camera_logger.h"
#include "ms_camera_global_typedef.h"
#include "ms_camera_globals.h"
#include "ms_camera_l1c_controller.h"
#include "ms_camera_l1c_vspa_in.h"
#include "ms_camera_l1c_vspa_out.h"
#include "ms_camera_l1c_receiver.h"
#include "ms_camera_l1c_transmitter.h"
#include "ms_camera_l1c_modem_mgr.h"
#define L1_MGR_TASK_ID      L1_CAMERA_MGR_TASK
#define L1_MGR_TASK_NAME    "cameraMgrTask"
#define L1_MGR_TASK_FUNC    camera_mgr_task
#define L1_MGR_TICK_TYPE    TICK_DISABLE
#define l1_target_vspa_in_init      l1_camera_vspa_in_init
#define l1_target_vspa_out_init     l1_camera_vspa_out_init
#define l1_target_mgr_init          l1_camera_mgr_init
#define l1_target_receiver_init     l1_camera_receiver_init
#define l1_target_transmitter_init  l1_camera_transmitter_init
#define l1_target_phytimer_init     l1_camera_phytimer_init
#define l1_target_rf_manager_init   l1_camera_rf_manager_init
#else
#include "ms_controller_logger.h"
#include "ms_controller_global_typedef.h"
#include "ms_controller_globals.h"
#include "ms_controller_l1c_controller.h"
#include "ms_controller_l1c_vspa_in.h"
#include "ms_controller_l1c_vspa_out.h"
#include "ms_controller_l1c_receiver.h"
#include "ms_controller_l1c_transmitter.h"
#include "ms_controller_l1c_modem_mgr.h"
#define L1_MGR_TASK_ID      L1_MODEM_MGR_TASK
#define L1_MGR_TASK_NAME    "modemMgrTask"
#define L1_MGR_TASK_FUNC    modem_mgr_task
#define L1_MGR_TICK_TYPE    TICK_ENABLE
#define l1_target_vspa_in_init      l1_controller_vspa_in_init
#define l1_target_vspa_out_init     l1_controller_vspa_out_init
#define l1_target_mgr_init          l1_controller_modem_mgr_init
#define l1_target_receiver_init     l1_controller_receiver_init
#define l1_target_transmitter_init  l1_controller_transmitter_init
#define l1_target_phytimer_init     l1_controller_phytimer_init
#define l1_target_rf_manager_init   l1_controller_rf_manager_init
#endif

#ifdef TEST_L1C_TASKS
#define L1C_TASK_STACK_SIZE_BYTES (1024U * 2U)
#else
#define L1C_TASK_STACK_SIZE_BYTES (1024U * 4U)
#endif

task_map_t   tasks_map;
Sem_Handle_t tick_semaphores[MAX_TICK_TASKS_PER_CORE];
uint8_t      tick_tasks_count;

static void l1_create_task( uint32_t core_id, task_id_t task_id,
                             const char *const task_name, tick_type_t tick_en,
                             unsigned long task_priority, Thread_Callback_t pxTaskCode )
{
    Error_t         ret = Success;
    char            tickSemName[32];
    Thread_Handle_t threadHandle = 0U;

    tasks_map.tasks[task_id].core_id = core_id;
    tasks_map.tasks[task_id].task_id = task_id;
    tasks_map.tasks[task_id].tick    = tick_en;

    Thread_Attributes_t tAttr = {
        .stackSize   = L1C_TASK_STACK_SIZE_BYTES,
        .priority    = task_priority,
        .schedPolicy = E_SCHED_DEFAULT,
    };

    if( core_id == g_GlobalDebugInfo.CoreNum )
    {
        if( tick_en )
        {
            if( tick_tasks_count == MAX_TICK_TASKS_PER_CORE )
            {
                log_err( "[L1_CTRL] Reached max tick-driven tasks\r\n" );
                return;
            }

            (void) snprintf( tickSemName, sizeof( tickSemName ),
                             "TickSemaphore%u", (unsigned int) task_id );
            ret = pal_sem_create( &tick_semaphores[tick_tasks_count], tickSemName,
                                  E_SEM_BINARY_ISR, E_SEM_EMPTY );
            if( ret != Success )
                log_err( "[L1_CTRL] Could not create tick semaphore for task %s\r\n", task_name );

            tasks_map.tasks[task_id].tick_sem_idx = tick_tasks_count;
            tick_tasks_count++;
        }

        ret = pal_thread_create( &threadHandle, task_name,
                                 (Thread_Callback_t) pxTaskCode, tAttr, NULL );
        if( ret != Success )
            log_err( "[L1_CTRL] Task creation failure for %s (ret=%d)\r\n", task_name, ret );
        else
            log_info( "[L1_CTRL] Created task %s with id %d\r\n", task_name, task_id );
    }
}

/* -----------------------------------------------------------------------
 * Tick control — shared implementation, target-agnostic function names
 * declared in the active target's l1c_controller.h.
 * ----------------------------------------------------------------------- */

#ifdef MS_TARGET_CAMERA
Error_t l1_camera_wait_on_tick( task_id_t task_id, uint32_t timeout_ms )
#else
Error_t l1_controller_wait_on_tick( task_id_t task_id, uint32_t timeout_ms )
#endif
{
    if( task_id >= L1_TASK_MAX_ID )
        return Failure;

    if( tasks_map.tasks[ task_id ].tick != TICK_ENABLE )
        return Failure;

    if( tasks_map.tasks[ task_id ].tick_sem_idx >= tick_tasks_count )
        return Failure;

    return pal_sem_lock( tick_semaphores[ tasks_map.tasks[ task_id ].tick_sem_idx ], timeout_ms );
}

#ifdef MS_TARGET_CAMERA
void l1_camera_signal_tick_tasks_from_isr( void )
#else
void l1_controller_signal_tick_tasks_from_isr( void )
#endif
{
    uint8_t i;

    for( i = 0; i < tick_tasks_count; i++ )
        (void) pal_sem_release( tick_semaphores[ i ] );
}

/* -----------------------------------------------------------------------
 * Task and queue init — calls target-specific subsystem inits via macros.
 * ----------------------------------------------------------------------- */

#ifdef MS_TARGET_CAMERA
void l1_camera_tasks_create( void )
#else
void l1_controller_tasks_create( void )
#endif
{
    l1_create_task( g_GlobalDebugInfo.CoreNum, L1_MGR_TASK_ID,
                    L1_MGR_TASK_NAME, L1_MGR_TICK_TYPE, 1, L1_MGR_TASK_FUNC );
    l1_create_task( g_GlobalDebugInfo.CoreNum, L1_VSPA_IN_TASK,
                    "vspaInTask", TICK_DISABLE, 1, vspa_in_task );
    l1_create_task( g_GlobalDebugInfo.CoreNum, L1_VSPA_OUT_TASK,
                    "vspaOutTask", TICK_DISABLE, 1, vspa_out_task );
    l1_create_task( g_GlobalDebugInfo.CoreNum, L1_RX_TASK,
                    "receiverTask", TICK_DISABLE, 1, receiver_task );
    l1_create_task( g_GlobalDebugInfo.CoreNum, L1_TX_TASK,
                    "transmitterTask", TICK_DISABLE, 1, transmitter_task );
}

#ifdef MS_TARGET_CAMERA
void l1_camera_queues_init( void )
#else
void l1_controller_queues_init( void )
#endif
{
    l1_target_vspa_in_init( g_GlobalDebugInfo.CoreNum );
    l1_target_vspa_out_init( g_GlobalDebugInfo.CoreNum );
    l1_target_mgr_init( g_GlobalDebugInfo.CoreNum );
    l1_target_receiver_init( g_GlobalDebugInfo.CoreNum );
    l1_target_transmitter_init( g_GlobalDebugInfo.CoreNum );
}

#ifdef MS_TARGET_CAMERA
void l1_camera_init( void )
#else
void l1_controller_init( void )
#endif
{
    l1_target_phytimer_init( g_GlobalDebugInfo.CoreNum );
    l1_target_rf_manager_init( g_GlobalDebugInfo.CoreNum );
#ifdef MS_TARGET_CAMERA
    l1_camera_queues_init();
    l1_camera_tasks_create();
#else
    l1_controller_queues_init();
    l1_controller_tasks_create();
#endif
}
