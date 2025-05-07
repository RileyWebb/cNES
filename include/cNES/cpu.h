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

typedef struct CPU {
    // Registers
    uint8_t a;  // Accumulator
    uint8_t x;  // X Register
    uint8_t y;  // Y Register
    uint8_t sp; // Stack Pointer
    uint16_t pc; // Program Counter
    uint8_t status; // Processor Status

    // Memory
    uint64_t total_cycles;

    NES* nes; // Pointer to the NES instance
} CPU;

//void CPU_Init(CPU* cpu);
void CPU_Reset(CPU* cpu);
int CPU_Step(CPU* cpu);

void CPU_Interupt(CPU* cpu);
void CPU_NMInterupt(CPU* cpu);

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

// Opcodes
// Load/Store Operations
void CPU_LDA(CPU* cpu, uint16_t address);
void CPU_LDX(CPU* cpu, uint16_t address);
void CPU_LDY(CPU* cpu, uint16_t address);
void CPU_STA(CPU* cpu, uint16_t address);
void CPU_STX(CPU* cpu, uint16_t address);
void CPU_STY(CPU* cpu, uint16_t address);

// Register Transfer Operations
void CPU_TAX(CPU* cpu);
void CPU_TAY(CPU* cpu);
void CPU_TXA(CPU* cpu);
void CPU_TYA(CPU* cpu);
void CPU_TSX(CPU* cpu);
void CPU_TXS(CPU* cpu);

// Stack Operations
void CPU_PHA(CPU* cpu);
void CPU_PLA(CPU* cpu);
void CPU_PHP(CPU* cpu);
void CPU_PLP(CPU* cpu);

// Decrement/Increment Operations
void CPU_INC(CPU* cpu, uint16_t address);
void CPU_DEC(CPU* cpu, uint16_t address);
void CPU_INX(CPU* cpu);
void CPU_INY(CPU* cpu);
void CPU_DEX(CPU* cpu);
void CPU_DEY(CPU* cpu);

// Arithmetic Operations
void CPU_ADC(CPU* cpu, uint16_t address);
void CPU_SBC(CPU* cpu, uint16_t address);



void CPU_NOP(CPU* cpu);

void CPU_JMP(CPU* cpu, uint16_t address);
void CPU_JSR(CPU* cpu, uint16_t address);

/* TODO: ADD THE REST OF THE OPCODES */

#endif // CPU_H
