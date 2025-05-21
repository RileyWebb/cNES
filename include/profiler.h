#ifndef PROFILER_H
#define PROFILER_H

#include <stdint.h>
#include <stdbool.h>
#include <SDL3/SDL_timer.h> // For SDL_GetPerformanceCounter, SDL_GetPerformanceFrequency

#define PROFILER_HISTORY_SIZE 128
#define PROFILER_SECTION_NAME_LEN 64
#define PROFILER_MAX_SECTIONS 64
#define PROFILER_MAX_FLAME_GRAPH_ITEMS 512

typedef struct {
    char name[PROFILER_SECTION_NAME_LEN];
    uint64_t start_ticks;
    double current_time_ms;
    double avg_time_ms;
    double max_time_ms;
    double times[PROFILER_HISTORY_SIZE];
    int history_idx;
    bool active;
    int parent_id;
    int depth;
    double start_time_in_frame_ms;
} ProfilerSection;

typedef struct {
    char name[PROFILER_SECTION_NAME_LEN];
    double start_time_ms;
    double duration_ms;
    int depth;
} FlameGraphItem;

typedef struct Profiler {
    uint64_t perf_freq;
    uint64_t frame_start_ticks;
    double frame_times_ms[PROFILER_HISTORY_SIZE];
    int frame_history_idx;
    float current_fps;
    double current_frame_time_ms;
    double avg_frame_time_ms;
    double max_frame_time_ms;
    ProfilerSection sections[PROFILER_MAX_SECTIONS];
    int num_sections;
    int section_stack[PROFILER_MAX_SECTIONS];
    int section_stack_top;
    FlameGraphItem last_frame_flame_items[PROFILER_MAX_FLAME_GRAPH_ITEMS];
    int last_frame_flame_items_count;
    float current_cpu_utilization;
    float current_gpu_utilization;
} Profiler;

// Lifecycle and Control
void Profiler_Init(void);
void Profiler_Shutdown(void);
void Profiler_Enable(bool enable);
bool Profiler_IsEnabled(void);

// Frame and Section Timing
void Profiler_BeginFrame(void);
void Profiler_EndFrame(void);
int Profiler_BeginSection(const char* name);
void Profiler_EndSection(int section_id);

// Data Access
float Profiler_GetFPS(void);
double Profiler_GetFrameTimeMS(void);
const Profiler* Profiler_GetInstance(void); // To allow UI to read data

#endif // PROFILER_H
