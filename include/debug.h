#include <stdlib.h>

#define DEBUG_USE_COLOR
//#define DEBUG_ASSERT_EXITS

#define DEBUG_INFO(...)                                                        \
	D_LogWrite(DEBUG_LOG_TYPE_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG_WARN(...)                                                        \
	D_LogWrite(DEBUG_LOG_TYPE_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG_ERROR(...)                                                       \
	D_LogWrite(DEBUG_LOG_TYPE_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG_FATAL(...)                                                       \
	D_LogWrite(DEBUG_LOG_TYPE_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#ifndef NDEBUG
#define DEBUG
#endif

#ifdef DEBUG
#define DEBUG_DEBUG(...)                                                       \
	D_LogWrite(DEBUG_LOG_TYPE_DEBUG, __FILE__, __LINE__, __VA_ARGS__)

#ifdef DEBUG_ASSERT_EXITS
#define DEBUG_ASSERT(x)                                                        \
	if (!x) {                                                                  \
	D_LogWrite(DEBUG_LOG_TYPE_ASSERT, __FILE__, __LINE__,                 \
					"ASSERTION FAILED: %s", #x);                               \
    debug_flush_buffers();                                                     \
    exit(-1);                                                                  \
    }
#else
#define DEBUG_ASSERT(x)                                                        \
    if (!x)                                                                    \
	D_LogWrite(DEBUG_LOG_TYPE_ASSERT, __FILE__, __LINE__,                 \
					"ASSERTION FAILED: %s", #x)
#endif

#define DEBUG_TRACE()                                                          \
	D_LogWrite(DEBUG_LOG_TYPE_TRACE, __FILE__, __LINE__,                  \
					"FUNCTION: %s()", __FUNCTION__)
#ifdef STACKTRACE

#endif
#else
#define DEBUG_DEBUG(...)
#define DEBUG_ASSERT(x)
#define DEBUG_TRACE()
#endif

enum {
	DEBUG_LOG_TYPE_TRACE = 0,
	DEBUG_LOG_TYPE_DEBUG = 1,
	DEBUG_LOG_TYPE_INFO = 2,
	DEBUG_LOG_TYPE_WARN = 3,
	DEBUG_LOG_TYPE_ERROR = 4,
	DEBUG_LOG_TYPE_FATAL = 5,
	DEBUG_LOG_TYPE_ASSERT = 6,
	DEBUG_LOG_TYPE_CONSOLE = 7
};

void D_LogWrite(int level, const char *file, int line, const char *fmt, ...);
int D_LogRegister(void *buffer);
void D_LogFlush();
