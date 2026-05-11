#ifndef FRONTEND_PROFILER_H
#define FRONTEND_PROFILER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Profiler Profiler;
typedef struct ProfilerThreadFrame ProfilerThreadFrame;
typedef struct ProfilerScopeEvent ProfilerScopeEvent;

typedef struct NES NES;
// Initialize frontend profiler
void FrontendProfiler_Init(void);


// Step function for emulation loop - handles profiler sections for CPU/PPU/NMI
void FrontendProfiler_Step(NES *nes);
// Draw the profiler window
void FrontendProfiler_DrawWindow(Profiler *profiler);

#endif // FRONTEND_PROFILER_H
