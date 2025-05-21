#include "profiler.h"
#include <SDL3/SDL.h> // For SDL_GetPerformanceCounter, SDL_GetPerformanceFrequency
#include <string.h>   // For strncpy, strcmp
#include <stdio.h>    // For snprintf
#include <float.h>    // For DBL_MAX, DBL_MIN

void Profiler_Init(Profiler* profiler) {
    if (!profiler) return;

    memset(profiler, 0, sizeof(Profiler));
    profiler->perf_freq = SDL_GetPerformanceFrequency();
    profiler->frame_history_idx = 0;
    profiler->current_fps = 0.0f;
    profiler->current_frame_time_ms = 0.0;
    profiler->avg_frame_time_ms = 0.0;
    profiler->max_frame_time_ms = 0.0;
    profiler->num_sections = 0;

    for (int i = 0; i < PROFILER_HISTORY_SIZE; ++i) {
        profiler->frame_times_ms[i] = 0.0;
    }
    for (int i = 0; i < PROFILER_MAX_SECTIONS; ++i) {
        profiler->sections[i].history_idx = 0;
        profiler->sections[i].current_time_ms = 0.0;
        profiler->sections[i].avg_time_ms = 0.0;
        profiler->sections[i].max_time_ms = 0.0;
        profiler->sections[i].active = false;
        profiler->sections[i].parent_id = -1;
        profiler->sections[i].depth = 0;
        profiler->sections[i].start_time_in_frame_ms = 0.0;
        for (int j = 0; j < PROFILER_HISTORY_SIZE; ++j) {
            profiler->sections[i].times[j] = 0.0;
        }
    }
    profiler->show_profiler_window = false; // Default to hidden
    profiler->section_stack_top = -1;
    profiler->last_frame_flame_items_count = 0;
}

void Profiler_Shutdown(Profiler* profiler) {
    // Nothing to dynamically allocate/deallocate in the current simple version
    (void)profiler; 
}

void Profiler_BeginFrame(Profiler* profiler) {
    if (!profiler) return;
    profiler->frame_start_ticks = SDL_GetPerformanceCounter();
    profiler->section_stack_top = -1; // Reset section stack for the new frame
    profiler->last_frame_flame_items_count = 0; // Clear items for the new flame graph
}

void Profiler_EndFrame(Profiler* profiler) {
    if (!profiler || profiler->perf_freq == 0) return;

    uint64_t end_ticks = SDL_GetPerformanceCounter();
    double frame_duration_s = (double)(end_ticks - profiler->frame_start_ticks) / profiler->perf_freq;
    profiler->current_frame_time_ms = frame_duration_s * 1000.0;

    profiler->frame_times_ms[profiler->frame_history_idx] = profiler->current_frame_time_ms;
    profiler->frame_history_idx = (profiler->frame_history_idx + 1) % PROFILER_HISTORY_SIZE;

    // Calculate FPS and average/max frame times over the history
    double sum_frame_time = 0.0;
    profiler->max_frame_time_ms = 0.0;
    int valid_samples = 0;
    for (int i = 0; i < PROFILER_HISTORY_SIZE; ++i) {
        if (profiler->frame_times_ms[i] > 0.00001) { // Consider non-zero times
            sum_frame_time += profiler->frame_times_ms[i];
            if (profiler->frame_times_ms[i] > profiler->max_frame_time_ms) {
                profiler->max_frame_time_ms = profiler->frame_times_ms[i];
            }
            valid_samples++;
        }
    }

    if (valid_samples > 0) {
        profiler->avg_frame_time_ms = sum_frame_time / valid_samples;
        if (profiler->avg_frame_time_ms > 0.00001) {
            profiler->current_fps = (float)(1000.0 / profiler->avg_frame_time_ms);
        } else {
            profiler->current_fps = 0.0f;
        }
    } else {
        profiler->avg_frame_time_ms = 0.0;
        profiler->current_fps = 0.0f;
    }

    // Update section averages
    for (int i = 0; i < profiler->num_sections; ++i) {
        double section_sum = 0;
        int section_samples = 0;
        profiler->sections[i].max_time_ms = 0.0;
        for (int j = 0; j < PROFILER_HISTORY_SIZE; ++j) {
            if (profiler->sections[i].times[j] > 0.00001) {
                section_sum += profiler->sections[i].times[j];
                if (profiler->sections[i].times[j] > profiler->sections[i].max_time_ms) {
                    profiler->sections[i].max_time_ms = profiler->sections[i].times[j];
                }
                section_samples++;
            }
        }
        if (section_samples > 0) {
            profiler->sections[i].avg_time_ms = section_sum / section_samples;
        } else {
            profiler->sections[i].avg_time_ms = 0.0;
        }
    }
}

int Profiler_BeginSection(Profiler* profiler, const char* name) {
    if (!profiler || !name) return -1;

    int section_id = -1;
    for (int i = 0; i < profiler->num_sections; ++i) {
        if (strcmp(profiler->sections[i].name, name) == 0) {
            section_id = i;
            break;
        }
    }

    if (section_id == -1) { // New section
        if (profiler->num_sections >= PROFILER_MAX_SECTIONS) {
            return -1; // No space for new section
        }
        section_id = profiler->num_sections++;
        ProfilerSection* new_sec = &profiler->sections[section_id];
        strncpy(new_sec->name, name, PROFILER_SECTION_NAME_LEN - 1);
        new_sec->name[PROFILER_SECTION_NAME_LEN - 1] = '\0';
        new_sec->history_idx = 0;
        new_sec->current_time_ms = 0.0;
        new_sec->avg_time_ms = 0.0;
        new_sec->max_time_ms = 0.0;
        for(int j=0; j<PROFILER_HISTORY_SIZE; ++j) new_sec->times[j] = 0.0;
    }

    ProfilerSection* sec = &profiler->sections[section_id];
    sec->start_ticks = SDL_GetPerformanceCounter();
    sec->active = true;

    // Hierarchy and flame graph specific data
    sec->parent_id = (profiler->section_stack_top >= 0) ? profiler->section_stack[profiler->section_stack_top] : -1;
    sec->depth = profiler->section_stack_top + 1;
    
    uint64_t current_offset_ticks = sec->start_ticks - profiler->frame_start_ticks;
    sec->start_time_in_frame_ms = (double)current_offset_ticks * 1000.0 / profiler->perf_freq;

    if (profiler->section_stack_top < PROFILER_MAX_SECTIONS - 1) {
        profiler->section_stack[++profiler->section_stack_top] = section_id;
    } else {
        // Stack overflow, should not happen if PROFILER_MAX_SECTIONS is large enough
        // Or handle error: e.g., by not pushing.
    }
    return section_id;
}

void Profiler_EndSection(Profiler* profiler, int section_id) {
    if (!profiler || section_id < 0 || section_id >= profiler->num_sections || !profiler->sections[section_id].active) {
        return;
    }

    ProfilerSection* section = &profiler->sections[section_id];
    uint64_t end_ticks = SDL_GetPerformanceCounter();
    double duration_s = (double)(end_ticks - section->start_ticks) / profiler->perf_freq;
    section->current_time_ms = duration_s * 1000.0;

    section->times[section->history_idx] = section->current_time_ms;
    section->history_idx = (section->history_idx + 1) % PROFILER_HISTORY_SIZE;
    section->active = false;

    // Pop from stack
    if (profiler->section_stack_top >= 0 && profiler->section_stack[profiler->section_stack_top] == section_id) {
        profiler->section_stack_top--;
    } else {
        // Stack mismatch, indicates an error in Begin/EndSection pairing
    }

    // Record data for flame graph for this frame
    if (profiler->last_frame_flame_items_count < PROFILER_MAX_FLAME_GRAPH_ITEMS) { // Use new constant
        FlameGraphItem* item = &profiler->last_frame_flame_items[profiler->last_frame_flame_items_count++];
        strncpy(item->name, section->name, PROFILER_SECTION_NAME_LEN -1);
        item->name[PROFILER_SECTION_NAME_LEN-1] = '\0';
        item->start_time_ms = section->start_time_in_frame_ms;
        item->duration_ms = section->current_time_ms;
        item->depth = section->depth;
    }
}

float Profiler_GetFPS(const Profiler* profiler) {
    return profiler ? profiler->current_fps : 0.0f;
}

double Profiler_GetFrameTimeMS(const Profiler* profiler) {
    return profiler ? profiler->current_frame_time_ms : 0.0;
}
