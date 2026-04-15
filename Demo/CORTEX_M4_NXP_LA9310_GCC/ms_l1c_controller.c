/*
 * Mundo Sense
 */

/*------------------------------------------
                INCLUDES
--------------------------------------------*/
#include <stdio.h>
#include "pal_types.h"
#include "pal_sem.h"
#include "pal_thread.h"

#include "ms_logger.h"
#include "ms_l1c_controller.h"
#include "ms_global_typedef.h"
#include "ms_globals.h"


/*------------------------------------------
                VARIABLES
--------------------------------------------*/
/* Task database */
task_map_t        tasks_map;

/* Tick control */
Sem_Handle_t tick_semaphores[MAX_TICK_TASKS_PER_CORE];

char* defTickSemName = "TickSemaphore"; 
uint8_t           tick_tasks_count; 
/*------------------------------------------
                FUNCTIONS
--------------------------------------------*/

/**
 * @brief Creates an L1 task and registers it in the task map.
 *
 * If the task is tick-driven, a binary semaphore is allocated and associated
 * with it before the underlying PAL thread is created. Only tasks assigned to
 * the current core (matched via g_GlobalDebugInfo.CoreNum) are actually spawned.
 *
 * @param core_id       Core on which the task should run.
 * @param task_id       Unique task identifier (index into tasks_map).
 * @param task_name     Human-readable name passed to the scheduler.
 * @param tick_en       TICK_ENABLE to attach a tick semaphore, TICK_DISABLE otherwise.
 * @param task_priority FreeRTOS priority for the new thread.
 * @param pxTaskCode    Entry-point function for the task.
 */
void l1_create_task(uint32_t core_id, task_id_t task_id, const char *const task_name, tick_type_t tick_en, unsigned long task_priority, Thread_Callback_t pxTaskCode)
{
    Error_t ret = Success;
    char tempStr[25];
    
    tasks_map.tasks[task_id].core_id = core_id;
    tasks_map.tasks[task_id].task_id = task_id;
    tasks_map.tasks[task_id].tick    = tick_en;

    Thread_Attributes_t tAttr = {
        //.stackSize = configMINIMAL_STACK_SIZE * 4,
        .stackSize = 1024 * 4,
        .priority = task_priority,
        .schedPolicy = E_SCHED_DEFAULT,
    };
    
    if (core_id == g_GlobalDebugInfo.CoreNum )
    {
        if (tick_en)
        {
            if (tick_tasks_count == MAX_TICK_TASKS_PER_CORE)
            {
                log_err("[L1_CTRL] Reached max number of tasks that can receive tick. Cannot create more tick driven tasks\r\n");
                return;
            }

            sprintf(tempStr, "%u", task_id);
            defTickSemName = strcat( defTickSemName, tempStr );
            ret = pal_sem_create(&tick_semaphores[tick_tasks_count], defTickSemName, E_SEM_BINARY, E_SEM_EMPTY);
            if( ret != Success)    
                log_err("[L1_CTRL] Could not create tick semaphore for task %s\r\n", task_name);

            tasks_map.tasks[task_id].tick_sem_idx = tick_tasks_count;

            tick_tasks_count++;
        }

        ret = pal_thread_create( (Thread_Handle_t*)&tasks_map.tasks[task_id].task_id, task_name,
                                    (Thread_Callback_t) pxTaskCode, tAttr, NULL);
        if (ret != Success)
            log_err("[L1_CTRL] Task Creation Failure \r\n");
        else                                       
            log_info("[L1_CTRL] Created task %s with id %d\r\n", task_name, task_id);
    }
}



/**
 * @brief Creates all L1 controller subsystem tasks.
 *
 * Calls each subsystem init function in dependency order: phy-timer first,
 * then host I/O, modem manager, RF manager, receiver, and transmitter.
 * Must be called once during system startup before the FreeRTOS scheduler starts.
 */
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

/**
 * @brief Initializes inter-task message queues for the L1 controller.
 *
 * Placeholder for queue creation between L1 subsystem tasks (host I/O,
 * modem manager, RF manager, receiver, transmitter). To be implemented
 * when inter-task data paths are defined.
 */
void l1_controller_queues_init( ){

    return;

}



