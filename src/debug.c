#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <sys/time.h>
#include <signal.h>

#include "debug.h"

#define MAX_BUFFERS 8

typedef struct
{
    int level;
    va_list ap;
    const char *fmt;
    const char *file;
    //struct tm *time;
    struct timeval tp;
    int line;
} d_log;

static struct {
    int level;
    bool quiet;
    bool alwaysFlush;
    void *buffers[MAX_BUFFERS];
} ctx;

static const char *levels[] = {
        "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "ASSRT", "CONN"
};

#ifdef DEBUG_USE_COLOR
static const char *colors[] = {
        "\x1b[95m", "\x1b[36m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[35m", "\x1b[0;90m",
};
#endif

int D_LogRegister(void *buffer) {
    for (int i = 0; i < MAX_BUFFERS; i++)
        if (!ctx.buffers[i]) {
            ctx.buffers[i] = buffer;
            return 0;
        }

    return -1;
}

static void D_LogPrint(d_log *log, void *buffer)
{
    char time[16];
    time_t curtime = log->tp.tv_sec;
    struct tm *t = localtime(&curtime);
    sprintf(time, "%02d:%02d:%02d.%06d", t->tm_hour, t->tm_min, t->tm_sec, log->tp.tv_usec);

#ifdef DEBUG
    #ifdef DEBUG_USE_COLOR
        if (buffer == stdout || buffer == stderr)
            fprintf(buffer, "%s %s%-5s\x1b[0m \x1b[90m%s:%d\x1b[0m ",
                    time, colors[log->level], levels[log->level], log->file, log->line);
        else
    #else
        if (buffer == stdout || buffer == stderr)
            fprintf(buffer, "%s %-5s %s:%d ", time, levels[log->level], log->file, log->line);
        else
    #endif
#else
    #ifdef DEBUG_USE_COLOR
        if (buffer == stdout || buffer == stderr)
            fprintf(buffer, "%s %s%-5s\x1b[0m ",
                    time, colors[log->level], levels[log->level]);
        else
    #else
        if (buffer == stdout || buffer == stderr)
            fprintf(buffer, "%s %-5s ", time, levels[log->level]);
        else
    #endif
#endif



    fprintf(buffer, "%s %-5s ", time, levels[log->level]);
    vfprintf(buffer, log->fmt, log->ap);
    fprintf(buffer, "\n");
}

void D_LogWrite(int level, const char *file, int line, const char *fmt, ...)
{
    if (level < ctx.level)
        return;

    d_log l = {
            .fmt   = fmt,
            .file  = (strrchr(file, '/') ? strrchr(file, '/') + 1 : file),
            .line  = line,
            .level = level,
    };

    gettimeofday(&l.tp, 0);
    //TODO: STACK TRACE

    va_start(l.ap, fmt);
    if (l.level == DEBUG_LOG_TYPE_ERROR || l.level == DEBUG_LOG_TYPE_FATAL || l.level == DEBUG_LOG_TYPE_ASSERT)
        D_LogPrint(&l, stderr);
    else
        D_LogPrint(&l, stdout);
    va_end(l.ap);

    for (int i = 0; i < MAX_BUFFERS && ctx.buffers[i]; i++)
    {
        va_start(l.ap, fmt);
        D_LogPrint(&l, ctx.buffers[i]);
        va_end(l.ap);
    }

    if (l.level == DEBUG_LOG_TYPE_FATAL)
        exit(-1);
}

void D_LogFlush()
{
    for (int i = 0; i < MAX_BUFFERS && ctx.buffers[i]; i++)
    {
        fflush(ctx.buffers[i]);
    }
}