#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <signal.h>

#include "debug.h"

#define MAX_BUFFERS 8

static struct {
    int level;
    bool quiet;
    bool alwaysFlush;
    void *buffers[MAX_BUFFERS];
    debug_log_callback callbacks[MAX_BUFFERS];
} ctx;

int DEBUG_RegisterBuffer(void *buffer) {
    for (int i = 0; i < MAX_BUFFERS; i++)
        if (!ctx.buffers[i]) {
            ctx.buffers[i] = buffer;
            return 0;
        }

    return -1;
}

static void D_LogPrint(debug_log *log, void *buffer)
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
                    time, debug_log_esc_colors[log->level], debug_log_strings[log->level]);
        else
    #else
        if (buffer == stdout || buffer == stderr)
            fprintf(buffer, "%s %-5s ", time, levels[log->level]);
        else
    #endif
#endif
    
    fprintf(buffer, "%s %-5s ", time, debug_log_strings[log->level]);
    vfprintf(buffer, log->fmt, log->ap);
    fprintf(buffer, "\n");
    
}

void DEBUG_WriteLog(int level, const char *file, int line, const char *fmt, ...)
{
    if (level < ctx.level)
        return;

    debug_log l = {
            .fmt   = fmt,
            .file  = (strrchr(file, '/') ? strrchr(file, '/') + 1 : file),
            .line  = line,
            .level = level,
    };

    gettimeofday(&l.tp, 0);
    //TODO: STACK TRACE

    for (int i = 0; i < MAX_BUFFERS && ctx.callbacks[i]; i++)
    {
        va_start(l.ap, fmt);
        ctx.callbacks[i](l);
        va_end(l.ap);
    }

    va_start(l.ap, fmt);
    if (l.level == DEBUG_LOG_LEVEL_ERROR || l.level == DEBUG_LOG_LEVEL_FATAL || l.level == DEBUG_LOG_LEVEL_ASSERT)
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


    if (l.level == DEBUG_LOG_LEVEL_FATAL)
        exit(-1);
}

int DEBUG_RegisterCallback(debug_log_callback callback) 
{
    for (int i = 0; i < MAX_BUFFERS; i++)
    if (!ctx.callbacks[i]) {
        ctx.callbacks[i] = callback;
        return 0;
    }

    return -1;
}

void DEBUG_Flush()
{
    for (int i = 0; i < MAX_BUFFERS && ctx.buffers[i]; i++)
    {
        fflush(ctx.buffers[i]);
    }
}