/*
 * Mundo Sense
 */

#include "debug_console.h"
#ifdef MS_TARGET_CAMERA
#include "ms_camera_logger.h"
#else
#include "ms_controller_logger.h"
#endif

ms_log_level_t g_ms_log_level = MS_LOG_LEVEL_INFO;

void ms_log_init( ms_log_level_t level )
{
    g_ms_log_level = level;
}

void ms_log_set_level( ms_log_level_t level )
{
    g_ms_log_level = level;
}

ms_log_level_t ms_log_get_level( void )
{
    return g_ms_log_level;
}
