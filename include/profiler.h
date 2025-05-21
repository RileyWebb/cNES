#ifndef PROFILER_H
#define PROFILER_H

#include <stdbool.h>
#include <stdint.h>

#define PROFILER_HISTORY_SIZE 200 // Number of frames of history to keep
#define PROFILER_MAX_SECTIONS 64  // Max unique section types
#define PROFILER_SECTION_NAME_LEN 64
#define PROFILER_MAX_FLAME_GRAPH_ITEMS 512 // Max items (calls) in a single frame's flame graph

typedef struct {
    char name[PROFILER_SECTION_NAME_LEN];
    double times[PROFILER_HISTORY_SIZE]; // Time in milliseconds
    double current_time_ms;
    double avg_time_ms;
    double max_time_ms;
    int history_idx;
    bool active; // Is this section currently being profiled this frame?
    uint64_t start_ticks; // For internal timing

    // Flame graph related (for the instance of this section in the current/last frame)
    int parent_id; // ID of the parent section in the current call stack, -1 if root for the frame
    int depth;     // Call stack depth for the current instance
    double start_time_in_frame_ms; // Start time relative to frame start
} ProfilerSection;

// Data for a single item in the flame graph (captured at the end of each section for the last frame)
typedef struct {
    char name[PROFILER_SECTION_NAME_LEN];
    double start_time_ms; // Relative to frame start
    double duration_ms;
    int depth;
    // uint32_t color; // Optional: for consistent coloring
} FlameGraphItem;

typedef struct {
    double frame_times_ms[PROFILER_HISTORY_SIZE];
    int frame_history_idx;
    float current_fps;
    double current_frame_time_ms;
    double avg_frame_time_ms;
    double max_frame_time_ms; // Max frame time in the current history buffer

    uint64_t frame_start_ticks;
    uint64_t perf_freq; // Performance counter frequency

    ProfilerSection sections[PROFILER_MAX_SECTIONS];
    int num_sections;

    bool show_profiler_window;

    // For flame graph generation (data from the last fully processed frame)
    FlameGraphItem last_frame_flame_items[PROFILER_MAX_FLAME_GRAPH_ITEMS]; // Changed size
    int last_frame_flame_items_count;

    // Internal stack for tracking active sections within a frame
    int section_stack[PROFILER_MAX_SECTIONS]; // Stores section IDs
    int section_stack_top;
} Profiler;

// Lifecycle
void Profiler_Init(Profiler* profiler);
void Profiler_Shutdown(Profiler* profiler); // In case of allocated resources

// Frame Timing
void Profiler_BeginFrame(Profiler* profiler);
void Profiler_EndFrame(Profiler* profiler);

// Section Timing
// Returns section_id or -1 on failure
int Profiler_BeginSection(Profiler* profiler, const char* name);
void Profiler_EndSection(Profiler* profiler, int section_id);

// Data Access
float Profiler_GetFPS(const Profiler* profiler);
double Profiler_GetFrameTimeMS(const Profiler* profiler);

#endif // PROFILER_H
