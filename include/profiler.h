#ifndef PROFILER_H
#define PROFILER_H

#include <stdint.h>
#include <stdbool.h>

#define PROFILER_MAX_SECTIONS 64
#define PROFILER_SECTION_NAME_LEN 32
#define PROFILER_HISTORY_SIZE 128
#define PROFILER_MAX_FLAME_ITEMS 256
#define PROFILER_MAX_STACK_DEPTH 16

typedef struct {
    char name[PROFILER_SECTION_NAME_LEN];
    double start_time_ms;
    double duration_ms;
    int depth;
} FlameGraphItem;

typedef struct {
    char name[PROFILER_SECTION_NAME_LEN];
    
    // Per-call timing (individual measurements)
    double times[PROFILER_HISTORY_SIZE];
    int history_idx;
    double current_time_ms;
    double avg_time_ms;
    double max_time_ms;
    
    // Per-frame aggregates
    uint64_t total_ticks_this_frame;
    int call_count_this_frame;
    double total_time_this_frame_ms;
    
    // Internal state
    uint64_t start_ticks;
    bool active;
    int depth;
} ProfilerSection;

typedef struct {
    // Timing
    uint64_t perf_freq;
    uint64_t frame_start_ticks;
    
    // Frame statistics
    double frame_times[PROFILER_HISTORY_SIZE];
    int frame_history_idx;
    double current_frame_time_ms;
    double avg_frame_time_ms;
    double max_frame_time_ms;
    float current_fps;
    
    // Sections
    ProfilerSection sections[PROFILER_MAX_SECTIONS];
    int num_sections;
    
    // Call stack for hierarchy
    int call_stack[PROFILER_MAX_STACK_DEPTH];
    int stack_depth;
    
    // Flame graph data
    FlameGraphItem flame_items[PROFILER_MAX_FLAME_ITEMS];
    int flame_item_count;
} Profiler;

// Lifecycle
void Profiler_Init(void);
void Profiler_Shutdown(void);
void Profiler_Enable(bool enable);
bool Profiler_IsEnabled(void);
const Profiler* Profiler_GetInstance(void);

// Frame timing
void Profiler_BeginFrame(void);
void Profiler_EndFrame(void);

// Section management
int Profiler_CreateSection(const char* name);
int Profiler_BeginSection(const char* name);     // Returns section ID
void Profiler_BeginSectionByID(int section_id);
void Profiler_EndSection(int section_id);

// Convenience macros
#define PROFILER_SCOPE(name) \
    for(int _prof_id = Profiler_BeginSection(name), _prof_once = 1; _prof_once; _prof_once = 0, Profiler_EndSection(_prof_id))

// Getters
float Profiler_GetFPS(void);
double Profiler_GetFrameTimeMS(void);

#endif // PROFILER_H
