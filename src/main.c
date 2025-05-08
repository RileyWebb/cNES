//#include <SDL2/SDL.h>
//#define SDL_MAIN_HANDLED


//SDL_Window *window;
//SDL_GLContext *glContext;

//ImGuiIO* ioptr;

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"

#include "ui.h"
#include "cNES/cpu.h"
#include "cNES/bus.h"
#include "cNES/nes.h"

#define NESTEST_COMPLETION_PC 0xC6A0
#define MAX_NESTEST_INSTRUCTIONS 30000

int main(int argc, char **argv)
{
    FILE *f_log = fopen("log.txt", "w");
    D_LogRegister(f_log);

    DEBUG_INFO("Starting cNES");

    UI_Init();

    NES* nes = NES_Create();

    NES_Load("tests/nestest.nes", nes);

    //CPU_Reset(cpu); // Reset CPU to initial state
    nes->cpu->pc = 0xC000;
    nes->cpu->status = 0x24; // Explicitly match P=24 state if tests rely on it
    nes->cpu->total_cycles = 7; // Start cycles if matching logs, 0 otherwise 

    for (;;)
    {
        UI_Update(nes);
    }

    DEBUG_INFO("Closing cNES");
    fclose(f_log);

    return 0;
}