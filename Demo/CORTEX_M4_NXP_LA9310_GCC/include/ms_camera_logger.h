/*
 * Mundo Sense
 */

#ifndef MS_CAM_LOGGER_H
#define MS_CAM_LOGGER_H

extern int debug_printf( const char *fmt_s, ... );

#ifndef PRINTF
    #define PRINTF    debug_printf
#endif

typedef enum
{
    MS_LOG_LEVEL_NONE = 0,
    MS_LOG_LEVEL_ERR  = 1,
    MS_LOG_LEVEL_WARN = 2,
    MS_LOG_LEVEL_INFO = 3,
    MS_LOG_LEVEL_DBG  = 4,
} ms_log_level_t;

void           ms_log_init( ms_log_level_t level );
void           ms_log_set_level( ms_log_level_t level );
ms_log_level_t ms_log_get_level( void );

extern ms_log_level_t g_ms_log_level;

#ifdef log_err
    #undef log_err
#endif
#ifdef log_warn
    #undef log_warn
#endif
#ifdef log_info
    #undef log_info
#endif
#ifdef log_dbg
    #undef log_dbg
#endif

#define log_err( fmt, ... ) \
    do { \
        if( g_ms_log_level >= MS_LOG_LEVEL_ERR ) \
            PRINTF( "[ERR]  " fmt, ##__VA_ARGS__ ); \
    } while( 0 )

#define log_warn( fmt, ... ) \
    do { \
        if( g_ms_log_level >= MS_LOG_LEVEL_WARN ) \
            PRINTF( "[WARN] " fmt, ##__VA_ARGS__ ); \
    } while( 0 )

#define log_info( fmt, ... ) \
    do { \
        if( g_ms_log_level >= MS_LOG_LEVEL_INFO ) \
            PRINTF( "[INFO] " fmt, ##__VA_ARGS__ ); \
    } while( 0 )

#define log_dbg( fmt, ... ) \
    do { \
        if( g_ms_log_level >= MS_LOG_LEVEL_DBG ) \
            PRINTF( "[DBG]  " fmt, ##__VA_ARGS__ ); \
    } while( 0 )

#endif /* MS_CAM_LOGGER_H */
