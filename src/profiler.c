#include "profiler.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <stdio.h> // For snprintf, if used by user for section names.

// These constants are expected to be defined in profiler.h or a shared config.
// For example:
// #define PROFILER_MAX_SECTIONS 64
// #define PROFILER_SECTION_NAME_LEN 64
// #define PROFILER_HISTORY_SIZE 120

// The ProfilerSection struct remains the same.
// Its instances in g_profiler.sections will hold data from the *previous* completed frame.
// Its instances in g_profiler.write_sections_buffer will be used for the *current* frame.

void Profiler_Init(void)
{
    memset(&g_profiler, 0, sizeof(Profiler));
    g_profiler.perf_freq = SDL_GetPerformanceFrequency();
    g_profiler.num_sections = 0; // Initialize read view section count
    g_profiler.num_write_sections = 0; // Initialize write buffer section count
}

void Profiler_Shutdown(void)
{
    // Nothing specific to clean up for this simplified version
}

void Profiler_Enable(bool enable)
{
    g_profiler_enabled = enable;
}

bool Profiler_IsEnabled(void)
{
    return g_profiler_enabled;
}

Profiler* Profiler_GetInstance(void)
{
    return &g_profiler; // g_profiler now contains the "read" view
}

void Profiler_BeginFrame(void)
{
    if (!g_profiler_enabled) return;

    g_profiler.current_processing_frame_start_ticks = SDL_GetPerformanceCounter();

    // Reset per-frame aggregates for all sections in the write buffer
    for (int i = 0; i < g_profiler.num_write_sections; i++) {
        g_profiler.write_sections_buffer[i].total_ticks_this_frame = 0;
        g_profiler.write_sections_buffer[i].call_count_this_frame = 0;
        g_profiler.write_sections_buffer[i].total_time_this_frame_ms = 0.0;
        g_profiler.write_sections_buffer[i].start_ticks_for_current_call = 0;
    }
}

void Profiler_EndFrame(void)
{
    if (!g_profiler_enabled || g_profiler.perf_freq == 0) return;

    uint64_t end_ticks = SDL_GetPerformanceCounter();
    uint64_t frame_duration_ticks = end_ticks - g_profiler.current_processing_frame_start_ticks;
    double processed_frame_time_ms = (double)frame_duration_ticks * 1000.0 / g_profiler.perf_freq;

    // Update frame time history for FPS calculation using the just processed frame's time
    g_profiler.frame_times[g_profiler.frame_history_idx] = processed_frame_time_ms;
    g_profiler.frame_history_idx = (g_profiler.frame_history_idx + 1) % PROFILER_HISTORY_SIZE;

    // Calculate frame statistics (avg, max, fps) based on history
    double sum_frame_times = 0.0;
    double max_ft_hist = 0.0;
    int valid_samples = 0;

    for (int i = 0; i < PROFILER_HISTORY_SIZE; i++) {
        if (g_profiler.frame_times[i] > 0.000001) {
            sum_frame_times += g_profiler.frame_times[i];
            if (g_profiler.frame_times[i] > max_ft_hist) {
                max_ft_hist = g_profiler.frame_times[i];
            }
            valid_samples++;
        }
    }
    
    double avg_ft_hist = 0.0;
    float fps_hist = 0.0f;
    if (valid_samples > 0) {
        avg_ft_hist = sum_frame_times / valid_samples;
        fps_hist = (avg_ft_hist > 0.000001) ? (float)(1000.0 / avg_ft_hist) : 0.0f;
    }

    // Finalize section times in the write buffer
    for (int i = 0; i < g_profiler.num_write_sections; i++) {
        ProfilerSection* sec_write = &g_profiler.write_sections_buffer[i];
        if (g_profiler.perf_freq > 0) {
            sec_write->total_time_this_frame_ms =
                (double)sec_write->total_ticks_this_frame * 1000.0 / g_profiler.perf_freq;
        } else {
            sec_write->total_time_this_frame_ms = 0.0;
        }
    }

    // --- Copy data from write buffer to read buffer (g_profiler main fields) ---
    g_profiler.current_frame_time_ms = processed_frame_time_ms;
    g_profiler.avg_frame_time_ms = avg_ft_hist;
    g_profiler.max_frame_time_ms = max_ft_hist; // Max from history, not just current frame
    g_profiler.current_fps = fps_hist;

    // Copy section data
    g_profiler.num_sections = g_profiler.num_write_sections;
    for (int i = 0; i < g_profiler.num_write_sections; i++) {
        // Deep copy the section data from write buffer to read buffer
        // Note: start_ticks_for_current_call is copied but is stale/irrelevant for the read view.
        memcpy(&g_profiler.sections[i], &g_profiler.write_sections_buffer[i], sizeof(ProfilerSection));
    }
}

static int Profiler_Internal_GetOrCreateSection(const char* name) {
    if (!name) return -1;

    // Operate on the write buffer
    for (int i = 0; i < g_profiler.num_write_sections; i++) {
        if (strncmp(g_profiler.write_sections_buffer[i].name, name, PROFILER_SECTION_NAME_LEN) == 0) {
            return i;
        }
    }

    if (g_profiler.num_write_sections >= PROFILER_MAX_SECTIONS) {
        return -1; // No space for new section in write buffer
    }

    int id = g_profiler.num_write_sections++;
    ProfilerSection* sec = &g_profiler.write_sections_buffer[id];

    strncpy(sec->name, name, PROFILER_SECTION_NAME_LEN - 1);
    sec->name[PROFILER_SECTION_NAME_LEN - 1] = '\0'; // Ensure null termination
    sec->total_ticks_this_frame = 0;
    sec->call_count_this_frame = 0;
    sec->total_time_this_frame_ms = 0.0;
    sec->start_ticks_for_current_call = 0;

    return id;
}

int Profiler_CreateSection(const char* name)
{
    if (!g_profiler_enabled || !name) {
        return -1;
    }
    return Profiler_Internal_GetOrCreateSection(name);
}

int Profiler_BeginSection(const char* name)
{
    if (!g_profiler_enabled || !name || g_profiler.perf_freq == 0) {
        return -1;
    }

    int section_id = Profiler_Internal_GetOrCreateSection(name);
    if (section_id == -1) {
        return -1;
    }

    Profiler_BeginSectionByID(section_id);
    return section_id;
}

void Profiler_BeginSectionByID(int section_id)
{
    if (!g_profiler_enabled ||
        section_id < 0 ||
        section_id >= g_profiler.num_write_sections || // Check against write buffer
        g_profiler.perf_freq == 0) {
        return;
    }
    // Operate on write buffer
    g_profiler.write_sections_buffer[section_id].start_ticks_for_current_call = SDL_GetPerformanceCounter();
}

void Profiler_EndSection(int section_id)
{
    if (!g_profiler_enabled ||
        section_id < 0 ||
        section_id >= g_profiler.num_write_sections || // Check against write buffer
        g_profiler.perf_freq == 0) {
        return;
    }

    // Operate on write buffer
    ProfilerSection* sec = &g_profiler.write_sections_buffer[section_id];

    if (sec->start_ticks_for_current_call == 0) {
        return;
    }
    
    uint64_t end_ticks = SDL_GetPerformanceCounter();
    uint64_t duration_ticks = end_ticks - sec->start_ticks_for_current_call;

    sec->total_ticks_this_frame += duration_ticks;
    sec->call_count_this_frame++;
    sec->start_ticks_for_current_call = 0; 
}

float Profiler_GetFPS(void)
{
    return g_profiler_enabled ? g_profiler.current_fps : 0.0f;
}

double Profiler_GetFrameTimeMS(void)
{
    return g_profiler_enabled ? g_profiler.current_frame_time_ms : 0.0;
}
