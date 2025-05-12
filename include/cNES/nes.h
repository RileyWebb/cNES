#ifndef NES_H
#define NES_H

#include <stdint.h>
#include <stddef.h>

typedef struct CPU CPU;
typedef struct PPU PPU;
typedef struct BUS BUS;

typedef struct NES {
    CPU* cpu; // Pointer to the CPU
    PPU* ppu; // Pointer to the PPU
    BUS* bus; // Pointer to the BUS
} NES;

NES *NES_Create();
int NES_Load(const char* path, NES* nes);
void NES_Destroy(NES* nes);

void NES_Step(NES *nes);

#endif // NES_H
