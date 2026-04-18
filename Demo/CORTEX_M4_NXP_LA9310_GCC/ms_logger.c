/*
 * Mundo Sense
 */

/*------------------------------------------
            INCLUDES
--------------------------------------------*/
#include "debug_console.h"
#include "ms_logger.h"

/*------------------------------------------
            VARIABLES
--------------------------------------------*/

/* Default level: INFO — errors, warnings, and operational messages visible */
ms_log_level_t g_ms_log_level = MS_LOG_LEVEL_INFO;

/*------------------------------------------
            FUNCTIONS
--------------------------------------------*/

/**
 * @brief Initializes the MS logger and sets the active log level.
 * @param level Initial log level. Messages above this level are suppressed.
 */
void ms_log_init( ms_log_level_t level )
{
    g_ms_log_level = level;
}

/**
 * @brief Sets the active log level at runtime.
 * @param level New log level.
 */
void ms_log_set_level( ms_log_level_t level )
{
    g_ms_log_level = level;
}

/**
 * @brief Returns the current active log level.
 * @return Current ms_log_level_t value.
 */
ms_log_level_t ms_log_get_level( void )
{
    return g_ms_log_level;
}
