#ifdef defined(__GNUC__) && defined(__x86_64__) && !defined(__APPLE__)
#define USE_INLINE_X86_ASM
#elif defined(_MSC_VER) && defined(_M_X64)
#define USE_INLINE_X86_ASM
#else
#undef USE_INLINE_X86_ASM
#endif


#ifdef USE_INLINE_X86_ASM

#include <stdint.h>
#include <stdio.h> // For printf in example and unknown opcode message

// NES CPU Registers
typedef struct {
    uint8_t a;      // Accumulator
    uint8_t x;      // X Index Register
    uint8_t y;      // Y Index Register
    uint8_t sp;     // Stack Pointer
    uint16_t pc;    // Program Counter
    uint8_t p;      // Status Register (Flags: N V - B D I Z C)
} CPU_State;

CPU_State cpu;
uint8_t memory[65536]; // 64KB RAM for simplicity; a real NES has a complex memory map

// Status Register Flags (bit positions)
#define FLAG_CARRY     (1 << 0) // C
#define FLAG_ZERO      (1 << 1) // Z
#define FLAG_IRQ_DISABLE (1 << 2) // I
#define FLAG_DECIMAL   (1 << 3) // D (not used in NES, but part of 6502)
#define FLAG_BREAK     (1 << 4) // B
#define FLAG_UNUSED    (1 << 5) // - (always 1 on NES)
#define FLAG_OVERFLOW  (1 << 6) // V
#define FLAG_NEGATIVE  (1 << 7) // N

// Memory access functions
// In a real emulator, these would handle memory mapping, PPU registers, APU registers, etc.
uint8_t mem_read(uint16_t addr) {
    // Add PPU/APU/Controller/Mapper read handling here
    if (addr >= 0x0000 && addr <= 0x1FFF) { // 2KB internal RAM, mirrored
        return memory[addr & 0x07FF];
    }
    // Simplified: direct memory read
    return memory[addr];
}

void mem_write(uint16_t addr, uint8_t value) {
    // Add PPU/APU/Controller/Mapper write handling here
    if (addr >= 0x0000 && addr <= 0x1FFF) { // 2KB internal RAM, mirrored
        memory[addr & 0x07FF] = value;
        return;
    }
    // Simplified: direct memory write
    memory[addr] = value;
}

// Helper to set/clear status flags
void set_flag(uint8_t flag, int value) {
    if (value) {
        cpu.p |= flag;
    } else {
        cpu.p &= ~flag;
    }
}

// Helper to update Zero and Negative flags based on a value
void update_zn_flags(uint8_t value) {
    set_flag(FLAG_ZERO, value == 0);
    set_flag(FLAG_NEGATIVE, (value & 0x80) != 0);
}

void cpu_reset() {
    // Load PC from reset vector ($FFFC-$FFFD)
    uint16_t lo = mem_read(0xFFFC);
    uint16_t hi = mem_read(0xFFFD);
    cpu.pc = (hi << 8) | lo;

    cpu.a = 0;
    cpu.x = 0;
    cpu.y = 0;
    cpu.sp = 0xFD; // Stack pointer initial value after reset
    cpu.p = FLAG_UNUSED | FLAG_IRQ_DISABLE; // Initial flags: Unused and IRQ Disable set

    // Typically, a reset takes 7 CPU cycles. This is not tracked here.
}

// Executes one CPU instruction using x86 inline assembly where appropriate.
void cpu_step_x86() {
    uint8_t opcode = mem_read(cpu.pc);
    cpu.pc++; // Increment PC after fetching opcode

    // Note: Cycle counts are approximate and depend on addressing modes.
    // A full implementation requires detailed cycle accounting.

    switch (opcode) {
        case 0xA9: { // LDA #imm (Load Accumulator Immediate)
            uint8_t immediate_value = mem_read(cpu.pc);
            cpu.pc++;
            
            __asm__ volatile (
                "movb %1, %%al\n\t"   // Move immediate_value into AL register
                "movb %%al, %0\n\t"   // Store AL (which now holds immediate_value) into cpu.a
                : "=m" (cpu.a)        // Output: cpu.a (memory operand)
                : "r" (immediate_value) // Input: immediate_value (register operand)
                : "al", "memory"      // Clobbered: AL register, and "memory" because cpu.a is written to
            );
            update_zn_flags(cpu.a);
            // Cycles: 2
            break;
        }

        case 0x8D: { // STA abs (Store Accumulator Absolute)
            uint16_t addr_lo = mem_read(cpu.pc);
            cpu.pc++;
            uint16_t addr_hi = mem_read(cpu.pc);
            cpu.pc++;
            uint16_t abs_addr = (addr_hi << 8) | addr_lo;
            
            // For STA, the primary action is memory write.
            // While cpu.a could be moved to a register via asm,
            // the mem_write function call is clearer here.
            mem_write(abs_addr, cpu.a);
            // Cycles: 4
            break;
        }

        case 0x4C: { // JMP abs (Jump Absolute)
            uint16_t addr_lo = mem_read(cpu.pc);
            // cpu.pc++; // PC is not incremented for the first byte of operand for JMP
            uint16_t addr_hi = mem_read(cpu.pc + 1);
            // cpu.pc++; // PC is effectively replaced by the new address
            uint16_t jump_addr = (addr_hi << 8) | addr_lo;

            __asm__ volatile (
                "movw %1, %0\n\t"     // Move jump_addr into cpu.pc
                : "=m" (cpu.pc)       // Output: cpu.pc (memory operand)
                : "r" (jump_addr)     // Input: jump_addr (register operand)
                : "memory"            // Clobbered: "memory" because cpu.pc is written to
            );
            // Cycles: 3
            break;
        }

        // Add cases for all other 6502 opcodes here...
        // This is a very large task.

        default:
            // PC was already incremented past the unknown opcode.
            // So, cpu.pc-1 is the address of the unknown opcode.
            printf("Unknown opcode: 0x%02X at PC: 0x%04X\n", opcode, cpu.pc - 1);
            // Potentially halt, trigger an error, or NOP.
            break;
    }
}

/*
// Example main function for testing
int main() {
    // Simple program:
    // $8000: LDA #$C0
    // $8002: STA $0200
    // $8005: JMP $8000
    memory[0xFFFC] = 0x00; // Reset vector low byte
    memory[0xFFFD] = 0x80; // Reset vector high byte -> $8000 start address

    memory[0x8000] = 0xA9; // LDA #$C0
    memory[0x8001] = 0xC0; // Value $C0
    memory[0x8002] = 0x8D; // STA $0200 (Absolute)
    memory[0x8003] = 0x00; // Low byte of address
    memory[0x8004] = 0x02; // High byte of address
    memory[0x8005] = 0x4C; // JMP $8000 (Absolute)
    memory[0x8006] = 0x00; // Low byte of jump address
    memory[0x8007] = 0x80; // High byte of jump address

    cpu_reset();

    printf("Initial state - PC: 0x%04X, A: %02X, X: %02X, Y: %02X, SP: %02X, P: %02X\n",
           cpu.pc, cpu.a, cpu.x, cpu.y, cpu.sp, cpu.p);

    for (int i = 0; i < 10; ++i) { // Run a few steps
        printf("Executing at PC: 0x%04X, Opcode: 0x%02X -> ", cpu.pc, mem_read(cpu.pc));
        cpu_step_x86();
        printf("New state - PC: 0x%04X, A: %02X, X: %02X, Y: %02X, SP: %02X, P: %02X",
               cpu.pc, cpu.a, cpu.x, cpu.y, cpu.sp, cpu.p);
        if (mem_read(0x0200) != 0) {
             printf(" (Value 0x%02X stored at $0200)", mem_read(0x0200));
        }
        printf("\n");
    }
    return 0;
}
*/

#endif // USE_INLINE_X86_ASM