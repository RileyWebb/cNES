#ifndef PROFILER_H
#define PROFILER_H

#include <stdint.h>
#include <stdbool.h>

#define PROFILER_MAX_SECTIONS 64
#define PROFILER_SECTION_NAME_LEN 64
#define PROFILER_HISTORY_SIZE 120

typedef struct ProfilerSection ProfilerSection;
typedef struct Profiler Profiler;

typedef struct ProfilerSection {
    char name[PROFILER_SECTION_NAME_LEN];
    uint64_t start_ticks_for_current_call; // Used by write_buffer's sections
    uint64_t total_ticks_this_frame;       // Holds accumulated ticks for a frame
    int call_count_this_frame;             // Holds accumulated calls for a frame
    double total_time_this_frame_ms;       // Calculated total time for a frame
} ProfilerSection;

typedef struct Profiler {
    // "Read" buffer: Data from the last completed frame, accessible via Profiler_GetInstance()
    ProfilerSection sections[PROFILER_MAX_SECTIONS];
    int num_sections; // Number of sections in the "read" buffer

    double current_frame_time_ms; // Time for the last completed frame
    float current_fps;             // FPS based on history, for the last completed frame period
    double avg_frame_time_ms;     // Average frame time from history
    double max_frame_time_ms;     // Max frame time from history

    // --- Internal "Write" buffer: Data for the current frame being processed ---
    ProfilerSection write_sections_buffer[PROFILER_MAX_SECTIONS];
    int num_write_sections;
    uint64_t current_processing_frame_start_ticks; // Start ticks for the frame currently being built

    // --- Common profiler state ---
    uint64_t perf_freq;
    double frame_times[PROFILER_HISTORY_SIZE]; // History buffer for frame time calculations
    int frame_history_idx;                     // Current index in the frame_times history buffer
} Profiler;

static Profiler g_profiler;
static bool g_profiler_enabled = true;

void Profiler_Init(void);
void Profiler_Shutdown(void);
Profiler* Profiler_GetInstance(void);
float Profiler_GetFPS(void);
void Profiler_BeginFrame(void);
int Profiler_CreateSection(const char* name);
int Profiler_BeginSection(const char* name);
void Profiler_BeginSectionByID(int section_id);
void Profiler_EndSection(int section_id);
void Profiler_EndFrame(void);
void Profiler_Enable(bool enable);
bool Profiler_IsEnabled(void);

#endif // PROFILER_H