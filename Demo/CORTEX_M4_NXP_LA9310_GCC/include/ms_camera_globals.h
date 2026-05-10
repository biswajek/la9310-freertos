/*
 * Mundo Sense
 */

#ifndef __CAMERA_GLOBALS_H
#define __CAMERA_GLOBALS_H

#include "ms_camera_global_typedef.h"

/* Small host-visible trace scratchpad in TCML for camera firmware.
 * Camera uses 0x1F81F000; controller uses 0x1F81E000 to avoid overlap. */
#define CAM_TRACE_WORD_COUNT   32U

static inline void cam_trace_write( uint32_t index, uint32_t value )
{
#if defined( TEST_L1C_TASKS ) || defined( L1C_ENABLE_TRACE )
    volatile uint32_t *trace_base = ( volatile uint32_t * ) 0x1F81F000u;

    if( index < CAM_TRACE_WORD_COUNT )
    {
        trace_base[index] = value;
    }
#else
    (void) index;
    (void) value;
#endif
}

extern volatile S_GlobalDebugInfo g_GlobalDebugInfo;

#endif /* __CAMERA_GLOBALS_H */
