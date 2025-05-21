#ifndef NES_H
#define NES_H

#include <stdint.h>
#include <stddef.h>

typedef struct CPU CPU;
typedef struct PPU PPU;
typedef struct BUS BUS;
typedef struct ROM ROM;
//typedef struct Profiler Profiler;

typedef struct NES {
    CPU* cpu; // Pointer to the CPU
    PPU* ppu; // Pointer to the PPU
    BUS* bus; // Pointer to the BUS
    ROM* rom; // Pointer to the ROM

    uint8_t controllers[2]; // Two NES controllers
    uint8_t controller_strobe; // Strobe flag for controllers
    uint8_t controller_shift[2]; // Shift registers for controllers

    //Profiler *profiler;
} NES;

NES *NES_Create();
int NES_Load(const char* path, NES* nes);
void NES_Destroy(NES* nes);

void NES_StepFrame(NES *nes);
void NES_Step(NES *nes);
void NES_Reset(NES *nes);

// Poll controller state (UI or platform layer should implement this and NES core should call it)
uint8_t NES_PollController(NES* nes, int controller);

// Set controller state (for UI or platform layer to update controller state in NES struct)
void NES_SetController(NES* nes, int controller, uint8_t state);

#endif // NES_H
