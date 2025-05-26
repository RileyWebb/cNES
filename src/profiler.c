#include "profiler.h"
#include <SDL3/SDL.h> // For SDL_GetPerformanceCounter, SDL_GetPerformanceFrequency
#include <string.h>   // For strncpy, strcmp
#include <stdio.h>    // For snprintf
#include <float.h>    // For DBL_MAX, DBL_MIN

static Profiler g_profiler_instance;
static bool g_profiler_enabled = true;

void Profiler_Init(void)
{
    memset(&g_profiler_instance, 0, sizeof(Profiler));
    g_profiler_instance.perf_freq = SDL_GetPerformanceFrequency();
    g_profiler_instance.frame_history_idx = 0;
    g_profiler_instance.current_fps = 0.0f;
    g_profiler_instance.current_frame_time_ms = 0.0;
    g_profiler_instance.avg_frame_time_ms = 0.0;
    g_profiler_instance.max_frame_time_ms = 0.0;
    g_profiler_instance.num_sections = 0;

    for (int i = 0; i < PROFILER_HISTORY_SIZE; ++i)
    {
        g_profiler_instance.frame_times_ms[i] = 0.0;
    }
    for (int i = 0; i < PROFILER_MAX_SECTIONS; ++i)
    {
        g_profiler_instance.sections[i].history_idx = 0;
        g_profiler_instance.sections[i].current_time_ms = 0.0;
        g_profiler_instance.sections[i].avg_time_ms = 0.0;
        g_profiler_instance.sections[i].max_time_ms = 0.0;
        g_profiler_instance.sections[i].active = false;
        g_profiler_instance.sections[i].parent_id = -1;
        g_profiler_instance.sections[i].depth = 0;
        g_profiler_instance.sections[i].start_time_in_frame_ms = 0.0;
        for (int j = 0; j < PROFILER_HISTORY_SIZE; ++j)
        {
            g_profiler_instance.sections[i].times[j] = 0.0;
        }
    }
    g_profiler_instance.section_stack_top = -1;
    g_profiler_instance.last_frame_flame_items_count = 0;
}

void Profiler_Shutdown(void)
{
}

void Profiler_Enable(bool enable)
{
    g_profiler_enabled = enable;
    if (!g_profiler_enabled)
    {
        // Optionally reset profiler state when disabled
        Profiler_Init(); // Re-initialize to clear old data
    }
}

bool Profiler_IsEnabled(void)
{
    return g_profiler_enabled;
}

const Profiler *Profiler_GetInstance(void)
{
    return &g_profiler_instance;
}

void Profiler_BeginFrame(void)
{
    if (!g_profiler_enabled)
        return;
    g_profiler_instance.frame_start_ticks = SDL_GetPerformanceCounter();
    g_profiler_instance.section_stack_top = -1;           // Reset section stack for the new frame
    g_profiler_instance.last_frame_flame_items_count = 0; // Clear items for the new flame graph
}

void Profiler_EndFrame(void)
{
    if (!g_profiler_enabled || g_profiler_instance.perf_freq == 0)
        return;

    uint64_t end_ticks = SDL_GetPerformanceCounter();
    double frame_duration_s = (double)(end_ticks - g_profiler_instance.frame_start_ticks) / g_profiler_instance.perf_freq;
    g_profiler_instance.current_frame_time_ms = frame_duration_s * 1000.0;

    g_profiler_instance.frame_times_ms[g_profiler_instance.frame_history_idx] = g_profiler_instance.current_frame_time_ms;
    g_profiler_instance.frame_history_idx = (g_profiler_instance.frame_history_idx + 1) % PROFILER_HISTORY_SIZE;

    // Calculate FPS and average/max frame times over the history
    double sum_frame_time = 0.0;
    g_profiler_instance.max_frame_time_ms = 0.0;
    int valid_samples = 0;
    for (int i = 0; i < PROFILER_HISTORY_SIZE; ++i)
    {
        if (g_profiler_instance.frame_times_ms[i] > 0.00001)
        { // Consider non-zero times
            sum_frame_time += g_profiler_instance.frame_times_ms[i];
            if (g_profiler_instance.frame_times_ms[i] > g_profiler_instance.max_frame_time_ms)
            {
                g_profiler_instance.max_frame_time_ms = g_profiler_instance.frame_times_ms[i];
            }
            valid_samples++;
        }
    }

    if (valid_samples > 0)
    {
        g_profiler_instance.avg_frame_time_ms = sum_frame_time / valid_samples;
        if (g_profiler_instance.avg_frame_time_ms > 0.00001)
        {
            g_profiler_instance.current_fps = (float)(1000.0 / g_profiler_instance.avg_frame_time_ms);
        }
        else
        {
            g_profiler_instance.current_fps = 0.0f;
        }
    }
    else
    {
        g_profiler_instance.avg_frame_time_ms = 0.0;
        g_profiler_instance.current_fps = 0.0f;
    }

    // TODO: Update g_profiler_instance.current_cpu_utilization
    // TODO: Update g_profiler_instance.current_gpu_utilization

    // Update section averages
    for (int i = 0; i < g_profiler_instance.num_sections; ++i)
    {
        double section_sum = 0;
        int section_samples = 0;
        g_profiler_instance.sections[i].max_time_ms = 0.0;
        for (int j = 0; j < PROFILER_HISTORY_SIZE; ++j)
        {
            if (g_profiler_instance.sections[i].times[j] > 0.00001)
            {
                section_sum += g_profiler_instance.sections[i].times[j];
                if (g_profiler_instance.sections[i].times[j] > g_profiler_instance.sections[i].max_time_ms)
                {
                    g_profiler_instance.sections[i].max_time_ms = g_profiler_instance.sections[i].times[j];
                }
                section_samples++;
            }
        }
        if (section_samples > 0)
        {
            g_profiler_instance.sections[i].avg_time_ms = section_sum / section_samples;
        }
        else
        {
            g_profiler_instance.sections[i].avg_time_ms = 0.0;
        }
    }
}

int Profiler_BeginSection(const char *name)
{
    if (!g_profiler_enabled || !name)
        return -1;

    int section_id = -1;
    for (int i = 0; i < g_profiler_instance.num_sections; ++i)
    {
        if (strcmp(g_profiler_instance.sections[i].name, name) == 0)
        {
            section_id = i;
            break;
        }
    }

    if (section_id == -1)
    { // New section
        if (g_profiler_instance.num_sections >= PROFILER_MAX_SECTIONS)
        {
            return -1; // No space for new section
        }
        section_id = g_profiler_instance.num_sections++;
        ProfilerSection *new_sec = &g_profiler_instance.sections[section_id];
        strncpy(new_sec->name, name, PROFILER_SECTION_NAME_LEN - 1);
        new_sec->name[PROFILER_SECTION_NAME_LEN - 1] = '\0';
        new_sec->history_idx = 0;
        new_sec->current_time_ms = 0.0;
        new_sec->avg_time_ms = 0.0;
        new_sec->max_time_ms = 0.0;
        for (int j = 0; j < PROFILER_HISTORY_SIZE; ++j)
            new_sec->times[j] = 0.0;
    }

    ProfilerSection *sec = &g_profiler_instance.sections[section_id];
    sec->start_ticks = SDL_GetPerformanceCounter();
    sec->active = true;

    // Hierarchy and flame graph specific data
    sec->parent_id = (g_profiler_instance.section_stack_top >= 0) ? g_profiler_instance.section_stack[g_profiler_instance.section_stack_top] : -1;
    sec->depth = g_profiler_instance.section_stack_top + 1;

    uint64_t current_offset_ticks = sec->start_ticks - g_profiler_instance.frame_start_ticks;
    sec->start_time_in_frame_ms = (double)current_offset_ticks * 1000.0 / g_profiler_instance.perf_freq;

    if (g_profiler_instance.section_stack_top < PROFILER_MAX_SECTIONS - 1)
    {
        g_profiler_instance.section_stack[++g_profiler_instance.section_stack_top] = section_id;
    }
    else
    {
        // Stack overflow
    }
    return section_id;
}

void Profiler_EndSection(int section_id)
{
    if (!g_profiler_enabled || section_id < 0 || section_id >= g_profiler_instance.num_sections || !g_profiler_instance.sections[section_id].active)
    {
        return;
    }

    ProfilerSection *section = &g_profiler_instance.sections[section_id];
    uint64_t end_ticks = SDL_GetPerformanceCounter();
    double duration_s = (double)(end_ticks - section->start_ticks) / g_profiler_instance.perf_freq;
    section->current_time_ms = duration_s * 1000.0;

    section->times[section->history_idx] = section->current_time_ms;
    section->history_idx = (section->history_idx + 1) % PROFILER_HISTORY_SIZE;
    section->active = false;

    // Pop from stack
    if (g_profiler_instance.section_stack_top >= 0 && g_profiler_instance.section_stack[g_profiler_instance.section_stack_top] == section_id)
    {
        g_profiler_instance.section_stack_top--;
    }
    else
    {
        // Stack mismatch
    }

    // Record data for flame graph for this frame
    if (g_profiler_instance.last_frame_flame_items_count < PROFILER_MAX_FLAME_GRAPH_ITEMS)
    {
        FlameGraphItem *item = &g_profiler_instance.last_frame_flame_items[g_profiler_instance.last_frame_flame_items_count++];
        strncpy(item->name, section->name, PROFILER_SECTION_NAME_LEN - 1);
        item->name[PROFILER_SECTION_NAME_LEN - 1] = '\0';
        item->start_time_ms = section->start_time_in_frame_ms;
        item->duration_ms = section->current_time_ms;
        item->depth = section->depth;
    }
}

float Profiler_GetFPS(void)
{
    if (!g_profiler_enabled)
        return 0.0f;
    return g_profiler_instance.current_fps;
}

double Profiler_GetFrameTimeMS(void)
{
    if (!g_profiler_enabled)
        return 0.0;
    return g_profiler_instance.current_frame_time_ms;
}
