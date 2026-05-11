#ifndef PROFILER_H
#define PROFILER_H

#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>

#define PROFILER_MAX_SECTIONS 64
#define PROFILER_SECTION_NAME_LEN 64
#define PROFILER_MAX_THREADS 16
#define PROFILER_MAX_EVENTS_PER_THREAD 4096
#define PROFILER_MAX_SCOPE_DEPTH 64
#define PROFILER_HISTORY_SIZE 120
#define PROFILER_THREAD_NAME_LEN 32

typedef struct ProfilerSection ProfilerSection;
typedef struct Profiler Profiler;
typedef struct ProfilerThreadFrame ProfilerThreadFrame;
typedef struct ProfilerFrameSnapshot ProfilerFrameSnapshot;
typedef struct ProfilerScopeEvent ProfilerScopeEvent;

typedef struct ProfilerScopeEvent {
    uint16_t section_id;
    uint16_t depth;
    uint16_t thread_slot;
    uint16_t flags;
    uint64_t start_ticks;
    uint64_t end_ticks;
} ProfilerScopeEvent;

typedef struct ProfilerThreadFrame {
    SDL_ThreadID thread_id;
    char name[PROFILER_THREAD_NAME_LEN];
    uint32_t event_count;
    ProfilerScopeEvent events[PROFILER_MAX_EVENTS_PER_THREAD];
} ProfilerThreadFrame;

typedef struct ProfilerFrameSnapshot {
    uint64_t frame_index;
    uint64_t start_ticks;
    uint64_t end_ticks;
    double duration_ms;
    uint32_t thread_count;
    ProfilerThreadFrame threads[PROFILER_MAX_THREADS];
} ProfilerFrameSnapshot;

typedef struct ProfilerSection {
    char name[PROFILER_SECTION_NAME_LEN];
    uint64_t total_ticks_this_frame;
    uint64_t max_ticks_this_frame;
    uint64_t last_ticks_this_frame;
    int call_count_this_frame;
    double total_time_this_frame_ms;
    double max_time_this_frame_ms;
    double last_time_this_frame_ms;
} ProfilerSection;

typedef struct Profiler {
    bool enabled;
    uint64_t perf_freq;
    uint64_t frame_counter;
    uint64_t current_frame_start_ticks;
    uint64_t current_frame_end_ticks;
    double current_frame_time_ms;
    float current_fps;
    double avg_frame_time_ms;
    double max_frame_time_ms;

    ProfilerSection sections[PROFILER_MAX_SECTIONS];
    int num_sections;

    ProfilerFrameSnapshot frame_storage[2];
    ProfilerFrameSnapshot *published_frame;
    ProfilerFrameSnapshot *write_frame;

    char thread_names[PROFILER_MAX_THREADS][PROFILER_THREAD_NAME_LEN];
    uint32_t thread_name_count;
    SDL_TLSID thread_tls;
    SDL_Mutex *mutex;
    double frame_times[PROFILER_HISTORY_SIZE]; // History buffer for frame time calculations
    int frame_history_idx;                     // Current index in the frame_times history buffer
} Profiler;

typedef struct ProfilerThreadState {
    uint64_t frame_index;
    uint32_t thread_slot;
    uint16_t stack_depth;
    uint32_t event_stack[PROFILER_MAX_SCOPE_DEPTH];
    int section_stack[PROFILER_MAX_SCOPE_DEPTH];
    char thread_name[PROFILER_THREAD_NAME_LEN];
} ProfilerThreadState;

extern Profiler g_profiler;

void Profiler_Init(void);
void Profiler_Shutdown(void);
Profiler* Profiler_GetInstance(void);
float Profiler_GetFPS(void);
double Profiler_GetFrameTimeMS(void);
void Profiler_BeginFrame(void);
int Profiler_CreateSection(const char* name);
int Profiler_BeginSection(const char* name);
void Profiler_BeginSectionByID(int section_id);
void Profiler_EndSection(int section_id);
void Profiler_EndFrame(void);
void Profiler_Enable(bool enable);
bool Profiler_IsEnabled(void);
void Profiler_SetThreadName(const char *name);
const char *Profiler_GetThreadName(void);
const ProfilerFrameSnapshot *Profiler_GetPublishedFrame(void);
uint32_t Profiler_GetPublishedThreadCount(void);
const ProfilerThreadFrame *Profiler_GetPublishedThread(uint32_t index);
const ProfilerSection *Profiler_GetSections(int *count);

#endif // PROFILER_H