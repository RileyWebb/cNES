
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <cimgui.h>
#include <cimgui_impl.h>
#include <cimplot.h>
#include <stdio.h>
#include <string.h>
    #include "debug.h"
    #include "profiler.h"
#include "cNES/nes.h"
#include "cNES/cpu.h"
#include "cNES/ppu.h"

#include "frontend/frontend.h"
#include "frontend/frontend_internal.h"
#include "frontend/frontend_profiler.h"

// --- Profiler Section IDs ---
static int frontend_profiler_ppu_section = -1;
static int frontend_profiler_cpu_section = -1;
static int frontend_profiler_nmi_section = -1;

// --- Selection State for Profiler UI ---
typedef struct FrontendProfilerSelection {
    bool active;
    uint32_t thread_index;
    uint32_t event_index;
    uint16_t section_id;
    uint16_t depth;
    uint64_t start_ticks;
    uint64_t end_ticks;
    char thread_name[PROFILER_THREAD_NAME_LEN];
    char section_name[PROFILER_SECTION_NAME_LEN];
} FrontendProfilerSelection;

static FrontendProfilerSelection frontend_profiler_selection = {0};

// Helper to get color from string
extern ImGuiIO *ioptr;

// --- Helper Functions ---

static ImU32 GetColorForString(const char *str)
{
    ImU32 hash = 0;
    while (*str)
    {
        hash = (hash << 5) + hash + (*str++);
    }

    hash = (hash ^ (hash >> 16)) * 0x85ebca6b;
    hash = (hash ^ (hash >> 13)) * 0xc2b2ae35;
    hash = (hash ^ (hash >> 16));

    unsigned char r = (hash & 0xFF0000) >> 16;
    unsigned char g = (hash & 0x00FF00) >> 8;
    unsigned char b = (hash & 0x0000FF);

    r = (r < 50) ? (r + 50) : r;
    g = (g < 50) ? (g + 50) : g;
    b = (b < 50) ? (b + 50) : b;

    ImVec4 color = {(float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, 1.0f};
    return igGetColorU32_Vec4(color);
}

static int Frontend_Profiler_GetMaxDepth(const ProfilerThreadFrame *thread)
{
    int max_depth = 0;
    if (!thread) {
        return 0;
    }

    for (uint32_t i = 0; i < thread->event_count; ++i)
    {
        if ((int)thread->events[i].depth > max_depth) {
            max_depth = (int)thread->events[i].depth;
        }
    }

    return max_depth;
}

static const char *Frontend_Profiler_SectionName(const Profiler *profiler, int section_id)
{
    if (!profiler || section_id < 0 || section_id >= profiler->num_sections) {
        return "Unknown";
    }

    return profiler->sections[section_id].name;
}

static double Frontend_Profiler_GetThreadDurationMS(const Profiler *profiler, const ProfilerFrameSnapshot *frame, const ProfilerThreadFrame *thread)
{
    if (!profiler || !frame || !thread || profiler->perf_freq == 0 || thread->event_count == 0) {
        return 0.0;
    }

    uint64_t min_start = UINT64_MAX;
    uint64_t max_end = 0;
    for (uint32_t event_index = 0; event_index < thread->event_count; ++event_index)
    {
        const ProfilerScopeEvent *event = &thread->events[event_index];
        if (event->end_ticks <= event->start_ticks) {
            continue;
        }

        if (event->start_ticks < min_start) {
            min_start = event->start_ticks;
        }
        if (event->end_ticks > max_end) {
            max_end = event->end_ticks;
        }
    }

    if (min_start == UINT64_MAX || max_end <= min_start) {
        return 0.0;
    }

    return (double)(max_end - min_start) * 1000.0 / (double)profiler->perf_freq;
}

static int Frontend_Profiler_ThreadPriority(const ProfilerThreadFrame *thread)
{
    if (!thread) {
        return 3;
    }

    if (strncmp(thread->name, "Main Thread", PROFILER_THREAD_NAME_LEN) == 0) {
        return 0;
    }
    if (strncmp(thread->name, "Emulation Thread", PROFILER_THREAD_NAME_LEN) == 0) {
        return 1;
    }

    return 2;
}

static bool Frontend_Profiler_ShouldSwapThreads(const ProfilerFrameSnapshot *frame, uint32_t left_index, uint32_t right_index)
{
    if (!frame || left_index >= frame->thread_count || right_index >= frame->thread_count) {
        return false;
    }

    const ProfilerThreadFrame *left = &frame->threads[left_index];
    const ProfilerThreadFrame *right = &frame->threads[right_index];

    int left_priority = Frontend_Profiler_ThreadPriority(left);
    int right_priority = Frontend_Profiler_ThreadPriority(right);
    if (left_priority != right_priority) {
        return left_priority > right_priority;
    }

    int name_cmp = strncmp(left->name, right->name, PROFILER_THREAD_NAME_LEN);
    if (name_cmp != 0) {
        return name_cmp > 0;
    }

    return left->thread_id > right->thread_id;
}

static uint32_t Frontend_Profiler_BuildThreadDisplayOrder(const ProfilerFrameSnapshot *frame, uint32_t *order_out, uint32_t max_order)
{
    if (!frame || !order_out || max_order == 0) {
        return 0;
    }

    uint32_t count = frame->thread_count;
    if (count > max_order) {
        count = max_order;
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        order_out[i] = i;
    }

    // Stable insertion sort to keep lane order deterministic frame-to-frame.
    for (uint32_t i = 1; i < count; ++i)
    {
        uint32_t key = order_out[i];
        int j = (int)i - 1;
        while (j >= 0 && Frontend_Profiler_ShouldSwapThreads(frame, order_out[j], key))
        {
            order_out[j + 1] = order_out[j];
            --j;
        }
        order_out[j + 1] = key;
    }

    return count;
}

static ImU32 Frontend_Profiler_TintColor(ImU32 color, float factor)
{
    unsigned char a = (unsigned char)((color >> 24) & 0xFF);
    unsigned char b = (unsigned char)((color >> 16) & 0xFF);
    unsigned char g = (unsigned char)((color >> 8) & 0xFF);
    unsigned char r = (unsigned char)(color & 0xFF);

    r = (unsigned char)SDL_clamp((int)(r * factor), 0, 255);
    g = (unsigned char)SDL_clamp((int)(g * factor), 0, 255);
    b = (unsigned char)SDL_clamp((int)(b * factor), 0, 255);

    return ((ImU32)a << 24) | ((ImU32)b << 16) | ((ImU32)g << 8) | (ImU32)r;
}

static void Frontend_Profiler_SelectEvent(uint32_t thread_index, uint32_t event_index, const ProfilerThreadFrame *thread, const ProfilerScopeEvent *event, const char *section_name)
{
    if (!thread || !event) {
        return;
    }

    frontend_profiler_selection.active = true;
    frontend_profiler_selection.thread_index = thread_index;
    frontend_profiler_selection.event_index = event_index;
    frontend_profiler_selection.section_id = event->section_id;
    frontend_profiler_selection.depth = event->depth;
    frontend_profiler_selection.start_ticks = event->start_ticks;
    frontend_profiler_selection.end_ticks = event->end_ticks;
    strncpy(frontend_profiler_selection.thread_name, thread->name[0] ? thread->name : "Thread", sizeof(frontend_profiler_selection.thread_name) - 1);
    frontend_profiler_selection.thread_name[sizeof(frontend_profiler_selection.thread_name) - 1] = '\0';
    strncpy(frontend_profiler_selection.section_name, section_name ? section_name : "Unknown", sizeof(frontend_profiler_selection.section_name) - 1);
    frontend_profiler_selection.section_name[sizeof(frontend_profiler_selection.section_name) - 1] = '\0';
}

static void Frontend_Profiler_DrawSelectionDetails(const Profiler *profiler, const ProfilerFrameSnapshot *frame)
{
    if (!frontend_profiler_selection.active)
    {
        igTextDisabled("Click a flame block to inspect it.");
        return;
    }

    if (!profiler || !frame || profiler->perf_freq == 0)
    {
        igTextDisabled("Selected item is not available right now.");
        return;
    }

    double duration_ms = (double)(frontend_profiler_selection.end_ticks - frontend_profiler_selection.start_ticks) * 1000.0 / (double)profiler->perf_freq;
    double start_ms = (double)(frontend_profiler_selection.start_ticks - frame->start_ticks) * 1000.0 / (double)profiler->perf_freq;
    double end_ms = (double)(frontend_profiler_selection.end_ticks - frame->start_ticks) * 1000.0 / (double)profiler->perf_freq;

    igText("Section: %s", frontend_profiler_selection.section_name);
    igText("Thread: %s", frontend_profiler_selection.thread_name);
    igText("Duration: %.3f ms", duration_ms);
    igText("Depth: %u", (unsigned)frontend_profiler_selection.depth);
    igText("Start: %.3f ms", start_ms);
    igText("End: %.3f ms", end_ms);
    if (igButton("Clear Selection", (ImVec2){120.0f, igGetFrameHeight()}))
    {
        memset(&frontend_profiler_selection, 0, sizeof(frontend_profiler_selection));
    }
}

// --- Public API ---

void FrontendProfiler_Init(void)
{
    Profiler_Init();
    Profiler_SetThreadName("Main Thread");
}

void FrontendProfiler_Step(NES *nes)
{
    if (!nes) {
        return;
    }
    if (frontend_profiler_ppu_section < 0) {
        frontend_profiler_ppu_section = Profiler_CreateSection("PPU Step");
    }
    if (frontend_profiler_cpu_section < 0) {
        frontend_profiler_cpu_section = Profiler_CreateSection("CPU Step");
    }
    if (frontend_profiler_nmi_section < 0) {
        frontend_profiler_nmi_section = Profiler_CreateSection("CPU NMI");
    }

    Profiler_BeginFrame();

    int current_frame = nes->ppu->frame_odd;

    while (current_frame == nes->ppu->frame_odd)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (frontend_profiler_ppu_section >= 0) {
                Profiler_BeginSectionByID(frontend_profiler_ppu_section);
            }
            PPU_Step(nes->ppu);
            if (frontend_profiler_ppu_section >= 0) {
                Profiler_EndSection(frontend_profiler_ppu_section);
            }
        }

        if (nes->ppu->nmi_interrupt_line)
        {
            if (frontend_profiler_nmi_section >= 0) {
                Profiler_BeginSectionByID(frontend_profiler_nmi_section);
            }
            CPU_NMI(nes->cpu);
            nes->ppu->nmi_interrupt_line = 0;
            if (frontend_profiler_nmi_section >= 0) {
                Profiler_EndSection(frontend_profiler_nmi_section);
            }
        }

        if (frontend_profiler_cpu_section >= 0) {
            Profiler_BeginSectionByID(frontend_profiler_cpu_section);
        }
        if (CPU_Step(nes->cpu) == -1)
        {
            DEBUG_ERROR("CPU execution halted due to error");
        }
        if (frontend_profiler_cpu_section >= 0) {
            Profiler_EndSection(frontend_profiler_cpu_section);
        }
    }

    Profiler_EndFrame();
}

void FrontendProfiler_DrawWindow(Profiler *profiler)
{
    if (!profiler || !frontend_showProfilerWindow) return;

    igSetNextWindowSize((ImVec2){600, 500}, ImGuiCond_FirstUseEver);
    if (igBegin("Profiler", &frontend_showProfilerWindow, ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus))
    {
        bool profiler_enabled = Profiler_IsEnabled();

        if (igCheckbox("Show Profiler", &profiler_enabled))
        {
            Profiler_Enable(profiler_enabled);
        }

        // Header info
        const ProfilerFrameSnapshot *frame = Profiler_GetPublishedFrame();
        uint32_t thread_count = Profiler_GetPublishedThreadCount();
        uint32_t thread_display_order[PROFILER_MAX_THREADS] = {0};
        uint32_t thread_display_count = Frontend_Profiler_BuildThreadDisplayOrder(frame, thread_display_order, PROFILER_MAX_THREADS);
        uint32_t event_count = 0;
        if (frame) {
            for (uint32_t i = 0; i < frame->thread_count; ++i) {
                event_count += frame->threads[i].event_count;
            }
        }

        igText("FPS: %.1f", profiler->current_fps);
        igSameLine(0, 20);
        igText("Frame: %.2f ms (Avg: %.2f ms, Max: %.2f ms)",
               profiler->current_frame_time_ms,
               profiler->avg_frame_time_ms,
               profiler->max_frame_time_ms);
        igText("Threads: %u | Events: %u | Sections: %d", thread_count, event_count, profiler->num_sections);

        // Frame time plot
        if (ImPlot_BeginPlot("Frame Times", (ImVec2){-1, 300}, ImPlotFlags_None))
        {
            ImPlot_SetupAxes("Frame Index", "Time (ms)", ImPlotAxisFlags_Lock, ImPlotAxisFlags_AutoFit);
            ImPlot_SetupAxisLimits(ImAxis_X1, 0, PROFILER_HISTORY_SIZE - 1, ImGuiCond_Always);

            float x_values[PROFILER_HISTORY_SIZE];
            float frame_times_float[PROFILER_HISTORY_SIZE];
            
            for (int i = 0; i < PROFILER_HISTORY_SIZE; i++) {
                x_values[i] = (float)i;
                int data_idx = (profiler->frame_history_idx + i) % PROFILER_HISTORY_SIZE;
                frame_times_float[i] = (float)profiler->frame_times[data_idx];
            }

            ImPlot_PlotLine_FloatPtrFloatPtr("Frame Time", x_values, frame_times_float, 
                                           PROFILER_HISTORY_SIZE, 0, 0, sizeof(float));
            ImPlot_EndPlot();
        }

        igSeparator();
        if (igBeginChild_Str("ProfilerFlame", (ImVec2){0, 320}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar))
        {
            ImDrawList *draw_list = igGetWindowDrawList();
            ImVec2 graph_pos = {0};
            ImVec2 graph_avail = {0};

            static float profiler_timeline_zoom = 1.0f;
            static bool profiler_auto_follow = false;

            igText("Timeline Zoom");
            igSameLine(0, 8);
            igSetNextItemWidth(160.0f);
            igSliderFloat("##ProfilerTimelineZoom", &profiler_timeline_zoom, 0.25f, 4.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
            igSameLine(0, 8);
            igCheckbox("Auto follow latest", &profiler_auto_follow);
            igSameLine(0, 8);
            if (igButton("Reset View", (ImVec2){110.0f, igGetFrameHeight()}))
            {
                profiler_timeline_zoom = 1.0f;
                profiler_auto_follow = true;
                igSetScrollX_Float(0.0f);
                igSetScrollY_Float(0.0f);
            }

            if (ioptr && igIsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && !igIsAnyItemActive())
            {
                if (igIsMouseDragging(ImGuiMouseButton_Right, 0.0f))
                {
                    ImVec2 drag_delta = ioptr->MouseDelta;
                    igSetScrollX_Float(igGetScrollX() - drag_delta.x);
                    igSetScrollY_Float(igGetScrollY() - drag_delta.y);
                    profiler_auto_follow = false;
                }
            }

            // Move to a fresh line so the canvas starts at the left edge, not after the inline controls.
            igNewLine();

            // The flamegraph canvas starts below the controls; capture origin after UI widgets are laid out.
            igGetCursorScreenPos(&graph_pos);
            igGetContentRegionAvail(&graph_avail);

            float label_width = 132.0f;
            float header_height = 24.0f;
            float block_height = igGetTextLineHeight() + igGetStyle()->FramePadding.y * 2.0f;
            float row_gap = 10.0f;
            float content_width = graph_avail.x;
            float max_thread_timeline_width = 0.0f;

            if (frame && thread_display_count > 0)
            {
                for (uint32_t display_index = 0; display_index < thread_display_count; ++display_index)
                {
                    uint32_t thread_index = thread_display_order[display_index];
                    double thread_duration_ms = Frontend_Profiler_GetThreadDurationMS(profiler, frame, &frame->threads[thread_index]);
                    float timeline_px_per_ms = 120.0f * profiler_timeline_zoom;
                    float thread_timeline_width = (thread_duration_ms > 0.0001) ? (float)thread_duration_ms * timeline_px_per_ms : 220.0f;
                    if (thread_timeline_width < 220.0f) {
                        thread_timeline_width = 220.0f;
                    }
                    if (thread_timeline_width > max_thread_timeline_width) {
                        max_thread_timeline_width = thread_timeline_width;
                    }
                }

                content_width = label_width + max_thread_timeline_width + 24.0f;
                if (content_width < graph_avail.x) {
                    content_width = graph_avail.x;
                }
            }

            float total_content_height = header_height;
            if (frame && thread_display_count > 0)
            {
                for (uint32_t display_index = 0; display_index < thread_display_count; ++display_index)
                {
                    uint32_t thread_index = thread_display_order[display_index];
                    total_content_height += header_height + ((float)Frontend_Profiler_GetMaxDepth(&frame->threads[thread_index]) + 1.0f) * (block_height + 4.0f) + row_gap;
                }
                total_content_height += 28.0f;
            }

            if (!frame || frame->thread_count == 0 || frame->duration_ms <= 0.0001)
            {
                igTextDisabled("No profiler data captured yet.");
            }
            else
            {
                float y_offset = graph_pos.y + header_height;

                for (uint32_t display_index = 0; display_index < thread_display_count; ++display_index)
                {
                    uint32_t thread_index = thread_display_order[display_index];
                    const ProfilerThreadFrame *thread = &frame->threads[thread_index];
                    int max_depth = Frontend_Profiler_GetMaxDepth(thread);
                    float row_height = header_height + ((float)max_depth + 1.0f) * (block_height + 4.0f) + 8.0f;
                    double thread_duration_ms = Frontend_Profiler_GetThreadDurationMS(profiler, frame, thread);
                    float timeline_width = (thread_duration_ms > 0.0001) ? (float)thread_duration_ms * 120.0f * profiler_timeline_zoom : 220.0f;
                    if (timeline_width < 220.0f) {
                        timeline_width = 220.0f;
                    }
                    double scale = timeline_width > 1.0f ? timeline_width / (thread_duration_ms > 0.0001 ? thread_duration_ms : 1.0) : 1.0;
                    uint64_t thread_min_start = UINT64_MAX;
                    uint64_t thread_max_end = 0;
                    for (uint32_t event_index = 0; event_index < thread->event_count; ++event_index)
                    {
                        const ProfilerScopeEvent *event = &thread->events[event_index];
                        if (event->end_ticks <= event->start_ticks) {
                            continue;
                        }
                        if (event->start_ticks < thread_min_start) {
                            thread_min_start = event->start_ticks;
                        }
                        if (event->end_ticks > thread_max_end) {
                            thread_max_end = event->end_ticks;
                        }
                    }
                    if (thread_min_start == UINT64_MAX || thread_max_end <= thread_min_start) {
                        continue;
                    }

                    ImVec2 row_min = {graph_pos.x, y_offset};
                    ImVec2 row_max = {graph_pos.x + content_width, y_offset + row_height};
                    ImU32 row_bg = igGetColorU32_Col(ImGuiCol_ChildBg, (display_index % 2 == 0) ? 1.0f : 0.82f);
                    ImDrawList_AddRectFilled(draw_list, row_min, row_max, row_bg, 6.0f, ImDrawFlags_RoundCornersAll);
                    ImDrawList_AddRect(draw_list, row_min, row_max, igGetColorU32_Col(ImGuiCol_Border, 0.9f), 6.0f, 0, 1.0f);

                    ImVec2 label_pos = {row_min.x + 10.0f, row_min.y + 4.0f};
                    ImDrawList_AddText_Vec2(draw_list, label_pos, igGetColorU32_Col(ImGuiCol_Text, 1.0f), thread->name[0] ? thread->name : "Thread", NULL);

                    char meta_text[96];
                    snprintf(meta_text, sizeof(meta_text), "%u events", thread->event_count);
                    ImDrawList_AddText_Vec2(draw_list, (ImVec2){row_min.x + label_width - 72.0f, row_min.y + 4.0f}, igGetColorU32_Col(ImGuiCol_TextDisabled, 1.0f), meta_text, NULL);

                    for (uint32_t event_index = 0; event_index < thread->event_count; ++event_index)
                    {
                        const ProfilerScopeEvent *event = &thread->events[event_index];
                        if (event->end_ticks <= event->start_ticks) {
                            continue;
                        }

                        const char *section_name = Frontend_Profiler_SectionName(profiler, event->section_id);
                        double start_ms = (double)(event->start_ticks - thread_min_start) * 1000.0 / (double)profiler->perf_freq;
                        double end_ms = (double)(event->end_ticks - thread_min_start) * 1000.0 / (double)profiler->perf_freq;

                        float x0 = row_min.x + label_width + (float)start_ms * (float)scale;
                        float x1 = row_min.x + label_width + (float)end_ms * (float)scale;
                        float y0 = row_min.y + header_height + (float)event->depth * (block_height + 4.0f);
                        float y1 = y0 + block_height;

                        if (x1 <= x0 + 1.0f) {
                            x1 = x0 + 1.0f;
                        }

                        if (x1 < row_min.x + label_width || x0 > row_max.x) {
                            continue;
                        }

                        x0 = row_min.x + label_width + (float)((event->start_ticks - thread_min_start) * 1000.0 / (double)profiler->perf_freq) * (float)scale;
                        x1 = row_min.x + label_width + (float)((event->end_ticks - thread_min_start) * 1000.0 / (double)profiler->perf_freq) * (float)scale;

                        bool selected = frontend_profiler_selection.active &&
                                        frontend_profiler_selection.thread_index == thread_index &&
                                        frontend_profiler_selection.event_index == event_index;

                        ImU32 fill_color = Frontend_Profiler_TintColor(GetColorForString(section_name), selected ? 1.25f : 0.92f);
                        ImU32 border_color = selected ? igGetColorU32_Col(ImGuiCol_Text, 1.0f) : Frontend_Profiler_TintColor(fill_color, 0.75f);
                        ImVec2 bar_min = {x0, y0};
                        ImVec2 bar_max = {x1, y1};

                        ImDrawList_AddRectFilled(draw_list, bar_min, bar_max, fill_color, 4.0f, ImDrawFlags_RoundCornersAll);
                        ImDrawList_AddRect(draw_list, bar_min, bar_max, border_color, 4.0f, 0, selected ? 2.0f : 1.0f);

                        ImVec2 text_size;
                        igCalcTextSize(&text_size, section_name, NULL, false, 0.0f);
                        if ((bar_max.x - bar_min.x) > text_size.x + 6.0f) {
                            ImDrawList_AddText_Vec2(draw_list, (ImVec2){bar_min.x + 3.0f, bar_min.y + (block_height - text_size.y) * 0.5f}, igGetColorU32_Col(ImGuiCol_Text, 1.0f), section_name, NULL);
                        }

                        char event_id[64];
                        snprintf(event_id, sizeof(event_id), "##prof_evt_%u_%u", thread_index, event_index);
                        igSetCursorScreenPos(bar_min);
                        igPushID_Int((int)thread_index);
                        igPushID_Int((int)event_index);
                        float clickable_width = bar_max.x - bar_min.x;
                        float clickable_height = bar_max.y - bar_min.y;
                        if (clickable_width < 1.0f) {
                            clickable_width = 1.0f;
                        }
                        if (clickable_height < 1.0f) {
                            clickable_height = 1.0f;
                        }
                        igInvisibleButton(event_id, (ImVec2){clickable_width, clickable_height}, 0);
                        if (igIsItemHovered(ImGuiHoveredFlags_None)) {
                            igBeginTooltip();
                            igText("%s", section_name);
                            igText("Thread: %s", thread->name[0] ? thread->name : "Thread");
                            igText("Duration: %.3f ms", end_ms - start_ms);
                            igText("Depth: %u", (unsigned)event->depth);
                            igText("Start: %.3f ms", start_ms);
                            igEndTooltip();
                        }
                        if (igIsItemClicked(ImGuiMouseButton_Left)) {
                            Frontend_Profiler_SelectEvent(thread_index, event_index, thread, event, section_name);
                        }
                        igPopID();
                        igPopID();
                    }

                    igDummy((ImVec2){content_width, row_height + row_gap});
                    y_offset += row_height + row_gap;
                }

                if (profiler_auto_follow)
                {
                    float target_x = content_width - graph_avail.x;
                    if (target_x < 0.0f) {
                        target_x = 0.0f;
                    }
                    igSetScrollX_Float(target_x);
                }
            }

            igEndChild();
        }

        igSeparator();
        igText("Section Statistics:");

        if (igBeginTable("SectionsTable", 7,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Sortable,
                        (ImVec2){0, 0}, 0))
        {
            igTableSetupColumn("Section", ImGuiTableColumnFlags_WidthStretch, 0, 0);
            igTableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 50, 1);
            igTableSetupColumn("Total (ms)", ImGuiTableColumnFlags_WidthFixed, 80, 2);
            igTableSetupColumn("Avg (ms)", ImGuiTableColumnFlags_WidthFixed, 70, 3);
            igTableSetupColumn("Max (ms)", ImGuiTableColumnFlags_WidthFixed, 70, 4);
            igTableSetupColumn("Last (ms)", ImGuiTableColumnFlags_WidthFixed, 70, 5);
            igTableSetupColumn("Share", ImGuiTableColumnFlags_WidthFixed, 60, 6);
            igTableHeadersRow();

            for (int i = 0; i < profiler->num_sections; i++) {
                const ProfilerSection *sec = &profiler->sections[i];
                double avg_ms = (sec->call_count_this_frame > 0) ? (sec->total_time_this_frame_ms / (double)sec->call_count_this_frame) : 0.0;
                double share = (frame && frame->duration_ms > 0.0001) ? (sec->total_time_this_frame_ms / frame->duration_ms) * 100.0 : 0.0;

                igTableNextRow(0, 0);
                igTableSetColumnIndex(0); igText("%s", sec->name);
                igTableSetColumnIndex(1); igText("%d", sec->call_count_this_frame);
                igTableSetColumnIndex(2); igText("%.3f", sec->total_time_this_frame_ms);
                igTableSetColumnIndex(3); igText("%.3f", avg_ms);
                igTableSetColumnIndex(4); igText("%.3f", sec->max_time_this_frame_ms);
                igTableSetColumnIndex(5); igText("%.3f", sec->last_time_this_frame_ms);
                igTableSetColumnIndex(6); igText("%.1f%%", share);
            }
            igEndTable();
        }

        igSeparator();
        igText("Selected Event:");
        Frontend_Profiler_DrawSelectionDetails(profiler, frame);
    }
    igEnd();
}
