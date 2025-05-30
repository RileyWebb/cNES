#include <stdlib.h>
#include <sys/time.h>
#include <stdarg.h>

#define DEBUG_USE_COLOR
//#define DEBUG_ASSERT_EXITS

#define DEBUG_INFO(...)                                                        \
	DEBUG_WriteLog(DEBUG_LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG_WARN(...)                                                        \
	DEBUG_WriteLog(DEBUG_LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG_ERROR(...)                                                       \
	DEBUG_WriteLog(DEBUG_LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG_FATAL(...)                                                       \
	DEBUG_WriteLog(DEBUG_LOG_LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#ifndef NDEBUG
#define DEBUG
#endif

#ifdef DEBUG
#define DEBUG_DEBUG(...)                                                       \
	DEBUG_WriteLog(DEBUG_LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)

#ifdef DEBUG_ASSERT_EXITS
#define DEBUG_ASSERT(x)                                                        \
	if (!x) {                                                                  \
	DEBUG_WriteLog(DEBUG_LOG_LEVEL_ASSERT, __FILE__, __LINE__,                 \
					"ASSERTION FAILED: %s", #x);                               \
    debug_flush_buffers();                                                     \
    exit(-1);                                                                  \
    }
#else
#define DEBUG_ASSERT(x)                                                        \
    if (!x)                                                                    \
	DEBUG_WriteLog(DEBUG_LOG_LEVEL_ASSERT, __FILE__, __LINE__,                 \
					"ASSERTION FAILED: %s", #x)
#endif

#define DEBUG_TRACE()                                                          \
	DEBUG_WriteLog(DEBUG_LOG_LEVEL_TRACE, __FILE__, __LINE__,                  \
					"FUNCTION: %s()", __FUNCTION__)
#ifdef STACKTRACE

#endif
#else
#define DEBUG_DEBUG(...)
#define DEBUG_ASSERT(x)
#define DEBUG_TRACE()
#endif

#define DEBUG_LEVEL_COUNT 7

typedef enum DEBUG_LOG_LEVEL {
	DEBUG_LOG_LEVEL_TRACE = 0,
	DEBUG_LOG_LEVEL_DEBUG = 1,
	DEBUG_LOG_LEVEL_INFO = 2,
	DEBUG_LOG_LEVEL_WARN = 3,
	DEBUG_LOG_LEVEL_ERROR = 4,
	DEBUG_LOG_LEVEL_FATAL = 5,
	DEBUG_LOG_LEVEL_ASSERT = 6
} DEBUG_LOG_LEVEL;

typedef struct
{
    DEBUG_LOG_LEVEL level;
    va_list ap;
    const char *fmt;
    const char *file;
    //struct tm *time;
    struct timeval tp;
    int line;
} debug_log;

static const char *debug_log_strings[] = {
	"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "ASSRT"
};

static const char *debug_log_esc_colors[] = {
	"\x1b[95m", "\x1b[36m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[35m", "\x1b[0;90m",
};

typedef void (*debug_log_callback)(debug_log);

void DEBUG_WriteLog(int level, const char *file, int line, const char *fmt, ...);
int DEBUG_RegisterBuffer(void *buffer);
int DEBUG_RegisterCallback(debug_log_callback callback);
void DEBUG_Flush();
