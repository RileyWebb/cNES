#include "profiler.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <stdio.h>

static Profiler g_profiler;
static bool g_profiler_enabled = true;

void Profiler_Init(void)
{
    memset(&g_profiler, 0, sizeof(Profiler));
    g_profiler.perf_freq = SDL_GetPerformanceFrequency();
    g_profiler.stack_depth = -1;
}

void Profiler_Shutdown(void)
{
    // Nothing to clean up
}

void Profiler_Enable(bool enable)
{
    g_profiler_enabled = enable;
}

bool Profiler_IsEnabled(void)
{
    return g_profiler_enabled;
}

const Profiler* Profiler_GetInstance(void)
{
    return &g_profiler;
}

void Profiler_BeginFrame(void)
{
    if (!g_profiler_enabled) return;
    
    g_profiler.frame_start_ticks = SDL_GetPerformanceCounter();
    g_profiler.stack_depth = -1;
    g_profiler.flame_item_count = 0;
    
    // Reset per-frame aggregates
    for (int i = 0; i < g_profiler.num_sections; i++) {
        g_profiler.sections[i].total_ticks_this_frame = 0;
        g_profiler.sections[i].call_count_this_frame = 0;
        g_profiler.sections[i].total_time_this_frame_ms = 0.0;
    }
}

void Profiler_EndFrame(void)
{
    if (!g_profiler_enabled || g_profiler.perf_freq == 0) return;
    
    uint64_t end_ticks = SDL_GetPerformanceCounter();
    double frame_duration_s = (double)(end_ticks - g_profiler.frame_start_ticks) / g_profiler.perf_freq;
    g_profiler.current_frame_time_ms = frame_duration_s * 1000.0;
    
    // Update frame time history
    g_profiler.frame_times[g_profiler.frame_history_idx] = g_profiler.current_frame_time_ms;
    g_profiler.frame_history_idx = (g_profiler.frame_history_idx + 1) % PROFILER_HISTORY_SIZE;
    
    // Calculate frame statistics
    double sum = 0.0;
    g_profiler.max_frame_time_ms = 0.0;
    int valid_samples = 0;
    
    for (int i = 0; i < PROFILER_HISTORY_SIZE; i++) {
        if (g_profiler.frame_times[i] > 0.001) {
            sum += g_profiler.frame_times[i];
            if (g_profiler.frame_times[i] > g_profiler.max_frame_time_ms) {
                g_profiler.max_frame_time_ms = g_profiler.frame_times[i];
            }
            valid_samples++;
        }
    }
    
    if (valid_samples > 0) {
        g_profiler.avg_frame_time_ms = sum / valid_samples;
        g_profiler.current_fps = (g_profiler.avg_frame_time_ms > 0.001) ? 
            (float)(1000.0 / g_profiler.avg_frame_time_ms) : 0.0f;
    } else {
        g_profiler.avg_frame_time_ms = 0.0;
        g_profiler.current_fps = 0.0f;
    }
    
    // Update section statistics
    for (int i = 0; i < g_profiler.num_sections; i++) {
        ProfilerSection* sec = &g_profiler.sections[i];
        
        // Calculate total time for this frame
        if (g_profiler.perf_freq > 0) {
            sec->total_time_this_frame_ms = 
                (double)sec->total_ticks_this_frame * 1000.0 / g_profiler.perf_freq;
        }
        
        // Update historical averages and max
        double section_sum = 0.0;
        int section_samples = 0;
        sec->max_time_ms = 0.0;
        
        for (int j = 0; j < PROFILER_HISTORY_SIZE; j++) {
            if (sec->times[j] > 0.001) {
                section_sum += sec->times[j];
                if (sec->times[j] > sec->max_time_ms) {
                    sec->max_time_ms = sec->times[j];
                }
                section_samples++;
            }
        }
        
        sec->avg_time_ms = (section_samples > 0) ? 
            section_sum / section_samples : 0.0;
    }
}

int Profiler_CreateSection(const char* name)
{
    if (!g_profiler_enabled || !name || g_profiler.num_sections >= PROFILER_MAX_SECTIONS) {
        return -1;
    }
    
    // Check if section already exists
    for (int i = 0; i < g_profiler.num_sections; i++) {
        if (strcmp(g_profiler.sections[i].name, name) == 0) {
            return i;
        }
    }
    
    // Create new section
    int id = g_profiler.num_sections++;
    ProfilerSection* sec = &g_profiler.sections[id];
    
    strncpy(sec->name, name, PROFILER_SECTION_NAME_LEN - 1);
    sec->name[PROFILER_SECTION_NAME_LEN - 1] = '\0';
    sec->history_idx = 0;
    sec->current_time_ms = 0.0;
    sec->avg_time_ms = 0.0;
    sec->max_time_ms = 0.0;
    sec->total_ticks_this_frame = 0;
    sec->call_count_this_frame = 0;
    sec->total_time_this_frame_ms = 0.0;
    sec->active = false;
    sec->depth = 0;
    
    for (int i = 0; i < PROFILER_HISTORY_SIZE; i++) {
        sec->times[i] = 0.0;
    }
    
    return id;
}

int Profiler_BeginSection(const char* name)
{
    if (!g_profiler_enabled || !name) return -1;
    
    int section_id = Profiler_CreateSection(name);
    if (section_id == -1) return -1;
    
    Profiler_BeginSectionByID(section_id);
    return section_id;
}

void Profiler_BeginSectionByID(int section_id)
{
    if (!g_profiler_enabled || 
        section_id < 0 || 
        section_id >= g_profiler.num_sections || 
        g_profiler.perf_freq == 0) {
        return;
    }
    
    ProfilerSection* sec = &g_profiler.sections[section_id];
    sec->start_ticks = SDL_GetPerformanceCounter();
    sec->active = true;
    sec->depth = g_profiler.stack_depth + 1;
    
    // Push to call stack
    if (g_profiler.stack_depth < PROFILER_MAX_STACK_DEPTH - 1) {
        g_profiler.call_stack[++g_profiler.stack_depth] = section_id;
    }
}

void Profiler_EndSection(int section_id)
{
    if (!g_profiler_enabled || 
        section_id < 0 || 
        section_id >= g_profiler.num_sections || 
        g_profiler.perf_freq == 0) {
        return;
    }
    
    ProfilerSection* sec = &g_profiler.sections[section_id];
    if (!sec->active) return;
    
    uint64_t end_ticks = SDL_GetPerformanceCounter();
    uint64_t duration_ticks = end_ticks - sec->start_ticks;
    double duration_ms = (double)duration_ticks * 1000.0 / g_profiler.perf_freq;
    
    sec->current_time_ms = duration_ms;
    sec->times[sec->history_idx] = duration_ms;
    sec->history_idx = (sec->history_idx + 1) % PROFILER_HISTORY_SIZE;
    sec->active = false;
    
    // Update per-frame aggregates
    sec->call_count_this_frame++;
    sec->total_ticks_this_frame += duration_ticks;
    
    // Pop from stack
    if (g_profiler.stack_depth >= 0 && 
        g_profiler.call_stack[g_profiler.stack_depth] == section_id) {
        g_profiler.stack_depth--;
    }
    
    // Add to flame graph
    if (g_profiler.flame_item_count < PROFILER_MAX_FLAME_ITEMS) {
        FlameGraphItem* item = &g_profiler.flame_items[g_profiler.flame_item_count++];
        strncpy(item->name, sec->name, PROFILER_SECTION_NAME_LEN - 1);
        item->name[PROFILER_SECTION_NAME_LEN - 1] = '\0';
        
        double start_offset_ms = (double)(sec->start_ticks - g_profiler.frame_start_ticks) * 
                                1000.0 / g_profiler.perf_freq;
        item->start_time_ms = start_offset_ms;
        item->duration_ms = duration_ms;
        item->depth = sec->depth;
    }
}

float Profiler_GetFPS(void)
{
    return g_profiler_enabled ? g_profiler.current_fps : 0.0f;
}

double Profiler_GetFrameTimeMS(void)
{
    return g_profiler_enabled ? g_profiler.current_frame_time_ms : 0.0;
}
