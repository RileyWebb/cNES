#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <cimgui.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cNES/nes.h"
#include "frontend/frontend.h"
#include "frontend/frontend_internal.h"

static ImVec4 frontend_log_colors[] = {
    {0.7f, 0.7f, 0.7f, 1.0f}, // TRACE - Light Gray
    {0.0f, 1.0f, 1.0f, 1.0f}, // DEBUG - Cyan
    {0.0f, 0.8f, 0.0f, 1.0f}, // INFO - Green
    {1.0f, 1.0f, 0.0f, 1.0f}, // WARN - Yellow
    {1.0f, 0.0f, 0.0f, 1.0f}, // ERROR - Red
    {1.0f, 0.0f, 1.0f, 1.0f}, // FATAL - Magenta
    {1.0f, 0.5f, 0.0f, 1.0f}, // ASSRT - Orange
    {0.5f, 0.0f, 1.0f, 1.0f}  // CONN - Purple
};



void Frontend_LogWindow()
{
    if (!frontend_showLog)
        return;

    static int selected_log_level_filter = DEBUG_LOG_LEVEL_TRACE; // Show all by default

    if (igBegin("Log", &frontend_showLog, ImGuiWindowFlags_None))
    {
        if (igButton("Clear", (ImVec2){0, 0}))
        {
            for (size_t i = 0; i < frontend_log_count; i++)
            {
                if (frontend_log_buffer[i].formatted) {
                    free(frontend_log_buffer[i].formatted);
                    frontend_log_buffer[i].formatted = NULL;
                }
            }
            memset(frontend_log_buffer, 0, sizeof(frontend_debug_log) * frontend_log_count); // Clear the whole struct
            frontend_log_count = 0;
        }
        igSameLine(0, 10);

        // Dropdown for log level filter
        igSetNextItemWidth(150); // Adjust width as needed
        //const char* log_level_items[] = { "Trace", "Debug", "Info", "Warn", "Error", "Fatal", "Assert", "Conn" };
        if (igCombo_Str_arr("Min Level", &selected_log_level_filter, debug_log_strings, DEBUG_LEVEL_COUNT, 4))
        {
            // Optional: Action on change, e.g., force scroll to bottom or re-filter
        }

        igSeparator();
        igBeginChild_Str("LogScrollingRegion", (ImVec2){0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        for (size_t i = 0; i < frontend_log_count; i++)
        {
            if (frontend_log_buffer[i].log.level >= selected_log_level_filter)
            {
                time_t curtime = frontend_log_buffer[i].log.tp.tv_sec;
                struct tm *t = localtime(&curtime);
                // Check if t is NULL, which can happen with invalid time_t values
                if (t) {
                    igText("%02d:%02d:%02d.%06ld", t->tm_hour, t->tm_min, t->tm_sec, (long)frontend_log_buffer[i].log.tp.tv_usec);
                } else {
                    igText("--:--:--.------"); // Placeholder for invalid time
                }
                igSameLine(0, 5);
                igTextColored(frontend_log_colors[frontend_log_buffer[i].log.level],
                              "[%s]", debug_log_strings[frontend_log_buffer[i].log.level]);
                igSameLine(0, 5); // Add some spacing after the log level
                if (frontend_log_buffer[i].formatted) {
                    igTextUnformatted(frontend_log_buffer[i].formatted, NULL);
                } else {
                    igTextUnformatted("Error: Log message not formatted.", NULL);
                }
            }
        }

        if (igGetScrollY() >= igGetScrollMaxY()) // Scroll to bottom if at the end
            igSetScrollHereY(1.0f);
        igEndChild();
    }
    igEnd();
}

void Frontend_Log(debug_log log)
{
    //if (frontend_log_count > )
    frontend_log_buffer[frontend_log_count].log = log;
    frontend_log_buffer[frontend_log_count].formatted = (char *)malloc(1024); // Allocate memory for formatted string
    vsnprintf(frontend_log_buffer[frontend_log_count].formatted, 1024, log.fmt, log.ap);
    frontend_log_count++;
}



