/**
 *  A slightly modified example of what we've already seen in dlintegrate.c
 *
 *  Logging here is designed to be used as header without the .c source file implementation.
 *  This is done intentionally to avoid the need of linking / not linking with corresponding
 *  object file.
 *  Thus, if DEBUG is not set or set to 0, all the macros expand to empty definitions and the
 *  resulting compiled code stays clean.
 *  If DEBUG is set to 1 or higher, its value represents logging level. Therefore all logging
 *  mesages with lower log level are not being output and are (most always) thrown out
 *  during optimization stages.
 *
 *  Log message format is specified in LOG_FORMAT macro definition below
 *
 *  Colored logging output is enabled by default and helps to make logging messages more
 *  visible and more distinguishable from each other. To disable, set LOG_USE_COLORS
 *  definition to 0
 *
 *  log_info, log_warn, log_error and log_crit are aliases to log(LOG_INFO, ...) etc.
 */

#if !defined(DEBUG) || !DEBUG
#define log(lvl, fmt, ...)
#define log_dbg(fmt, ...)
#define log_info(fmt, ...)
#define log_warn(fmt, ...)
#define log_err(fmt, ...)
#define log_crit(fmt, ...)
#else
#include <stdio.h>

// Log message format is specified here. All overcomplicated handling below
// is done for the sole purpose of this line
#define LOG_FORMAT(fmt) (_LOG_COLOR ">> %s [L%ld @ %s]: " fmt _LOG_NOCOLOR "\n")

enum _log_level {
    LOG_CRIT = 1,
    LOG_ERR  = 2,
    LOG_WARN = 3,
    LOG_INFO = 4,
    LOG_DEBUG = 5       /* Insanely verbose logging */
};

#define _LOG_NAME(lvl) (lvl == LOG_DEBUG ? "DEBUG" : \
                        lvl == LOG_INFO ? "INFO" : \
                        lvl == LOG_WARN ? "WARN" : \
                        lvl == LOG_ERR ? "ERROR" : \
                        lvl == LOG_CRIT ? "CRITICAL" : \
                        "<UNKNOWN>")

#if !defined(LOG_USE_COLORS)
#define LOG_USE_COLORS 1
#endif

#if !LOG_USE_COLORS
#define _LOG_COLOR ""
#define _LOG_NOCOLOR ""
#define _log_getcolor(lvl)
#define _LOG_CARGS(lvl, ...) __VA_ARGS__

#else
#define _LOG_COLOR "\033[0;%dm"
#define _LOG_NOCOLOR "\033[0m"

enum _log_colors {
    _LOG_COLOR_DEBUG = 32,       // DEBUG: green
    _LOG_COLOR_INFO  = 94,       // INFO: light blue
    _LOG_COLOR_WARN  = 33,       // WARN: yellow
    _LOG_COLOR_ERR   = 31,       // ERR: red
    _LOG_COLOR_CRIT  = 91        // CRIT: light red
};

inline int _log_getcolor(enum _log_level lvl)
{
    switch (lvl) {
    case LOG_DEBUG: return _LOG_COLOR_DEBUG;
    case LOG_INFO:  return _LOG_COLOR_INFO;
    case LOG_WARN:  return _LOG_COLOR_WARN;
    case LOG_ERR:   return _LOG_COLOR_ERR;
    case LOG_CRIT:  return _LOG_COLOR_CRIT;
    default:        return 0;
    }
}

// as color is being output first, this prepends it to the other arguments
#define _LOG_CARGS(lvl, ...) _log_getcolor(lvl),##__VA_ARGS__
#endif

#define _log(lvl, msg, lineno, funcname, ...) do {                                                \
    if (lvl <= DEBUG) fprintf(stderr, LOG_FORMAT(msg), _LOG_CARGS(lvl, _LOG_NAME((enum _log_level)lvl),             \
            (long)lineno, funcname,##__VA_ARGS__));                                               \
    } while(0)

#define log(lvl, fmt, ...) _log(lvl, fmt, __LINE__, __func__,##__VA_ARGS__)
#define log_dbg(fmt, ...)  _log(LOG_DEBUG, fmt, __LINE__, __func__,##__VA_ARGS__)
#define log_info(fmt, ...) _log(LOG_INFO, fmt, __LINE__, __func__,##__VA_ARGS__)
#define log_warn(fmt, ...) _log(LOG_WARN, fmt, __LINE__, __func__,##__VA_ARGS__)
#define log_err(fmt, ...)  _log(LOG_ERR, fmt, __LINE__, __func__,##__VA_ARGS__)
#define log_crit(fmt, ...) _log(LOG_CRIT, fmt, __LINE__, __func__,##__VA_ARGS__)
#endif
