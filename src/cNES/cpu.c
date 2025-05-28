#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "debug.h"
#include "cNES/nes.h"
#include "cNES/bus.h"
#include "cNES/ppu.h"

#include "cNES/cpu.h"

// Function pointer types for operations and addressing modes
typedef uint16_t (*addr_mode_func_ptr)(CPU *cpu, bool *page_crossed);
typedef void (*op_func_ptr)(CPU *cpu, uint16_t address, uint8_t *cycles_ref);

// Structure for an instruction in the lookup table
typedef struct {
    op_func_ptr operation;              // Pointer to the CPU operation function
    addr_mode_func_ptr addressing_mode;  // Pointer to the addressing mode function
    uint8_t cycles;                     // Base CPU cycles for the instruction
    uint8_t add_cycles_on_page_cross;   // 1 if read op with page-crossing addr mode adds cycle, 0 otherwise
} Instruction;

// --- Helper Functions (static inline) ---
static inline void CPU_Push(CPU *cpu, uint8_t value) {
    // Stack is at $0100-$01FF. SP is an offset.
    cpu->nes->bus->memory[0x0100 + cpu->sp] = value;
    cpu->sp = (cpu->sp - 1) & 0xFF; // Decrement stack pointer and wrap at 0xFF
}

static inline uint8_t CPU_Pop(CPU *cpu) {
    cpu->sp = (cpu->sp + 1) & 0xFF; // Increment stack pointer and wrap at 0xFF
    return cpu->nes->bus->memory[0x0100 + cpu->sp];
}

static inline void CPU_Push16(CPU *cpu, uint16_t value) {
    CPU_Push(cpu, (uint8_t)(value >> 8));   // High byte
    CPU_Push(cpu, (uint8_t)(value & 0xFF)); // Low byte
}

static inline uint16_t CPU_Pop16(CPU *cpu) {
    uint8_t lo = CPU_Pop(cpu);
    uint8_t hi = CPU_Pop(cpu);
    return (uint16_t)lo | ((uint16_t)hi << 8);
}

static inline void CPU_UpdateZeroNegativeFlags(CPU *cpu, uint8_t value) {
    CPU_SetFlag(cpu, CPU_FLAG_ZERO, value == 0);
    CPU_SetFlag(cpu, CPU_FLAG_NEGATIVE, (value & 0x80) != 0);
}

// --- Addressing Modes (static inline) ---
static inline uint16_t CPU_ADDR_IMP(CPU *cpu, bool *page_crossed) { (void)cpu; *page_crossed = false; return 0; }
static inline uint16_t CPU_ADDR_ACC(CPU *cpu, bool *page_crossed) { (void)cpu; *page_crossed = false; return 0; }
static inline uint16_t CPU_ADDR_IMM(CPU *cpu, bool *page_crossed) { *page_crossed = false; return cpu->pc++; }
static inline uint16_t CPU_ADDR_ZP(CPU *cpu, bool *page_crossed) { *page_crossed = false; return BUS_Read(cpu->nes, cpu->pc++); }
static inline uint16_t CPU_ADDR_ZPX(CPU *cpu, bool *page_crossed) { *page_crossed = false; return (BUS_Read(cpu->nes, cpu->pc++) + cpu->x) & 0xFF; }
static inline uint16_t CPU_ADDR_ZPY(CPU *cpu, bool *page_crossed) { *page_crossed = false; return (BUS_Read(cpu->nes, cpu->pc++) + cpu->y) & 0xFF; }
static inline uint16_t CPU_ADDR_REL(CPU *cpu, bool *page_crossed) { *page_crossed = false; uint16_t offset_addr = cpu->pc++; int8_t offset = (int8_t)BUS_Read(cpu->nes, offset_addr); return cpu->pc + (uint16_t)offset; }
static inline uint16_t CPU_ADDR_ABS(CPU *cpu, bool *page_crossed) { *page_crossed = false; uint16_t address = BUS_Read16(cpu->nes, cpu->pc); cpu->pc += 2; return address; }
static inline uint16_t CPU_ADDR_ABSX(CPU *cpu, bool *page_crossed) { uint16_t base_addr = BUS_Read16(cpu->nes, cpu->pc); cpu->pc += 2; uint16_t final_addr = base_addr + cpu->x; *page_crossed = ((base_addr & 0xFF00) != (final_addr & 0xFF00)); return final_addr; }
static inline uint16_t CPU_ADDR_ABSY(CPU *cpu, bool *page_crossed) { uint16_t base_addr = BUS_Read16(cpu->nes, cpu->pc); cpu->pc += 2; uint16_t final_addr = base_addr + cpu->y; *page_crossed = ((base_addr & 0xFF00) != (final_addr & 0xFF00)); return final_addr; }
static inline uint16_t CPU_ADDR_IND(CPU *cpu, bool *page_crossed) { *page_crossed = false; uint16_t ptr_addr = BUS_Read16(cpu->nes, cpu->pc); cpu->pc += 2; uint16_t effective_addr_lo = BUS_Read(cpu->nes, ptr_addr); uint16_t effective_addr_hi_addr; if ((ptr_addr & 0x00FF) == 0x00FF) { effective_addr_hi_addr = ptr_addr & 0xFF00; } else { effective_addr_hi_addr = ptr_addr + 1; } uint16_t effective_addr_hi = BUS_Read(cpu->nes, effective_addr_hi_addr); return (effective_addr_hi << 8) | effective_addr_lo; }
static inline uint16_t CPU_ADDR_IZX(CPU *cpu, bool *page_crossed) { *page_crossed = false; uint8_t zp_addr_base = BUS_Read(cpu->nes, cpu->pc++); uint8_t zp_addr = (zp_addr_base + cpu->x) & 0xFF; uint16_t effective_addr_lo = BUS_Read(cpu->nes, zp_addr); uint16_t effective_addr_hi = BUS_Read(cpu->nes, (zp_addr + 1) & 0xFF); return (effective_addr_hi << 8) | effective_addr_lo; }
static inline uint16_t CPU_ADDR_IZY(CPU *cpu, bool *page_crossed) { uint8_t zp_addr = BUS_Read(cpu->nes, cpu->pc++); uint16_t base_addr_lo = BUS_Read(cpu->nes, zp_addr); uint16_t base_addr_hi = BUS_Read(cpu->nes, (zp_addr + 1) & 0xFF); uint16_t base_addr = (base_addr_hi << 8) | base_addr_lo; uint16_t final_addr = base_addr + cpu->y; *page_crossed = ((base_addr & 0xFF00) != (final_addr & 0xFF00)); return final_addr; }

// Official
static void CPU_OP_ADC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_AND(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ASL_A(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ASL_MEM(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BCC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BCS(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BEQ(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BIT(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BMI(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BNE(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BPL(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BRK(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BVC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BVS(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_CLC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_CLD(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_CLI(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_CLV(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_CMP(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_CPX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_CPY(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_DEC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_DEX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_DEY(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_EOR(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_INC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_INX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_INY(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_JMP(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_JSR(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_LDA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_LDX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_LDY(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_LSR_A(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_LSR_MEM(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_NOP(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ORA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_PHA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_PHP(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_PLA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_PLP(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ROL_A(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ROL_MEM(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ROR_A(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ROR_MEM(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_RTI(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_RTS(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_SBC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_SEC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_SED(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_SEI(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_STA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_STX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_STY(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_TAX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_TAY(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_TSX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_TXA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_TXS(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_TYA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);

// Unofficial / Illegal Opcodes
static void CPU_OP_KIL(CPU *cpu, uint16_t address, uint8_t *cycles_ref); // Halts CPU
static void CPU_OP_SLO(CPU *cpu, uint16_t address, uint8_t *cycles_ref); // ASL + ORA
static void CPU_OP_RLA(CPU *cpu, uint16_t address, uint8_t *cycles_ref); // ROL + AND
static void CPU_OP_SRE(CPU *cpu, uint16_t address, uint8_t *cycles_ref); // LSR + EOR
static void CPU_OP_RRA(CPU *cpu, uint16_t address, uint8_t *cycles_ref); // ROR + ADC
static void CPU_OP_SAX(CPU *cpu, uint16_t address, uint8_t *cycles_ref); // Store A & X
static void CPU_OP_LAX(CPU *cpu, uint16_t address, uint8_t *cycles_ref); // LDA + LDX (or LDA + TAX)
static void CPU_OP_DCP(CPU *cpu, uint16_t address, uint8_t *cycles_ref); // DEC + CMP
static void CPU_OP_ISC(CPU *cpu, uint16_t address, uint8_t *cycles_ref); // INC + SBC
static void CPU_OP_ANC(CPU *cpu, uint16_t address, uint8_t *cycles_ref); // AND + set C as N
static void CPU_OP_ALR(CPU *cpu, uint16_t address, uint8_t *cycles_ref); // AND + LSR
static void CPU_OP_ARR(CPU *cpu, uint16_t address, uint8_t *cycles_ref); // AND + ROR, special flags
static void CPU_OP_SBX(CPU *cpu, uint16_t address, uint8_t *cycles_ref); // (A & X) - operand -> X

static void CPU_OP_SHX(CPU *cpu, uint16_t address, uint8_t *cycles_ref); // Store X & (HighByte(Address) + 1)
static void CPU_OP_SHY(CPU *cpu, uint16_t address, uint8_t *cycles_ref); // Store Y & (HighByte(Address) + 1)
static void CPU_OP_TAS(CPU *cpu, uint16_t address, uint8_t *cycles_ref); // Store A & X -> SP; (A & X & (HighByte(Address) + 1)) -> Address (unstable) - Simplified to SP = A & X
static void CPU_OP_LAS(CPU *cpu, uint16_t address, uint8_t *cycles_ref); // Load (Memory & SP) -> A, X, SP
static const Instruction instruction_lookup[256] = {
    // 0x00 - 0x0F
    {CPU_OP_BRK, CPU_ADDR_IMP,  7, 0},
    {CPU_OP_ORA, CPU_ADDR_IZX,  6, 0},
    {CPU_OP_KIL, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_SLO, CPU_ADDR_IZX,  8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_ORA, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_ASL_MEM, CPU_ADDR_ZP,   5, 0},
    {CPU_OP_SLO, CPU_ADDR_ZP,   5, 0},
    {CPU_OP_PHP, CPU_ADDR_IMP,  3, 0},
    {CPU_OP_ORA, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_ASL_A, CPU_ADDR_ACC,  2, 0},
    {CPU_OP_ANC, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_NOP, CPU_ADDR_ABS,  4, 0},
    {CPU_OP_ORA, CPU_ADDR_ABS,  4, 0},
    {CPU_OP_ASL_MEM, CPU_ADDR_ABS,  6, 0},
    {CPU_OP_SLO, CPU_ADDR_ABS,  6, 0},

    // 0x10 - 0x1F
    {CPU_OP_BPL, CPU_ADDR_REL,  2, 0},
    {CPU_OP_ORA, CPU_ADDR_IZY,  5, 1},
    {CPU_OP_KIL, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_SLO, CPU_ADDR_IZY,  8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZPX,  4, 0},
    {CPU_OP_ORA, CPU_ADDR_ZPX,  4, 0},
    {CPU_OP_ASL_MEM, CPU_ADDR_ZPX,  6, 0},
    {CPU_OP_SLO, CPU_ADDR_ZPX,  6, 0},
    {CPU_OP_CLC, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_ORA, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_NOP, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_SLO, CPU_ADDR_ABSY, 7, 0},
    {CPU_OP_NOP, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_ORA, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_ASL_MEM, CPU_ADDR_ABSX, 7, 0},
    {CPU_OP_SLO, CPU_ADDR_ABSX, 7, 0},

    // 0x20 - 0x2F
    {CPU_OP_JSR, CPU_ADDR_ABS,  6, 0},
    {CPU_OP_AND, CPU_ADDR_IZX,  6, 0},
    {CPU_OP_KIL, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_RLA, CPU_ADDR_IZX,  8, 0},
    {CPU_OP_BIT, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_AND, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_ROL_MEM, CPU_ADDR_ZP,   5, 0},
    {CPU_OP_RLA, CPU_ADDR_ZP,   5, 0},
    {CPU_OP_PLP, CPU_ADDR_IMP,  4, 0},
    {CPU_OP_AND, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_ROL_A, CPU_ADDR_ACC,  2, 0},
    {CPU_OP_ANC, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_BIT, CPU_ADDR_ABS,  4, 0},
    {CPU_OP_AND, CPU_ADDR_ABS,  4, 0},
    {CPU_OP_ROL_MEM, CPU_ADDR_ABS,  6, 0},
    {CPU_OP_RLA, CPU_ADDR_ABS,  6, 0},

    // 0x30 - 0x3F
    {CPU_OP_BMI, CPU_ADDR_REL,  2, 0},
    {CPU_OP_AND, CPU_ADDR_IZY,  5, 1},
    {CPU_OP_KIL, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_RLA, CPU_ADDR_IZY,  8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZPX,  4, 0},
    {CPU_OP_AND, CPU_ADDR_ZPX,  4, 0},
    {CPU_OP_ROL_MEM, CPU_ADDR_ZPX,  6, 0},
    {CPU_OP_RLA, CPU_ADDR_ZPX,  6, 0},
    {CPU_OP_SEC, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_AND, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_NOP, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_RLA, CPU_ADDR_ABSY, 7, 0},
    {CPU_OP_NOP, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_AND, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_ROL_MEM, CPU_ADDR_ABSX, 7, 0},
    {CPU_OP_RLA, CPU_ADDR_ABSX, 7, 0},

    // 0x40 - 0x4F
    {CPU_OP_RTI, CPU_ADDR_IMP,  6, 0},
    {CPU_OP_EOR, CPU_ADDR_IZX,  6, 0},
    {CPU_OP_KIL, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_SRE, CPU_ADDR_IZX,  8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_EOR, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_LSR_MEM, CPU_ADDR_ZP,   5, 0},
    {CPU_OP_SRE, CPU_ADDR_ZP,   5, 0},
    {CPU_OP_PHA, CPU_ADDR_IMP,  3, 0},
    {CPU_OP_EOR, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_LSR_A, CPU_ADDR_ACC,  2, 0},
    {CPU_OP_ALR, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_JMP, CPU_ADDR_ABS,  3, 0},
    {CPU_OP_EOR, CPU_ADDR_ABS,  4, 0},
    {CPU_OP_LSR_MEM, CPU_ADDR_ABS,  6, 0},
    {CPU_OP_SRE, CPU_ADDR_ABS,  6, 0},

    // 0x50 - 0x5F
    {CPU_OP_BVC, CPU_ADDR_REL,  2, 0},
    {CPU_OP_EOR, CPU_ADDR_IZY,  5, 1},
    {CPU_OP_KIL, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_SRE, CPU_ADDR_IZY,  8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZPX,  4, 0},
    {CPU_OP_EOR, CPU_ADDR_ZPX,  4, 0},
    {CPU_OP_LSR_MEM, CPU_ADDR_ZPX,  6, 0},
    {CPU_OP_SRE, CPU_ADDR_ZPX,  6, 0},
    {CPU_OP_CLI, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_EOR, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_NOP, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_SRE, CPU_ADDR_ABSY, 7, 0},
    {CPU_OP_NOP, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_EOR, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_LSR_MEM, CPU_ADDR_ABSX, 7, 0},
    {CPU_OP_SRE, CPU_ADDR_ABSX, 7, 0},

    // 0x60 - 0x6F
    {CPU_OP_RTS, CPU_ADDR_IMP,  6, 0},
    {CPU_OP_ADC, CPU_ADDR_IZX,  6, 0},
    {CPU_OP_KIL, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_RRA, CPU_ADDR_IZX,  8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_ADC, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_ROR_MEM, CPU_ADDR_ZP,   5, 0},
    {CPU_OP_RRA, CPU_ADDR_ZP,   5, 0},
    {CPU_OP_PLA, CPU_ADDR_IMP,  4, 0},
    {CPU_OP_ADC, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_ROR_A, CPU_ADDR_ACC,  2, 0},
    {CPU_OP_ARR, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_JMP, CPU_ADDR_IND,  5, 0},
    {CPU_OP_ADC, CPU_ADDR_ABS,  4, 0},
    {CPU_OP_ROR_MEM, CPU_ADDR_ABS,  6, 0},
    {CPU_OP_RRA, CPU_ADDR_ABS,  6, 0},

    // 0x70 - 0x7F
    {CPU_OP_BVS, CPU_ADDR_REL,  2, 0},
    {CPU_OP_ADC, CPU_ADDR_IZY,  5, 1},
    {CPU_OP_KIL, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_RRA, CPU_ADDR_IZY,  8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZPX,  4, 0},
    {CPU_OP_ADC, CPU_ADDR_ZPX,  4, 0},
    {CPU_OP_ROR_MEM, CPU_ADDR_ZPX,  6, 0},
    {CPU_OP_RRA, CPU_ADDR_ZPX,  6, 0},
    {CPU_OP_SEI, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_ADC, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_NOP, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_RRA, CPU_ADDR_ABSY, 7, 0},
    {CPU_OP_NOP, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_ADC, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_ROR_MEM, CPU_ADDR_ABSX, 7, 0},
    {CPU_OP_RRA, CPU_ADDR_ABSX, 7, 0},

    // 0x80 - 0x8F
    {CPU_OP_NOP, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_STA, CPU_ADDR_IZX,  6, 0},
    {CPU_OP_NOP, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_SAX, CPU_ADDR_IZX,  6, 0},
    {CPU_OP_STY, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_STA, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_STX, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_SAX, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_DEY, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_NOP, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_TXA, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_KIL, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_STY, CPU_ADDR_ABS,  4, 0},
    {CPU_OP_STA, CPU_ADDR_ABS,  4, 0},
    {CPU_OP_STX, CPU_ADDR_ABS,  4, 0},
    {CPU_OP_SAX, CPU_ADDR_ABS,  4, 0},

    // 0x90 - 0x9F
    {CPU_OP_BCC, CPU_ADDR_REL,  2, 0},
    {CPU_OP_STA, CPU_ADDR_IZY,  6, 0},
    {CPU_OP_KIL, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_TAS, CPU_ADDR_ABSY, 5, 0},
    {CPU_OP_STY, CPU_ADDR_ZPX,  4, 0},
    {CPU_OP_STA, CPU_ADDR_ZPX,  4, 0},
    {CPU_OP_STX, CPU_ADDR_ZPY,  4, 0},
    {CPU_OP_SAX, CPU_ADDR_ZPY,  4, 0},
    {CPU_OP_TYA, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_STA, CPU_ADDR_ABSY, 5, 0},
    {CPU_OP_TXS, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_TAS, CPU_ADDR_ABSY, 5, 0},
    {CPU_OP_SHY, CPU_ADDR_ABSX, 5, 0},
    {CPU_OP_STA, CPU_ADDR_ABSX, 5, 0},
    {CPU_OP_SHX, CPU_ADDR_ABSY, 5, 0},
    {CPU_OP_TAS, CPU_ADDR_ABSY, 5, 0},

    // 0xA0 - 0xAF
    {CPU_OP_LDY, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_LDA, CPU_ADDR_IZX,  6, 0},
    {CPU_OP_LDX, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_LAX, CPU_ADDR_IZX,  6, 0},
    {CPU_OP_LDY, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_LDA, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_LDX, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_LAX, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_TAY, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_LDA, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_TAX, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_LAX, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_LDY, CPU_ADDR_ABS,  4, 0},
    {CPU_OP_LDA, CPU_ADDR_ABS,  4, 0},
    {CPU_OP_LDX, CPU_ADDR_ABS,  4, 0},
    {CPU_OP_LAX, CPU_ADDR_ABS,  4, 0},

    // 0xB0 - 0xBF
    {CPU_OP_BCS, CPU_ADDR_REL,  2, 0},
    {CPU_OP_LDA, CPU_ADDR_IZY,  5, 1},
    {CPU_OP_KIL, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_LAX, CPU_ADDR_IZY,  5, 1},
    {CPU_OP_LDY, CPU_ADDR_ZPX,  4, 0},
    {CPU_OP_LDA, CPU_ADDR_ZPX,  4, 0},
    {CPU_OP_LDX, CPU_ADDR_ZPY,  4, 0},
    {CPU_OP_LAX, CPU_ADDR_ZPY,  4, 0},
    {CPU_OP_CLV, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_LDA, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_TSX, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_LAS, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_LDY, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_LDA, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_LDX, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_LAX, CPU_ADDR_ABSY, 4, 1},

    // 0xC0 - 0xCF
    {CPU_OP_CPY, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_CMP, CPU_ADDR_IZX,  6, 0},
    {CPU_OP_NOP, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_DCP, CPU_ADDR_IZX,  8, 0},
    {CPU_OP_CPY, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_CMP, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_DEC, CPU_ADDR_ZP,   5, 0},
    {CPU_OP_DCP, CPU_ADDR_ZP,   5, 0},
    {CPU_OP_INY, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_CMP, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_DEX, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_SBX, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_CPY, CPU_ADDR_ABS,  4, 0},
    {CPU_OP_CMP, CPU_ADDR_ABS,  4, 0},
    {CPU_OP_DEC, CPU_ADDR_ABS,  6, 0},
    {CPU_OP_DCP, CPU_ADDR_ABS,  6, 0},

    // 0xD0 - 0xDF
    {CPU_OP_BNE, CPU_ADDR_REL,  2, 0},
    {CPU_OP_CMP, CPU_ADDR_IZY,  5, 1},
    {CPU_OP_KIL, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_DCP, CPU_ADDR_IZY,  8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZPX,  4, 0},
    {CPU_OP_CMP, CPU_ADDR_ZPX,  4, 0},
    {CPU_OP_DEC, CPU_ADDR_ZPX,  6, 0},
    {CPU_OP_DCP, CPU_ADDR_ZPX,  6, 0},
    {CPU_OP_CLD, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_CMP, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_NOP, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_DCP, CPU_ADDR_ABSY, 7, 0},
    {CPU_OP_NOP, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_CMP, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_DEC, CPU_ADDR_ABSX, 7, 0},
    {CPU_OP_DCP, CPU_ADDR_ABSX, 7, 0},

    // 0xE0 - 0xEF
    {CPU_OP_CPX, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_SBC, CPU_ADDR_IZX,  6, 0},
    {CPU_OP_NOP, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_ISC, CPU_ADDR_IZX,  8, 0},
    {CPU_OP_CPX, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_SBC, CPU_ADDR_ZP,   3, 0},
    {CPU_OP_INC, CPU_ADDR_ZP,   5, 0},
    {CPU_OP_ISC, CPU_ADDR_ZP,   5, 0},
    {CPU_OP_INX, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_SBC, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_NOP, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_SBC, CPU_ADDR_IMM,  2, 0},
    {CPU_OP_CPX, CPU_ADDR_ABS,  4, 0},
    {CPU_OP_SBC, CPU_ADDR_ABS,  4, 0},
    {CPU_OP_INC, CPU_ADDR_ABS,  6, 0},
    {CPU_OP_ISC, CPU_ADDR_ABS,  6, 0},

    // 0xF0 - 0xFF
    {CPU_OP_BEQ, CPU_ADDR_REL,  2, 0},
    {CPU_OP_SBC, CPU_ADDR_IZY,  5, 1},
    {CPU_OP_KIL, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_ISC, CPU_ADDR_IZY,  8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZPX,  4, 0},
    {CPU_OP_SBC, CPU_ADDR_ZPX,  4, 0},
    {CPU_OP_INC, CPU_ADDR_ZPX,  6, 0},
    {CPU_OP_ISC, CPU_ADDR_ZPX,  6, 0},
    {CPU_OP_SED, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_SBC, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_NOP, CPU_ADDR_IMP,  2, 0},
    {CPU_OP_ISC, CPU_ADDR_ABSY, 7, 0},
    {CPU_OP_NOP, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_SBC, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_INC, CPU_ADDR_ABSX, 7, 0},
    {CPU_OP_ISC, CPU_ADDR_ABSX, 7, 0}
};

void CPU_SetFlag(CPU *cpu, uint8_t flag, uint8_t value) 
{
    if (value)
        cpu->status |= flag; // Set the flag
    else
        cpu->status &= ~flag; // Clear the flag
}

uint8_t CPU_GetFlag(CPU *cpu, uint8_t flag) 
{
    return (cpu->status & flag); // Return the status of the flag
}

// --- CPU Lifecycle Functions ---
CPU *CPU_Create(NES *nes) {
    CPU *cpu = malloc(sizeof(CPU));
    if (!cpu) return NULL;
    memset(cpu, 0, sizeof(CPU));
    cpu->nes = nes;
    // Lookup table is now const and initialized at compile time.
    // No need for CPU_InitializeLookupTable() or table_initialized flag.
    CPU_Reset(cpu);
    return cpu;
}

void CPU_Reset(CPU *cpu) {
    cpu->a = 0;
    cpu->x = 0;
    cpu->y = 0;
    cpu->sp = 0xFD; // Stack pointer starts at 0xFD
    cpu->status = CPU_FLAG_UNUSED | CPU_FLAG_INTERRUPT; // Start with I flag set, Unused set
    cpu->pc = BUS_Read16(cpu->nes, 0xFFFC); // Read reset vector
    cpu->total_cycles = 0;
}

void CPU_Destroy(CPU* cpu) {
    if (cpu)
        free(cpu);
}

// --- Main CPU Step Function ---
int CPU_Step(CPU *cpu) {
    //uint16_t initial_pc_debug = cpu->pc; // For debugging

    uint8_t opcode = BUS_Read(cpu->nes, cpu->pc++);
    Instruction inst = instruction_lookup[opcode];

    bool page_crossed_by_addr = false;
    uint16_t effective_address = 0;

    //if (inst.addressing_mode) {
        effective_address = inst.addressing_mode(cpu, &page_crossed_by_addr);
    //}

    uint8_t current_opcode_cycles = inst.cycles;
    if (page_crossed_by_addr && inst.add_cycles_on_page_cross) {
        current_opcode_cycles++;
    }

    if (inst.operation) {
        inst.operation(cpu, effective_address, &current_opcode_cycles);
    }
    
    cpu->total_cycles += current_opcode_cycles;

    // Example debug print (optional)
    // debug_print("PC:%04X OP:%02X (%s) A:%02X X:%02X Y:%02X P:%02X SP:%02X ADDR:%04X CYC:%lu (+%u)\n",
    //        initial_pc_debug, opcode, inst.name, cpu->a, cpu->x, cpu->y, cpu->status, cpu->sp, effective_address, cpu->total_cycles, current_opcode_cycles);

    return current_opcode_cycles;
}


// --- CPU Operation Implementations ---
// (These need to be fully implemented based on your previous code and 6502 specs)

// Official Opcodes (Implementations from previous response, ensure they are complete)
static void CPU_OP_ADC(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; uint8_t M = BUS_Read(cpu->nes, address); uint16_t temp = cpu->a + M + (CPU_GetFlag(cpu, CPU_FLAG_CARRY) ? 1 : 0); CPU_SetFlag(cpu, CPU_FLAG_CARRY, temp > 0xFF); CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, (~(cpu->a ^ M) & (cpu->a ^ (uint8_t)temp) & 0x80) != 0); cpu->a = (uint8_t)temp; CPU_UpdateZeroNegativeFlags(cpu, cpu->a); }
static void CPU_OP_AND(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; cpu->a &= BUS_Read(cpu->nes, address); CPU_UpdateZeroNegativeFlags(cpu, cpu->a); }
static void CPU_OP_ASL_A(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; CPU_SetFlag(cpu, CPU_FLAG_CARRY, (cpu->a & 0x80) != 0); cpu->a <<= 1; CPU_UpdateZeroNegativeFlags(cpu, cpu->a); }
static void CPU_OP_ASL_MEM(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; uint8_t M = BUS_Read(cpu->nes, address); CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x80) != 0); M <<= 1; BUS_Write(cpu->nes, address, M); CPU_UpdateZeroNegativeFlags(cpu, M); }
static void CPU_OP_BCC(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { if (!CPU_GetFlag(cpu, CPU_FLAG_CARRY)) { uint16_t old_pc = cpu->pc; cpu->pc = address; *cycles_ref += 1; if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) *cycles_ref += 1; } }
static void CPU_OP_BCS(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { if (CPU_GetFlag(cpu, CPU_FLAG_CARRY)) { uint16_t old_pc = cpu->pc; cpu->pc = address; *cycles_ref += 1; if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) *cycles_ref += 1; } }
static void CPU_OP_BEQ(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { if (CPU_GetFlag(cpu, CPU_FLAG_ZERO)) { uint16_t old_pc = cpu->pc; cpu->pc = address; *cycles_ref += 1; if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) *cycles_ref += 1; } }
static void CPU_OP_BIT(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; uint8_t M = BUS_Read(cpu->nes, address); CPU_SetFlag(cpu, CPU_FLAG_ZERO, (cpu->a & M) == 0); CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, (M & 0x40) != 0); CPU_SetFlag(cpu, CPU_FLAG_NEGATIVE, (M & 0x80) != 0); }
static void CPU_OP_BMI(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { if (CPU_GetFlag(cpu, CPU_FLAG_NEGATIVE)) { uint16_t old_pc = cpu->pc; cpu->pc = address; *cycles_ref += 1; if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) *cycles_ref += 1; } }
static void CPU_OP_BNE(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { if (!CPU_GetFlag(cpu, CPU_FLAG_ZERO)) { uint16_t old_pc = cpu->pc; cpu->pc = address; *cycles_ref += 1; if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) *cycles_ref += 1; } }
static void CPU_OP_BPL(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { if (!CPU_GetFlag(cpu, CPU_FLAG_NEGATIVE)) { uint16_t old_pc = cpu->pc; cpu->pc = address; *cycles_ref += 1; if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) *cycles_ref += 1; } }
static void CPU_OP_BRK(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; cpu->pc++; CPU_Push16(cpu, cpu->pc); CPU_Push(cpu, cpu->status | CPU_FLAG_BREAK | CPU_FLAG_UNUSED); CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, true); cpu->pc = BUS_Read16(cpu->nes, 0xFFFE); }
static void CPU_OP_BVC(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { if (!CPU_GetFlag(cpu, CPU_FLAG_OVERFLOW)) { uint16_t old_pc = cpu->pc; cpu->pc = address; *cycles_ref += 1; if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) *cycles_ref += 1; } }
static void CPU_OP_BVS(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { if (CPU_GetFlag(cpu, CPU_FLAG_OVERFLOW)) { uint16_t old_pc = cpu->pc; cpu->pc = address; *cycles_ref += 1; if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) *cycles_ref += 1; } }
static void CPU_OP_CLC(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; CPU_SetFlag(cpu, CPU_FLAG_CARRY, false); }
static void CPU_OP_CLD(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; CPU_SetFlag(cpu, CPU_FLAG_DECIMAL, false); }
static void CPU_OP_CLI(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, false); }
static void CPU_OP_CLV(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, false); }
static void CPU_OP_CMP(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; uint8_t M = BUS_Read(cpu->nes, address); uint8_t temp = cpu->a - M; CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->a >= M); CPU_UpdateZeroNegativeFlags(cpu, temp); }
static void CPU_OP_CPX(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; uint8_t M = BUS_Read(cpu->nes, address); uint8_t temp = cpu->x - M; CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->x >= M); CPU_UpdateZeroNegativeFlags(cpu, temp); }
static void CPU_OP_CPY(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; uint8_t M = BUS_Read(cpu->nes, address); uint8_t temp = cpu->y - M; CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->y >= M); CPU_UpdateZeroNegativeFlags(cpu, temp); }
static void CPU_OP_DEC(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; uint8_t M = BUS_Read(cpu->nes, address) - 1; BUS_Write(cpu->nes, address, M); CPU_UpdateZeroNegativeFlags(cpu, M); }
static void CPU_OP_DEX(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; cpu->x--; CPU_UpdateZeroNegativeFlags(cpu, cpu->x); }
static void CPU_OP_DEY(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; cpu->y--; CPU_UpdateZeroNegativeFlags(cpu, cpu->y); }
static void CPU_OP_EOR(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; cpu->a ^= BUS_Read(cpu->nes, address); CPU_UpdateZeroNegativeFlags(cpu, cpu->a); }
static void CPU_OP_INC(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; uint8_t M = BUS_Read(cpu->nes, address) + 1; BUS_Write(cpu->nes, address, M); CPU_UpdateZeroNegativeFlags(cpu, M); }
static void CPU_OP_INX(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; cpu->x++; CPU_UpdateZeroNegativeFlags(cpu, cpu->x); }
static void CPU_OP_INY(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; cpu->y++; CPU_UpdateZeroNegativeFlags(cpu, cpu->y); }
static void CPU_OP_JMP(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; cpu->pc = address; }
static void CPU_OP_JSR(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; CPU_Push16(cpu, cpu->pc - 1); cpu->pc = address; }
static void CPU_OP_LDA(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; cpu->a = BUS_Read(cpu->nes, address); CPU_UpdateZeroNegativeFlags(cpu, cpu->a); }
static void CPU_OP_LDX(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; cpu->x = BUS_Read(cpu->nes, address); CPU_UpdateZeroNegativeFlags(cpu, cpu->x); }
static void CPU_OP_LDY(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; cpu->y = BUS_Read(cpu->nes, address); CPU_UpdateZeroNegativeFlags(cpu, cpu->y); }
static void CPU_OP_LSR_A(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; CPU_SetFlag(cpu, CPU_FLAG_CARRY, (cpu->a & 0x01) != 0); cpu->a >>= 1; CPU_UpdateZeroNegativeFlags(cpu, cpu->a); }
static void CPU_OP_LSR_MEM(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; uint8_t M = BUS_Read(cpu->nes, address); CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x01) != 0); M >>= 1; BUS_Write(cpu->nes, address, M); CPU_UpdateZeroNegativeFlags(cpu, M); }
static void CPU_OP_ORA(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; cpu->a |= BUS_Read(cpu->nes, address); CPU_UpdateZeroNegativeFlags(cpu, cpu->a); }
static void CPU_OP_PHA(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; CPU_Push(cpu, cpu->a); }
static void CPU_OP_PHP(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; CPU_Push(cpu, cpu->status | CPU_FLAG_BREAK | CPU_FLAG_UNUSED); } // B flag is set when pushed
static void CPU_OP_PLA(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; cpu->a = CPU_Pop(cpu); CPU_UpdateZeroNegativeFlags(cpu, cpu->a); }
static void CPU_OP_PLP(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; cpu->status = CPU_Pop(cpu); CPU_SetFlag(cpu, CPU_FLAG_UNUSED, true); CPU_SetFlag(cpu, CPU_FLAG_BREAK, false); } // U is always set, B is cleared
static void CPU_OP_ROL_A(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; bool old_c = CPU_GetFlag(cpu, CPU_FLAG_CARRY); CPU_SetFlag(cpu, CPU_FLAG_CARRY, (cpu->a & 0x80) != 0); cpu->a <<= 1; if (old_c) cpu->a |= 0x01; CPU_UpdateZeroNegativeFlags(cpu, cpu->a); }
static void CPU_OP_ROL_MEM(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; uint8_t M = BUS_Read(cpu->nes, address); bool old_c = CPU_GetFlag(cpu, CPU_FLAG_CARRY); CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x80) != 0); M <<= 1; if (old_c) M |= 0x01; BUS_Write(cpu->nes, address, M); CPU_UpdateZeroNegativeFlags(cpu, M); }
static void CPU_OP_ROR_A(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; bool old_c = CPU_GetFlag(cpu, CPU_FLAG_CARRY); CPU_SetFlag(cpu, CPU_FLAG_CARRY, (cpu->a & 0x01) != 0); cpu->a >>= 1; if (old_c) cpu->a |= 0x80; CPU_UpdateZeroNegativeFlags(cpu, cpu->a); }
static void CPU_OP_ROR_MEM(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; uint8_t M = BUS_Read(cpu->nes, address); bool old_c = CPU_GetFlag(cpu, CPU_FLAG_CARRY); CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x01) != 0); M >>= 1; if (old_c) M |= 0x80; BUS_Write(cpu->nes, address, M); CPU_UpdateZeroNegativeFlags(cpu, M); }
static void CPU_OP_RTI(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; cpu->status = CPU_Pop(cpu); CPU_SetFlag(cpu, CPU_FLAG_UNUSED, true); CPU_SetFlag(cpu, CPU_FLAG_BREAK, false); cpu->pc = CPU_Pop16(cpu); }
static void CPU_OP_RTS(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; cpu->pc = CPU_Pop16(cpu) + 1; }
static void CPU_OP_SBC(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; uint8_t M = BUS_Read(cpu->nes, address); uint16_t temp = cpu->a - M - (CPU_GetFlag(cpu, CPU_FLAG_CARRY) ? 0 : 1); CPU_SetFlag(cpu, CPU_FLAG_CARRY, !(temp > 0xFF)); CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, ((cpu->a ^ M) & (cpu->a ^ (uint8_t)temp) & 0x80) != 0); cpu->a = (uint8_t)temp; CPU_UpdateZeroNegativeFlags(cpu, cpu->a); }
static void CPU_OP_SEC(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; CPU_SetFlag(cpu, CPU_FLAG_CARRY, true); }
static void CPU_OP_SED(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; CPU_SetFlag(cpu, CPU_FLAG_DECIMAL, true); }
static void CPU_OP_SEI(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, true); }
static void CPU_OP_STA(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; BUS_Write(cpu->nes, address, cpu->a); }
static void CPU_OP_STX(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; BUS_Write(cpu->nes, address, cpu->x); }
static void CPU_OP_STY(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)cycles_ref; BUS_Write(cpu->nes, address, cpu->y); }
static void CPU_OP_TAX(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; cpu->x = cpu->a; CPU_UpdateZeroNegativeFlags(cpu, cpu->x); }
static void CPU_OP_TAY(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; cpu->y = cpu->a; CPU_UpdateZeroNegativeFlags(cpu, cpu->y); }
static void CPU_OP_TSX(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; cpu->x = cpu->sp; CPU_UpdateZeroNegativeFlags(cpu, cpu->x); }
static void CPU_OP_TXA(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; cpu->a = cpu->x; CPU_UpdateZeroNegativeFlags(cpu, cpu->a); }
static void CPU_OP_TXS(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; cpu->sp = cpu->x; }
static void CPU_OP_TYA(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { (void)address; (void)cycles_ref; cpu->a = cpu->y; CPU_UpdateZeroNegativeFlags(cpu, cpu->a); }

// Unofficial Opcode Implementations (stubs or simplified, expand as needed)
static void CPU_OP_KIL(CPU *cpu, uint16_t address, uint8_t *cycles_ref) {
    (void)cpu; (void)address; (void)cycles_ref;
    // Typically halts the CPU. For emulation, you might loop indefinitely or set a halt flag.
    // For now, acts like a NOP that might take 2 cycles.
    // Or, if you have a halt flag: cpu->halted = true;
    DEBUG_WARN("KIL instruction encountered at PC: %04X\n", cpu->pc - 1);
}

static void CPU_OP_NOP(CPU *cpu, uint16_t address, uint8_t *cycles_ref) {
    (void)cpu; (void)address; (void)cycles_ref; // Acts like NOP
}

static void CPU_OP_SLO(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { // ASL + ORA
    (void)cycles_ref;
    uint8_t M = BUS_Read(cpu->nes, address);
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x80) != 0);
    M <<= 1;
    BUS_Write(cpu->nes, address, M);
    cpu->a |= M;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_RLA(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { // ROL + AND
    (void)cycles_ref;
    uint8_t M = BUS_Read(cpu->nes, address);
    bool old_c = CPU_GetFlag(cpu, CPU_FLAG_CARRY);
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x80) != 0);
    M <<= 1;
    if (old_c) M |= 0x01;
    BUS_Write(cpu->nes, address, M);
    cpu->a &= M;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_SRE(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { // LSR + EOR
    (void)cycles_ref;
    uint8_t M = BUS_Read(cpu->nes, address);
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x01) != 0);
    M >>= 1;
    BUS_Write(cpu->nes, address, M);
    cpu->a ^= M;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_RRA(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { // ROR + ADC
    (void)cycles_ref;
    uint8_t M = BUS_Read(cpu->nes, address);
    bool old_c_ror = CPU_GetFlag(cpu, CPU_FLAG_CARRY);
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x01) != 0); // Carry for ROR
    M >>= 1;
    if (old_c_ror) M |= 0x80;
    BUS_Write(cpu->nes, address, M);

    // ADC part
    uint16_t temp = cpu->a + M + (CPU_GetFlag(cpu, CPU_FLAG_CARRY) ? 1 : 0); // Use new carry from ROR
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, temp > 0xFF); // Carry for ADC
    CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, (~(cpu->a ^ M) & (cpu->a ^ (uint8_t)temp) & 0x80) != 0);
    cpu->a = (uint8_t)temp;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_SAX(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { // Store A & X
    (void)cycles_ref;
    BUS_Write(cpu->nes, address, cpu->a & cpu->x);
}

static void CPU_OP_LAX(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { // LDA + LDX (or LDA + TAX)
    (void)cycles_ref;
    cpu->a = BUS_Read(cpu->nes, address);
    cpu->x = cpu->a;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Flags based on A (same as X)
}

static void CPU_OP_DCP(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { // DEC + CMP
    (void)cycles_ref;
    uint8_t M = BUS_Read(cpu->nes, address) - 1;
    BUS_Write(cpu->nes, address, M);
    // CMP part (A - M)
    uint8_t temp_cmp = cpu->a - M;
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->a >= M);
    CPU_UpdateZeroNegativeFlags(cpu, temp_cmp);
}

static void CPU_OP_ISC(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { // INC + SBC
    (void)cycles_ref;
    uint8_t M = BUS_Read(cpu->nes, address) + 1;
    BUS_Write(cpu->nes, address, M);
    // SBC part (A - M - (1-C))
    uint16_t temp_sbc = cpu->a - M - (CPU_GetFlag(cpu, CPU_FLAG_CARRY) ? 0 : 1);
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, !(temp_sbc > 0xFF));
    CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, ((cpu->a ^ M) & (cpu->a ^ (uint8_t)temp_sbc) & 0x80) != 0);
    cpu->a = (uint8_t)temp_sbc;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_ANC(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { // AND, C = N
    (void)cycles_ref;
    cpu->a &= BUS_Read(cpu->nes, address); // For ANC #imm, address is immediate value
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, CPU_GetFlag(cpu, CPU_FLAG_NEGATIVE));
}

static void CPU_OP_ALR(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { // AND #imm, LSR A
    (void)cycles_ref;
    cpu->a &= BUS_Read(cpu->nes, address); // For ALR #imm, address is immediate value
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (cpu->a & 0x01) != 0);
    cpu->a >>= 1;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_ARR(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { // AND #imm, ROR A, special flags
    (void)cycles_ref;
    cpu->a &= BUS_Read(cpu->nes, address); // For ARR #imm, address is immediate value
    bool old_c = CPU_GetFlag(cpu, CPU_FLAG_CARRY);
    // ROR A part
    bool new_c_for_ror = (cpu->a & 0x01) != 0; // This is NOT the final carry flag for ARR
    cpu->a >>= 1;
    if (old_c) cpu->a |= 0x80;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
    // Special flags for ARR
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (cpu->a & 0x40) != 0); // Bit 6 of result to Carry
    CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, ((cpu->a & 0x40) ^ ((cpu->a & 0x20) << 1)) != 0); // (Bit6 XOR Bit5) of result to Overflow
}

static void CPU_OP_SBX(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { // (A & X) - imm -> X
    (void)cycles_ref;
    uint8_t M = BUS_Read(cpu->nes, address); // For SBX #imm, address is immediate value
    uint16_t temp = (cpu->a & cpu->x) - M;
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, !((cpu->a & cpu->x) < M)); // (A&X) >= M
    cpu->x = (uint8_t)temp;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->x);
}

static void CPU_OP_SHX(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { // Store X & (HighByte(addr) + 1) -> addr
    (void)cycles_ref;
    // This is complex due to how address is formed for write.
    // For ABSY: addr = base + Y. Write to (base + Y). Data = X & (HighByte(base+Y) + 1)
    // This is simplified: assume 'address' is the final computed address.
    // The high byte used for the AND operation is from the *effective address*.
    uint8_t high_byte_of_addr = (address >> 8) & 0xFF;
    uint8_t data_to_write = cpu->x & (high_byte_of_addr + 1);
    // Instability: If (base + Y) crosses page, some sources say it writes to (base_HB + Y) & data.
    // For simplicity, write `data_to_write` to `address`.
    BUS_Write(cpu->nes, address, data_to_write);
}

static void CPU_OP_SHY(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { // Store Y & (HighByte(addr) + 1) -> addr
    (void)cycles_ref;
    uint8_t high_byte_of_addr = (address >> 8) & 0xFF;
    uint8_t data_to_write = cpu->y & (high_byte_of_addr + 1);
    BUS_Write(cpu->nes, address, data_to_write);
}

static void CPU_OP_TAS(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { // A&X -> SP; A&X & (H+1) -> M (unstable)
    (void)cycles_ref;
    cpu->sp = cpu->a & cpu->x;
    // The memory write part is often unstable and varies.
    // Simplified: just SP = A & X. Some emulators might implement the write.
    // If write is implemented:
    // uint8_t high_byte_of_addr = (address >> 8) & 0xFF;
    // uint8_t data_to_write = cpu->sp & (high_byte_of_addr + 1);
    // BUS_Write(cpu->nes, address, data_to_write);
}

static void CPU_OP_LAS(CPU *cpu, uint16_t address, uint8_t *cycles_ref) { // Mem & SP -> A, X, SP
    (void)cycles_ref;
    uint8_t M = BUS_Read(cpu->nes, address);
    cpu->a = cpu->x = cpu->sp = (M & cpu->sp);
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

// NMI and IRQ are external, not ops, but their handler logic:
void CPU_NMI(CPU *cpu) {
    CPU_Push16(cpu, cpu->pc);
    CPU_Push(cpu, (cpu->status & ~CPU_FLAG_BREAK) | CPU_FLAG_UNUSED);
    CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, true);
    cpu->total_cycles += 7; // NMI fixed cycles
    cpu->pc = BUS_Read16(cpu->nes, 0xFFFA);
}

void CPU_IRQ(CPU *cpu) {
    if (!CPU_GetFlag(cpu, CPU_FLAG_INTERRUPT)) {
        CPU_Push16(cpu, cpu->pc);
        CPU_Push(cpu, (cpu->status & ~CPU_FLAG_BREAK) | CPU_FLAG_UNUSED);
        CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, true);
        cpu->total_cycles += 7; // IRQ fixed cycles
        cpu->pc = BUS_Read16(cpu->nes, 0xFFFE);
    }
}
