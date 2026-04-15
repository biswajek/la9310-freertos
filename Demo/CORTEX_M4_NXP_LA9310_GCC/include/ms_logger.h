/*
 * Mundo Sense
 */

#ifndef MS_LOGGER_H
#define MS_LOGGER_H

/* Forward declaration — satisfied by debug_console at link time */
extern int debug_printf( const char * fmt_s, ... );

#ifndef PRINTF
    #define PRINTF    debug_printf
#endif

/*------------------------------------------
            LOG LEVELS
--------------------------------------------*/
typedef enum
{
    MS_LOG_LEVEL_NONE = 0, /* No output                     */
    MS_LOG_LEVEL_ERR  = 1, /* Errors that need attention    */
    MS_LOG_LEVEL_WARN = 2, /* Unexpected but recoverable    */
    MS_LOG_LEVEL_INFO = 3, /* Normal operational messages   */
    MS_LOG_LEVEL_DBG  = 4, /* Verbose debug output          */
} ms_log_level_t;

/*------------------------------------------
            API
--------------------------------------------*/

/**
 * @brief Initializes the MS logger and sets the active log level.
 * @param level Initial log level. Messages above this level are suppressed.
 */
void ms_log_init( ms_log_level_t level );

/**
 * @brief Sets the active log level at runtime.
 * @param level New log level.
 */
void ms_log_set_level( ms_log_level_t level );

/**
 * @brief Returns the current active log level.
 * @return Current ms_log_level_t value.
 */
ms_log_level_t ms_log_get_level( void );

/* Internal: current active level — evaluated in each macro */
extern ms_log_level_t g_ms_log_level;

/*------------------------------------------
            LOG MACROS
  Redefine log_* for the MS layer so they
  use the MS level gate, not the HIF regs.
--------------------------------------------*/
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

#endif /* MS_LOGGER_H */
