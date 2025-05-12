#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stddef.h>

typedef struct NES NES;

typedef enum {
    CPU_FLAG_CARRY     = (1 << 0), // Carry Flag (C)
    CPU_FLAG_ZERO      = (1 << 1), // Zero Flag (Z)
    CPU_FLAG_INTERRUPT = (1 << 2), // Interrupt Disable Flag (I)
    CPU_FLAG_DECIMAL   = (1 << 3), // Decimal Mode Flag (D) (unused in NES)
    CPU_FLAG_BREAK     = (1 << 4), // Break Command Flag (B)
    CPU_FLAG_UNUSED    = (1 << 5), // Unused flag (always 1)
    CPU_FLAG_OVERFLOW  = (1 << 6), // Overflow Flag (V)
    CPU_FLAG_NEGATIVE  = (1 << 7), // Negative Flag (N)
} CPU_StatusFlags;

typedef struct CPU_Opcode {
    enum {
        CPU_MODE_IMPLIED,
        CPU_MODE_ACCUMULATOR,
        CPU_MODE_IMMEDIATE,
        CPU_MODE_ZERO_PAGE,
        CPU_MODE_ZERO_PAGE_X,
        CPU_MODE_ZERO_PAGE_Y,
        CPU_MODE_RELATIVE,
        CPU_MODE_ABSOLUTE,
        CPU_MODE_ABSOLUTE_X,
        CPU_MODE_ABSOLUTE_Y,
        CPU_MODE_INDIRECT,
        CPU_MODE_INDEXED_INDIRECT,
        CPU_MODE_INDIRECT_INDEXED
    } addressing_mode;
    const char mnemonic[5];
    uint8_t cycles;
} CPU_Opcode;

typedef struct CPU {
    // Registers
    uint8_t a;  // Accumulator
    uint8_t x;  // X Register
    uint8_t y;  // Y Register
    uint8_t sp; // Stack Pointer
    uint16_t pc; // Program Counter
    uint8_t status; // Processor Status

    uint64_t total_cycles;

    NES* nes; // Pointer to the NES instance
} CPU;

extern CPU_Opcode cpu_opcodes[256];

CPU *CPU_Create(NES *nes);
void CPU_Reset(CPU* cpu);
int CPU_Step(CPU* cpu);

void CPU_Interupt(CPU* cpu);
void CPU_NMI(CPU* cpu);

// Stack operations
void CPU_Push(CPU* cpu, uint8_t value);
void CPU_Push16(CPU* cpu, uint16_t value);
uint8_t CPU_Pop(CPU* cpu);
uint16_t CPU_Pop16(CPU* cpu);

// Status flag helper functions
void CPU_SetFlag(CPU* cpu, uint8_t flag, int value);
uint8_t CPU_GetFlag(CPU* cpu, uint8_t flag);
void CPU_UpdateZeroNegativeFlags(CPU* cpu, uint8_t value);
void CPU_SetNegativeFlag(CPU* cpu, uint8_t value);

// Addressing modes
uint16_t CPU_Immediate(CPU* cpu);
uint16_t CPU_Accumulator(CPU* cpu);
uint16_t CPU_Implied(CPU* cpu);
uint16_t CPU_ZeroPage(CPU* cpu);
uint16_t CPU_ZeroPageX(CPU* cpu);
uint16_t CPU_ZeroPageY(CPU* cpu);
uint16_t CPU_Relative(CPU* cpu);
uint16_t CPU_Absolute(CPU* cpu);
uint16_t CPU_AbsoluteX(CPU* cpu);
uint16_t CPU_AbsoluteY(CPU* cpu);
uint16_t CPU_Indirect(CPU* cpu);
uint16_t CPU_IndexedIndirect(CPU* cpu);
uint16_t CPU_IndirectIndexed(CPU* cpu);

#endif // CPU_H
