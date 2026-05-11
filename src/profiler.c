#include "profiler.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Profiler g_profiler;

static ProfilerThreadState *Profiler_GetThreadState(void);
static void SDLCALL Profiler_DestroyThreadState(void *value);
static void Profiler_AssignThreadSlot(ProfilerThreadState *state);
static void Profiler_ResetFrame(ProfilerFrameSnapshot *frame, uint64_t frame_index);
static void Profiler_ResetSectionStats(void);
static void Profiler_FinalizeSectionStats(void);
static int Profiler_FindSectionIndex(const char *name);
static int Profiler_CreateSectionLocked(const char *name);

void Profiler_Init(void)
{
    memset(&g_profiler, 0, sizeof(g_profiler));
    g_profiler.enabled = false;
    g_profiler.perf_freq = SDL_GetPerformanceFrequency();
    g_profiler.mutex = SDL_CreateMutex();
    g_profiler.write_frame = &g_profiler.frame_storage[0];
    g_profiler.published_frame = &g_profiler.frame_storage[1];
    g_profiler.thread_tls = (SDL_TLSID){0};
    Profiler_ResetFrame(g_profiler.write_frame, 0);
    Profiler_ResetFrame(g_profiler.published_frame, 0);
}

void Profiler_Shutdown(void)
{
    if (g_profiler.mutex) {
        SDL_DestroyMutex(g_profiler.mutex);
        g_profiler.mutex = NULL;
    }

    g_profiler.write_frame = NULL;
    g_profiler.published_frame = NULL;
}

void Profiler_Enable(bool enable)
{
    g_profiler.enabled = enable;
}

bool Profiler_IsEnabled(void)
{
    return g_profiler.enabled;
}

Profiler *Profiler_GetInstance(void)
{
    return &g_profiler;
}

void Profiler_SetThreadName(const char *name)
{
    if (!name || !*name) {
        return;
    }

    ProfilerThreadState *state = Profiler_GetThreadState();
    if (!state) {
        return;
    }

    strncpy(state->thread_name, name, PROFILER_THREAD_NAME_LEN - 1);
    state->thread_name[PROFILER_THREAD_NAME_LEN - 1] = '\0';

    if (g_profiler.write_frame && state->thread_slot < g_profiler.write_frame->thread_count) {
        ProfilerThreadFrame *thread_frame = &g_profiler.write_frame->threads[state->thread_slot];
        strncpy(thread_frame->name, state->thread_name, PROFILER_THREAD_NAME_LEN - 1);
        thread_frame->name[PROFILER_THREAD_NAME_LEN - 1] = '\0';
    }
}

const char *Profiler_GetThreadName(void)
{
    ProfilerThreadState *state = Profiler_GetThreadState();
    if (!state) {
        return "Thread";
    }

    if (state->thread_name[0] == '\0') {
        snprintf(state->thread_name, sizeof(state->thread_name), "Thread %u", (unsigned)SDL_GetCurrentThreadID());
    }

    return state->thread_name;
}

static void Profiler_ResetFrame(ProfilerFrameSnapshot *frame, uint64_t frame_index)
{
    if (!frame) {
        return;
    }

    memset(frame, 0, sizeof(*frame));
    frame->frame_index = frame_index;
    frame->start_ticks = SDL_GetPerformanceCounter();
}

void Profiler_BeginFrame(void)
{
    if (!g_profiler.enabled || g_profiler.perf_freq == 0 || !g_profiler.write_frame) {
        return;
    }

    g_profiler.frame_counter++;
    if (g_profiler.write_frame == &g_profiler.frame_storage[0]) {
        g_profiler.write_frame = &g_profiler.frame_storage[1];
    } else {
        g_profiler.write_frame = &g_profiler.frame_storage[0];
    }

    Profiler_ResetFrame(g_profiler.write_frame, g_profiler.frame_counter);
}

static void Profiler_ResetSectionStats(void)
{
    for (int i = 0; i < g_profiler.num_sections; ++i) {
        ProfilerSection *section = &g_profiler.sections[i];
        section->total_ticks_this_frame = 0;
        section->max_ticks_this_frame = 0;
        section->last_ticks_this_frame = 0;
        section->call_count_this_frame = 0;
        section->total_time_this_frame_ms = 0.0;
        section->max_time_this_frame_ms = 0.0;
        section->last_time_this_frame_ms = 0.0;
    }
}

static void Profiler_FinalizeSectionStats(void)
{
    Profiler_ResetSectionStats();

    if (!g_profiler.write_frame) {
        return;
    }

    for (uint32_t thread_index = 0; thread_index < g_profiler.write_frame->thread_count; ++thread_index) {
        const ProfilerThreadFrame *thread_frame = &g_profiler.write_frame->threads[thread_index];
        for (uint32_t event_index = 0; event_index < thread_frame->event_count; ++event_index) {
            const ProfilerScopeEvent *event = &thread_frame->events[event_index];
            if (event->section_id < 0 || event->section_id >= g_profiler.num_sections) {
                continue;
            }

            if (event->end_ticks <= event->start_ticks) {
                continue;
            }

            uint64_t duration_ticks = event->end_ticks - event->start_ticks;
            ProfilerSection *section = &g_profiler.sections[event->section_id];
            section->total_ticks_this_frame += duration_ticks;
            section->last_ticks_this_frame = duration_ticks;
            if (duration_ticks > section->max_ticks_this_frame) {
                section->max_ticks_this_frame = duration_ticks;
            }
            section->call_count_this_frame++;
        }
    }

    for (int i = 0; i < g_profiler.num_sections; ++i) {
        ProfilerSection *section = &g_profiler.sections[i];
        section->total_time_this_frame_ms = (double)section->total_ticks_this_frame * 1000.0 / (double)g_profiler.perf_freq;
        section->max_time_this_frame_ms = (double)section->max_ticks_this_frame * 1000.0 / (double)g_profiler.perf_freq;
        section->last_time_this_frame_ms = (double)section->last_ticks_this_frame * 1000.0 / (double)g_profiler.perf_freq;
    }
}

void Profiler_EndFrame(void)
{
    if (!g_profiler.enabled || g_profiler.perf_freq == 0 || !g_profiler.write_frame) {
        return;
    }

    g_profiler.write_frame->end_ticks = SDL_GetPerformanceCounter();
    g_profiler.write_frame->duration_ms = (double)(g_profiler.write_frame->end_ticks - g_profiler.write_frame->start_ticks) * 1000.0 / (double)g_profiler.perf_freq;

    g_profiler.current_frame_start_ticks = g_profiler.write_frame->start_ticks;
    g_profiler.current_frame_end_ticks = g_profiler.write_frame->end_ticks;
    g_profiler.current_frame_time_ms = g_profiler.write_frame->duration_ms;

    g_profiler.frame_times[g_profiler.frame_history_idx] = g_profiler.current_frame_time_ms;
    g_profiler.frame_history_idx = (g_profiler.frame_history_idx + 1) % PROFILER_HISTORY_SIZE;

    double sum_frame_times = 0.0;
    double max_frame_time = 0.0;
    int valid_samples = 0;
    for (int i = 0; i < PROFILER_HISTORY_SIZE; ++i) {
        if (g_profiler.frame_times[i] > 0.000001) {
            sum_frame_times += g_profiler.frame_times[i];
            if (g_profiler.frame_times[i] > max_frame_time) {
                max_frame_time = g_profiler.frame_times[i];
            }
            valid_samples++;
        }
    }

    g_profiler.avg_frame_time_ms = (valid_samples > 0) ? (sum_frame_times / (double)valid_samples) : 0.0;
    g_profiler.max_frame_time_ms = max_frame_time;
    g_profiler.current_fps = (g_profiler.avg_frame_time_ms > 0.000001) ? (float)(1000.0 / g_profiler.avg_frame_time_ms) : 0.0f;

    Profiler_FinalizeSectionStats();
    g_profiler.published_frame = g_profiler.write_frame;
}

const ProfilerFrameSnapshot *Profiler_GetPublishedFrame(void)
{
    return g_profiler.published_frame ? g_profiler.published_frame : g_profiler.write_frame;
}

uint32_t Profiler_GetPublishedThreadCount(void)
{
    const ProfilerFrameSnapshot *frame = Profiler_GetPublishedFrame();
    return frame ? frame->thread_count : 0;
}

const ProfilerThreadFrame *Profiler_GetPublishedThread(uint32_t index)
{
    const ProfilerFrameSnapshot *frame = Profiler_GetPublishedFrame();
    if (!frame || index >= frame->thread_count) {
        return NULL;
    }

    return &frame->threads[index];
}

const ProfilerSection *Profiler_GetSections(int *count)
{
    if (count) {
        *count = g_profiler.num_sections;
    }

    return g_profiler.sections;
}

static int Profiler_FindSectionIndex(const char *name)
{
    if (!name) {
        return -1;
    }

    for (int i = 0; i < g_profiler.num_sections; ++i) {
        if (strncmp(g_profiler.sections[i].name, name, PROFILER_SECTION_NAME_LEN) == 0) {
            return i;
        }
    }

    return -1;
}

static int Profiler_CreateSectionLocked(const char *name)
{
    if (!name) {
        return -1;
    }

    int index = Profiler_FindSectionIndex(name);
    if (index >= 0) {
        return index;
    }

    if (g_profiler.num_sections >= PROFILER_MAX_SECTIONS) {
        return -1;
    }

    index = g_profiler.num_sections++;
    ProfilerSection *section = &g_profiler.sections[index];
    memset(section, 0, sizeof(*section));
    strncpy(section->name, name, PROFILER_SECTION_NAME_LEN - 1);
    section->name[PROFILER_SECTION_NAME_LEN - 1] = '\0';
    return index;
}

int Profiler_CreateSection(const char *name)
{
    if (!g_profiler.enabled || !name || !*name) {
        return -1;
    }

    if (!g_profiler.mutex) {
        return Profiler_CreateSectionLocked(name);
    }

    SDL_LockMutex(g_profiler.mutex);
    int result = Profiler_CreateSectionLocked(name);
    SDL_UnlockMutex(g_profiler.mutex);
    return result;
}

int Profiler_BeginSection(const char *name)
{
    if (!g_profiler.enabled || !name || !*name) {
        return -1;
    }

    int section_id = Profiler_CreateSection(name);
    if (section_id < 0) {
        return -1;
    }

    Profiler_BeginSectionByID(section_id);
    return section_id;
}

static void Profiler_AssignThreadSlot(ProfilerThreadState *state)
{
    if (!state || !g_profiler.write_frame) {
        return;
    }

    SDL_ThreadID thread_id = SDL_GetCurrentThreadID();

    if (!g_profiler.mutex) {
        return;
    }

    SDL_LockMutex(g_profiler.mutex);

    ProfilerFrameSnapshot *frame = g_profiler.write_frame;
    uint32_t slot = UINT32_MAX;
    for (uint32_t i = 0; i < frame->thread_count; ++i) {
        if (frame->threads[i].thread_id == thread_id) {
            slot = i;
            break;
        }
    }

    if (slot == UINT32_MAX) {
        if (frame->thread_count < PROFILER_MAX_THREADS) {
            slot = frame->thread_count++;
            ProfilerThreadFrame *thread_frame = &frame->threads[slot];
            memset(thread_frame, 0, sizeof(*thread_frame));
            thread_frame->thread_id = thread_id;

            if (state->thread_name[0] != '\0') {
                strncpy(thread_frame->name, state->thread_name, PROFILER_THREAD_NAME_LEN - 1);
                thread_frame->name[PROFILER_THREAD_NAME_LEN - 1] = '\0';
            } else {
                snprintf(thread_frame->name, sizeof(thread_frame->name), "Thread %u", (unsigned)thread_id);
            }
        }
    }

    SDL_UnlockMutex(g_profiler.mutex);

    if (slot == UINT32_MAX) {
        return;
    }

    state->frame_index = g_profiler.frame_counter;
    state->thread_slot = slot;
    state->stack_depth = 0;

    if (state->thread_name[0] == '\0') {
        snprintf(state->thread_name, sizeof(state->thread_name), "Thread %u", (unsigned)thread_id);
    }
}

static ProfilerThreadState *Profiler_GetThreadState(void)
{
    ProfilerThreadState *state = (ProfilerThreadState *)SDL_GetTLS(&g_profiler.thread_tls);
    if (!state) {
        state = (ProfilerThreadState *)calloc(1, sizeof(*state));
        if (!state) {
            return NULL;
        }

        if (!SDL_SetTLS(&g_profiler.thread_tls, state, Profiler_DestroyThreadState)) {
            free(state);
            return NULL;
        }
    }

    if (state->frame_index != g_profiler.frame_counter ||
        !g_profiler.write_frame ||
        state->thread_slot >= g_profiler.write_frame->thread_count ||
        g_profiler.write_frame->threads[state->thread_slot].thread_id != SDL_GetCurrentThreadID()) {
        Profiler_AssignThreadSlot(state);
    }

    return state;
}

static void SDLCALL Profiler_DestroyThreadState(void *value)
{
    free(value);
}

void Profiler_BeginSectionByID(int section_id)
{
    if (!g_profiler.enabled || section_id < 0 || section_id >= g_profiler.num_sections || g_profiler.perf_freq == 0) {
        return;
    }

    ProfilerThreadState *state = Profiler_GetThreadState();
    if (!state || !g_profiler.write_frame) {
        return;
    }

    if (state->stack_depth >= PROFILER_MAX_SCOPE_DEPTH) {
        return;
    }

    if (state->frame_index != g_profiler.frame_counter) {
        state->frame_index = g_profiler.frame_counter;
        state->stack_depth = 0;
    }

    ProfilerThreadFrame *thread_frame = &g_profiler.write_frame->threads[state->thread_slot];
    if (thread_frame->event_count >= PROFILER_MAX_EVENTS_PER_THREAD) {
        state->event_stack[state->stack_depth] = UINT32_MAX;
        state->section_stack[state->stack_depth] = section_id;
        state->stack_depth++;
        return;
    }

    uint32_t event_index = thread_frame->event_count++;
    ProfilerScopeEvent *event = &thread_frame->events[event_index];
    memset(event, 0, sizeof(*event));
    event->section_id = (uint16_t)section_id;
    event->thread_slot = (uint16_t)state->thread_slot;
    event->depth = state->stack_depth;
    event->start_ticks = SDL_GetPerformanceCounter();

    state->event_stack[state->stack_depth] = event_index;
    state->section_stack[state->stack_depth] = section_id;
    state->stack_depth++;
}

void Profiler_EndSection(int section_id)
{
    if (!g_profiler.enabled || section_id < 0 || section_id >= g_profiler.num_sections || g_profiler.perf_freq == 0) {
        return;
    }

    ProfilerThreadState *state = Profiler_GetThreadState();
    if (!state || state->stack_depth == 0 || !g_profiler.write_frame) {
        return;
    }

    int found = -1;
    for (int i = (int)state->stack_depth - 1; i >= 0; --i) {
        if (state->section_stack[i] == section_id) {
            found = i;
            break;
        }
    }

    if (found < 0) {
        return;
    }

    uint32_t event_index = state->event_stack[found];
    state->stack_depth = (uint16_t)found;

    if (event_index == UINT32_MAX) {
        return;
    }

    ProfilerThreadFrame *thread_frame = &g_profiler.write_frame->threads[state->thread_slot];
    if (event_index >= thread_frame->event_count) {
        return;
    }

    ProfilerScopeEvent *event = &thread_frame->events[event_index];
    event->end_ticks = SDL_GetPerformanceCounter();
}
