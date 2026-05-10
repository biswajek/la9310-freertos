/*
 * Mundo Sense
 */

#ifdef MS_TARGET_CAMERA
#include "ms_camera_global_typedef.h"
#include "ms_camera_globals.h"
#include "ms_camera_logger.h"
#else
#include "ms_controller_global_typedef.h"
#include "ms_controller_globals.h"
#include "ms_controller_logger.h"
#endif

volatile S_GlobalDebugInfo g_GlobalDebugInfo = {
    .g_Error_A = 0,
    .CoreNum   = 0,
};
