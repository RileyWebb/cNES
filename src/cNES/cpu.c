#include "debug.h"

#include "cNES/nes.h"
#include "cNES/bus.h"
#include "cNES/cpu.h"

void CPU_Reset(CPU *cpu) 
{
    cpu->a = 0;
    cpu->x = 0;
    cpu->y = 0;
    cpu->sp = 0xFD; // Stack pointer starts at 0xFD
    cpu->status = CPU_FLAG_UNUSED | CPU_FLAG_INTERRUPT; // Start with I flag set, Unused set
    cpu->pc = BUS_Read16(cpu->nes, 0xFFFC); // Read reset vector
    cpu->total_cycles = 0;
    // Add 7 cycles for reset sequence? Depends on when timing starts.
}

void CPU_Push(CPU *cpu, uint8_t value) 
{
    cpu->nes->bus->memory[0x0100 + cpu->sp] = value; // Push to stack
    cpu->sp--; // Decrement stack pointer
}

uint8_t CPU_Pop(CPU *cpu) 
{
    cpu->sp++; // Increment stack pointer
    return cpu->nes->bus->memory[0x0100 + cpu->sp]; // Pop from stack
}

void CPU_Push16(CPU *cpu, uint16_t value) 
{
    CPU_Push(cpu, (uint8_t)(value >> 8));   // High byte
    CPU_Push(cpu, (uint8_t)(value & 0xFF)); // Low byte
}

uint16_t CPU_Pop16(CPU *cpu) 
{
    uint8_t lo = CPU_Pop(cpu);
    uint8_t hi = CPU_Pop(cpu);
    return (uint16_t)lo | ((uint16_t)hi << 8);
}

void CPU_SetFlag(CPU *cpu, uint8_t flag, int value) 
{
    if (value) {
        cpu->status |= flag; // Set the flag
    } else {
        cpu->status &= ~flag; // Clear the flag
    }
}

uint8_t CPU_GetFlag(CPU *cpu, uint8_t flag) 
{
    return (cpu->status & flag) > 0; // Return the status of the flag
}

void CPU_UpdateZeroNegativeFlags(CPU *cpu, uint8_t value) 
{
    CPU_SetFlag(cpu, CPU_FLAG_ZERO, value == 0); // Set zero flag if value is zero
    CPU_SetFlag(cpu, CPU_FLAG_NEGATIVE, (value & 0x80) != 0); // Set negative flag if bit 7 is set
}

void CPU_SetNegativeFlag(CPU *cpu, uint8_t value) 
{
    CPU_SetFlag(cpu, CPU_FLAG_NEGATIVE, (value & 0x80) != 0); // Set negative flag if bit 7 is set
}

// Addressing modes
uint16_t CPU_Immediate(CPU *cpu) 
{
    return cpu->pc++; // Immediate mode, just return the current PC and increment
}

uint16_t CPU_Accumulator(CPU *cpu) 
{
    return 0; // Accumulator mode, no address needed
}

uint16_t CPU_Implied(CPU *cpu) 
{
    return 0; // Implied mode, no address needed
}

uint16_t CPU_ZeroPage(CPU *cpu) 
{
    return BUS_Read(cpu->nes, cpu->pc++); // Zero page mode, read the address from memory
}

uint16_t CPU_ZeroPageX(CPU *cpu) 
{
    return (BUS_Read(cpu->nes, cpu->pc++) + cpu->x) & 0xFF; // Zero page X mode
}

uint16_t CPU_ZeroPageY(CPU *cpu) 
{
    return (BUS_Read(cpu->nes, cpu->pc++) + cpu->y) & 0xFF; // Zero page Y mode
}

uint16_t CPU_Relative(CPU *cpu) 
{
    int8_t offset = (int8_t)BUS_Read(cpu->nes, cpu->pc++); // Read signed offset
    return (uint16_t)(cpu->pc + offset); // Calculate effective address with explicit cast
}

uint16_t CPU_Absolute(CPU *cpu) 
{
    uint16_t address = BUS_Read16(cpu->nes, cpu->pc); // Absolute mode, read the address from memory
    cpu->pc += 2; // Increment program counter by 2
    return address;
}

uint16_t CPU_AbsoluteX(CPU *cpu) 
{
    uint16_t base_addr = BUS_Read16(cpu->nes, cpu->pc); // Read base address
    cpu->pc += 2; // Increment program counter by 2
    uint16_t final_addr = base_addr + cpu->x; // Add X register
    if ((base_addr & 0xFF00) != (final_addr & 0xFF00)) { // Check for page boundary crossing
        cpu->total_cycles++; // Add cycle penalty
    }
    return final_addr; // Return effective address
}

uint16_t CPU_AbsoluteY(CPU *cpu) 
{
    uint16_t base_addr = BUS_Read16(cpu->nes, cpu->pc); // Read base address
    cpu->pc += 2; // Increment program counter by 2
    uint16_t final_addr = base_addr + cpu->y; // Add Y register
    if ((base_addr & 0xFF00) != (final_addr & 0xFF00)) { // Check for page boundary crossing
        cpu->total_cycles++; // Add cycle penalty
    }
    return final_addr; // Return effective address
}

uint16_t CPU_Indirect(CPU *cpu) 
{
    uint16_t addr = BUS_Read16(cpu->nes, cpu->pc); // Read address from memory
    uint16_t lo = BUS_Read(cpu->nes, addr); // Read low byte
    uint16_t hi = BUS_Read(cpu->nes, (addr & 0xFF00) | ((addr + 1) & 0x00FF)); // Handle page boundary bug
    return (hi << 8) | lo; // Combine into 16-bit address
}

uint16_t CPU_IndexedIndirect(CPU *cpu) 
{
    uint8_t zp_addr = (BUS_Read(cpu->nes, cpu->pc++) + cpu->x) & 0xFF; // Add X and wrap around zero page
    uint8_t lo = BUS_Read(cpu->nes, zp_addr); // Read low byte
    uint8_t hi = BUS_Read(cpu->nes, (zp_addr + 1) & 0xFF); // Read high byte, wrap around zero page
    return (uint16_t)lo | ((uint16_t)hi << 8); // Combine into 16-bit address
}

uint16_t CPU_IndirectIndexed(CPU *cpu) 
{
    uint8_t zp_addr = BUS_Read(cpu->nes, cpu->pc++); // Read zero page address
    uint16_t base_addr = (uint16_t)BUS_Read(cpu->nes, zp_addr) | ((uint16_t)BUS_Read(cpu->nes, (zp_addr + 1) & 0xFF) << 8); // Handle zero-page wraparound
    uint16_t final_addr = base_addr + cpu->y; // Add Y register to base address

    // Check for page boundary crossing
    if ((base_addr & 0xFF00) != (final_addr & 0xFF00)) {
        cpu->total_cycles++; // Add cycle penalty for page crossing
    }

    return final_addr; // Return the effective address
}

// Load/Store Operations
void CPU_LDA(CPU *cpu, uint16_t address) 
{
    cpu->a = BUS_Read(cpu->nes, address); // Load A from memory
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_LDX(CPU *cpu, uint16_t address) 
{
    cpu->x = BUS_Read(cpu->nes, address); // Load X from memory
    CPU_UpdateZeroNegativeFlags(cpu, cpu->x); // Update flags
}

void CPU_LDY(CPU *cpu, uint16_t address) 
{
    cpu->y = BUS_Read(cpu->nes, address); // Load Y from memory
    CPU_UpdateZeroNegativeFlags(cpu, cpu->y); // Update flags
}

void CPU_STA(CPU *cpu, uint16_t address) 
{
    BUS_Write(cpu->nes, address, cpu->a); // Store A to memory
}

void CPU_STX(CPU *cpu, uint16_t address) 
{
    BUS_Write(cpu->nes, address, cpu->x); // Store X to memory
}

void CPU_STY(CPU *cpu, uint16_t address) 
{
    BUS_Write(cpu->nes, address, cpu->y); // Store Y to memory
}

// Register Transfer Operations
void CPU_TAX(CPU *cpu) 
{
    cpu->x = cpu->a; // Transfer A to X
    CPU_UpdateZeroNegativeFlags(cpu, cpu->x); // Update flags
}

void CPU_TAY(CPU *cpu) 
{
    cpu->y = cpu->a; // Transfer A to Y
    CPU_UpdateZeroNegativeFlags(cpu, cpu->y); // Update flags
}

void CPU_TSX(CPU *cpu) 
{
    cpu->x = cpu->sp; // Transfer SP to X
    CPU_UpdateZeroNegativeFlags(cpu, cpu->x); // Update flags
}

void CPU_TXA(CPU *cpu) 
{
    cpu->a = cpu->x; // Transfer X to A
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_TYA(CPU *cpu) 
{
    cpu->a = cpu->y; // Transfer Y to A
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_TXS(CPU *cpu) 
{
    cpu->sp = cpu->x; // Transfer X to SP
}

// Stack Operations
void CPU_PHA(CPU *cpu) 
{
    CPU_Push(cpu, cpu->a); // Push A to stack
}

void CPU_PLA(CPU *cpu) 
{
    cpu->a = CPU_Pop(cpu); // Pull A from stack
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_PHP(CPU *cpu) 
{
    CPU_Push(cpu, cpu->status | CPU_FLAG_BREAK | CPU_FLAG_UNUSED); // Push status to stack
}

void CPU_PLP(CPU *cpu) 
{
    cpu->status = CPU_Pop(cpu); // Pull status from stack
    CPU_SetFlag(cpu, CPU_FLAG_BREAK, 0); // Clear break flag
    CPU_SetFlag(cpu, CPU_FLAG_UNUSED, 1); // Set unused flag
}

// Decrement/Increment Operations
void CPU_DEC(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address) - 1; // Decrement memory
    BUS_Write(cpu->nes, address, value); // Write back to memory
    CPU_UpdateZeroNegativeFlags(cpu, value); // Update flags
}

void CPU_INC(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address) + 1; // Increment memory
    BUS_Write(cpu->nes, address, value); // Write back to memory
    CPU_UpdateZeroNegativeFlags(cpu, value); // Update flags
}

// Decrement/Increment Register Operations
void CPU_DEX(CPU *cpu) 
{
    cpu->x--; // Decrement X
    CPU_UpdateZeroNegativeFlags(cpu, cpu->x); // Update flags
}

void CPU_DEY(CPU *cpu) 
{
    cpu->y--; // Decrement Y
    CPU_UpdateZeroNegativeFlags(cpu, cpu->y); // Update flags
}

void CPU_INX(CPU *cpu) 
{
    cpu->x++; // Increment X
    CPU_UpdateZeroNegativeFlags(cpu, cpu->x); // Update flags
}

void CPU_INY(CPU *cpu) 
{
    cpu->y++; // Increment Y
    CPU_UpdateZeroNegativeFlags(cpu, cpu->y); // Update flags
}

// Arithmetic Operations
void CPU_ADC(CPU *cpu, uint16_t address) 
{
    uint8_t operand = BUS_Read(cpu->nes, address); // Read operand from memory
    uint8_t carry = CPU_GetFlag(cpu, CPU_FLAG_CARRY) ? 1 : 0; // Get carry flag
    uint16_t sum = (uint16_t)((uint32_t)cpu->a + (uint32_t)operand + (uint32_t)carry); // Calculate sum

    // Set carry flag if overflow occurs
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, sum > 0xFF);

    // Set overflow flag if the sign of the result is different from the sign of the operands
    CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, ((~(cpu->a ^ operand) & (cpu->a ^ (uint8_t)(sum & 0xFF))) & 0x80) != 0);

    cpu->a = (uint8_t)(sum & 0xFF); // Store result in A
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_SBC(CPU *cpu, uint16_t address) 
{
    uint16_t operand = BUS_Read(cpu->nes, address) ^ 0x00FF; // Read operand from memory
    uint8_t carry = CPU_GetFlag(cpu, CPU_FLAG_CARRY) ? 1 : 0; // Get carry flag
    uint16_t sum = (uint16_t)((uint32_t)cpu->a + (uint32_t)operand + (uint32_t)carry); // Calculate sum

    // Set carry flag if no borrow occurs
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, sum & 0xFF00);

    // Set overflow flag if the sign of the result is different from the sign of the operands
    CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, (sum ^ (uint16_t)cpu->a) & (sum ^ operand) & 0x0080);

    cpu->a = (uint8_t)(sum & 0xFF); // Store result in A
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

// Shift Operations
void CPU_ASL(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (value & 0x80) != 0); // Set carry flag
    value <<= 1; // Shift left
    BUS_Write(cpu->nes, address, value); // Write back to memory
    CPU_UpdateZeroNegativeFlags(cpu, value); // Update flags
}

void CPU_ASL_A(CPU *cpu) 
{
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (cpu->a & 0x80) != 0); // Set carry flag
    cpu->a <<= 1; // Shift left
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_LSR(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (value & 0x01) != 0); // Set carry flag
    value >>= 1; // Shift right
    BUS_Write(cpu->nes, address, value); // Write back to memory
    CPU_UpdateZeroNegativeFlags(cpu, value); // Update flags
}

void CPU_LSR_A(CPU *cpu) 
{
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (cpu->a & 0x01) != 0); // Set carry flag
    cpu->a >>= 1; // Shift right
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_ROL(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    int old_carry = CPU_GetFlag(cpu, CPU_FLAG_CARRY); // Get old carry flag
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (value & 0x80) != 0); // Set carry flag
    value <<= 1; // Shift left
    if (old_carry) value |= 0x01; // Set bit 0 if old carry was set
    BUS_Write(cpu->nes, address, value); // Write back to memory
    CPU_UpdateZeroNegativeFlags(cpu, value); // Update flags
}

void CPU_ROL_A(CPU *cpu) 
{
    int old_carry = CPU_GetFlag(cpu, CPU_FLAG_CARRY); // Get old carry flag
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (cpu->a & 0x80) != 0); // Set carry flag
    cpu->a <<= 1; // Shift left
    if (old_carry) cpu->a |= 0x01; // Set bit 0 if old carry was set
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_ROR(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    int old_carry = CPU_GetFlag(cpu, CPU_FLAG_CARRY); // Get old carry flag
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (value & 0x01) != 0); // Set carry flag
    value >>= 1; // Shift right
    if (old_carry) value |= 0x80; // Set bit 7 if old carry was set
    BUS_Write(cpu->nes, address, value); // Write back to memory
    CPU_UpdateZeroNegativeFlags(cpu, value); // Update flags
}

void CPU_ROR_A(CPU *cpu) 
{
    int old_carry = CPU_GetFlag(cpu, CPU_FLAG_CARRY); // Get old carry flag
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (cpu->a & 0x01) != 0); // Set carry flag
    cpu->a >>= 1; // Shift right
    if (old_carry) cpu->a |= 0x80; // Set bit 7 if old carry was set
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

// Logic Operations
void CPU_AND(CPU *cpu, uint16_t address) 
{
    cpu->a &= BUS_Read(cpu->nes, address); // AND A with memory
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_EOR(CPU *cpu, uint16_t address) 
{
    cpu->a ^= BUS_Read(cpu->nes, address); // EOR A with memory
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_ORA(CPU *cpu, uint16_t address) 
{
    cpu->a |= BUS_Read(cpu->nes, address); // OR A with memory
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_BIT(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    CPU_SetFlag(cpu, CPU_FLAG_ZERO, (cpu->a & value) == 0); // Set zero flag if result is zero
    CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, (value & 0x40) != 0); // Set overflow flag if bit 6 is set
    CPU_SetFlag(cpu, CPU_FLAG_NEGATIVE, (value & 0x80) != 0); // Set negative flag if bit 7 is set
}

// Compare Operations
void CPU_CPX(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    uint16_t result = (uint16_t)cpu->x - (uint16_t)value; // Compare X with memory
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->x >= value); // Set carry flag if no borrow
    CPU_UpdateZeroNegativeFlags(cpu, (uint8_t)(result & 0xFF)); // Update flags
}

void CPU_CPY(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    uint16_t result = (uint16_t)cpu->y - (uint16_t)value; // Compare Y with memory
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->y >= value); // Set carry flag if no borrow
    CPU_UpdateZeroNegativeFlags(cpu, (uint8_t)(result & 0xFF)); // Update flags
}

void CPU_CMP(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    uint16_t result = (uint16_t)cpu->a - (uint16_t)value; // Compare A with memory
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->a >= value); // Set carry flag if A >= value (fixed)
    CPU_UpdateZeroNegativeFlags(cpu, (uint8_t)(result & 0xFF)); // Update flags
}

// Branch Operations
void CPU_BCC(CPU *cpu, uint16_t address) 
{
    if (!CPU_GetFlag(cpu, CPU_FLAG_CARRY)) { // Branch if carry flag is clear
        uint16_t old_pc = cpu->pc; // Store old program counter
        cpu->pc = address; // Set program counter to address
        cpu->total_cycles++; // Add cycle for branch taken
        if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) { // Check if page boundary is crossed
            cpu->total_cycles++; // Add cycle penalty for page crossing
        }
    }
}

void CPU_BCS(CPU *cpu, uint16_t address) 
{
    if (CPU_GetFlag(cpu, CPU_FLAG_CARRY)) { // Branch if carry flag is set
        uint16_t old_pc = cpu->pc; // Store old program counter
        cpu->pc = address; // Set program counter to address
        cpu->total_cycles++; // Add cycle for branch taken
        if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) { // Check if page boundary is crossed
            cpu->total_cycles++; // Add cycle penalty for page crossing
        }
    }
}

void CPU_BEQ(CPU *cpu, uint16_t address) 
{
    if (CPU_GetFlag(cpu, CPU_FLAG_ZERO)) { // Branch if zero flag is set
        uint16_t old_pc = cpu->pc; // Store old program counter
        cpu->pc = address; // Set program counter to address
        cpu->total_cycles++; // Add cycle for branch taken
        if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) { // Check if page boundary is crossed
            cpu->total_cycles++; // Add cycle penalty for page crossing
        }
    }
}

void CPU_BNE(CPU *cpu, uint16_t address) 
{
    if (!CPU_GetFlag(cpu, CPU_FLAG_ZERO)) { // Branch if zero flag is clear
        uint16_t old_pc = cpu->pc; // Store old program counter
        cpu->pc = address; // Set program counter to address
        cpu->total_cycles++; // Add cycle for branch taken
        if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) { // Check if page boundary is crossed
            cpu->total_cycles++; // Add cycle penalty for page crossing
        }
    }
}

void CPU_BMI(CPU *cpu, uint16_t address) 
{
    if (CPU_GetFlag(cpu, CPU_FLAG_NEGATIVE)) { // Branch if negative flag is set
        uint16_t old_pc = cpu->pc; // Store old program counter
        cpu->pc = address; // Set program counter to address
        cpu->total_cycles++; // Add cycle for branch taken
        if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) { // Check if page boundary is crossed
            cpu->total_cycles++; // Add cycle penalty for page crossing
        }
    }
}

void CPU_BPL(CPU *cpu, uint16_t address) 
{
    if (!CPU_GetFlag(cpu, CPU_FLAG_NEGATIVE)) { // Branch if negative flag is clear
        uint16_t old_pc = cpu->pc; // Store old program counter
        cpu->pc = address; // Set program counter to address
        cpu->total_cycles++; // Add cycle for branch taken
        if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) { // Check if page boundary is crossed
            cpu->total_cycles++; // Add cycle penalty for page crossing
        }
    }
}

void CPU_BVS(CPU *cpu, uint16_t address) 
{
    if (CPU_GetFlag(cpu, CPU_FLAG_OVERFLOW)) 
    { // Branch if overflow flag is set
        uint16_t old_pc = cpu->pc; // Store old program counter
        cpu->pc = address; // Set program counter to address
        cpu->total_cycles++; // Add cycle for branch taken
        if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) 
        { // Check if page boundary is crossed
            cpu->total_cycles++; // Add cycle penalty for page crossing
        }
    }
}

void CPU_BVC(CPU *cpu, uint16_t address) 
{
    if (!CPU_GetFlag(cpu, CPU_FLAG_OVERFLOW)) { // Branch if overflow flag is clear
        uint16_t old_pc = cpu->pc; // Store old program counter
        cpu->pc = address; // Set program counter to address
        cpu->total_cycles++; // Add cycle for branch taken
        if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) { // Check if page boundary is crossed
            cpu->total_cycles++; // Add cycle penalty for page crossing
        }
    }
}

// Jump Operations
void CPU_JMP(CPU *cpu, uint16_t address) 
{
    cpu->pc = address; // Jump to address
}

void CPU_JMP_IND(CPU *cpu, uint16_t address) 
{
    uint16_t addr = BUS_Read16(cpu->nes, address); // Read address from memory
    cpu->pc = addr; // Jump to address
}

void CPU_JSR(CPU *cpu, uint16_t address) 
{
    CPU_Push16(cpu, cpu->pc - 1); // Push decremented program counter to stack
    cpu->pc = address; // Jump to address
}

void CPU_RTS(CPU *cpu) 
{
    cpu->pc = CPU_Pop16(cpu) + 1; // Pull program counter from stack and increment
}

void CPU_RTI(CPU *cpu) 
{
    cpu->status = CPU_Pop(cpu); // Pull status from stack
    cpu->status &= (uint8_t)~CPU_FLAG_BREAK; // Clear break flag
    cpu->status |= CPU_FLAG_UNUSED; // Set unused flag
    cpu->pc = CPU_Pop16(cpu); // Pull program counter from stack
}

// Interrupt Operations
void CPU_IRQ(CPU *cpu) 
{
    if (!CPU_GetFlag(cpu, CPU_FLAG_INTERRUPT)) { // Check if interrupts are enabled
        CPU_Push16(cpu, cpu->pc); // Push program counter to stack
        CPU_Push(cpu, cpu->status & (uint8_t)~CPU_FLAG_BREAK); // Push status to stack with BREAK flag cleared
        CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, 1); // Set interrupt flag
        cpu->pc = BUS_Read16(cpu->nes, 0xFFFE); // Read interrupt vector
    }
}

void CPU_NMI(CPU *cpu) 
{
    CPU_Push16(cpu, cpu->pc); // Push program counter to stack
    CPU_Push(cpu, (uint8_t)(cpu->status & (uint8_t)~CPU_FLAG_BREAK)); // Push status to stack with BREAK flag cleared (fixed)
    CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, 1); // Set interrupt flag
    cpu->pc = BUS_Read16(cpu->nes, 0xFFFA); // Read NMI vector
}

void CPU_BRK(CPU *cpu) 
{
    CPU_Push16(cpu, cpu->pc); // Push program counter to stack
    CPU_Push(cpu, cpu->status | CPU_FLAG_BREAK); // Push status to stack with BREAK flag set (fixed)
    CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, 1); // Set interrupt flag
    cpu->pc = BUS_Read16(cpu->nes, 0xFFFE); // Read interrupt vector
}

void CPU_NOP(CPU *cpu) 
{
    // No operation
}

// Flag Operations
void CPU_SEI(CPU *cpu) 
{
    CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, 1); // Set interrupt flag
}

void CPU_CLI(CPU *cpu) 
{
    CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, 0); // Clear interrupt flag
}

void CPU_CLV(CPU *cpu) 
{
    CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, 0); // Clear overflow flag
}

void CPU_CLD(CPU *cpu) 
{
    CPU_SetFlag(cpu, CPU_FLAG_DECIMAL, 0); // Clear decimal mode flag
}

void CPU_SED(CPU *cpu) 
{
    CPU_SetFlag(cpu, CPU_FLAG_DECIMAL, 1); // Set decimal mode flag
}

void CPU_SEP(CPU *cpu, uint8_t value) 
{
    cpu->status |= value; // Set status flags
}

void CPU_CLC(CPU *cpu) 
{
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, 0); // Clear carry flag
}

void CPU_SEC(CPU *cpu) 
{
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, 1); // Set carry flag
}

// Unofficial Opcodes
void CPU_LAX(CPU *cpu, uint16_t address) 
{
    cpu->a = BUS_Read(cpu->nes, address); // Load A from memory
    cpu->x = cpu->a; // Transfer A to X
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_SAX(CPU *cpu, uint16_t address) 
{
    BUS_Write(cpu->nes, address, cpu->a & cpu->x); // Store A AND X to memory
}

void CPU_AYX(CPU *cpu, uint16_t address) 
{
    cpu->y = cpu->a; // Transfer A to Y
    cpu->x = cpu->y; // Transfer Y to X
    CPU_UpdateZeroNegativeFlags(cpu, cpu->y); // Update flags
}

void CPU_ARR(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    uint8_t carry = CPU_GetFlag(cpu, CPU_FLAG_CARRY) ? 1 : 0; // Get carry flag
    uint16_t sum = (uint16_t)((uint32_t)cpu->a + (uint32_t)value + (uint32_t)carry); // Calculate sum

    // Set carry flag if overflow occurs
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, sum > 0xFF);

    // Set overflow flag if the sign of the result is different from the sign of the operands
    CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, ((~(cpu->a ^ value) & (cpu->a ^ (uint8_t)(sum & 0xFF))) & 0x80) != 0);

    cpu->a = (uint8_t)(sum & 0xFF); // Store result in A
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_SLO(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (value & 0x80) != 0); // Set carry flag
    value <<= 1; // Shift left
    BUS_Write(cpu->nes, address, value); // Write back to memory
    cpu->a |= value; // OR A with shifted value
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_RLA(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    int old_carry = CPU_GetFlag(cpu, CPU_FLAG_CARRY); // Get old carry flag
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (value & 0x80) != 0); // Set carry flag
    value <<= 1; // Shift left
    if (old_carry) value |= 0x01; // Set bit 0 if old carry was set
    BUS_Write(cpu->nes, address, value); // Write back to memory
    cpu->a &= value; // AND A with shifted value
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_SRE(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (value & 0x01) != 0); // Set carry flag
    value >>= 1; // Shift right
    BUS_Write(cpu->nes, address, value); // Write back to memory
    cpu->a ^= value; // EOR A with shifted value
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_RRA(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    int old_carry = CPU_GetFlag(cpu, CPU_FLAG_CARRY); // Get old carry flag
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (value & 0x01) != 0); // Set carry flag from bit 0
    value >>= 1; // Shift right
    if (old_carry) value |= 0x80; // Set bit 7 if old carry was set
    BUS_Write(cpu->nes, address, value); // Write back to memory

    // Use the updated carry flag for ADC
    uint8_t carry_in = CPU_GetFlag(cpu, CPU_FLAG_CARRY) ? 1 : 0;
    uint16_t result = (uint16_t)((uint32_t)cpu->a + (uint32_t)value + (uint32_t)carry_in); // Add with carry
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, result > 0xFF); // Set carry flag if overflow occurs
    CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, ((~(cpu->a ^ value) & (cpu->a ^ (uint8_t)result)) & 0x80) != 0); // Set overflow flag (ADC logic)
    cpu->a = (uint8_t)result; // Store result in A
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_DCP(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    value--; // Decrement memory
    BUS_Write(cpu->nes, address, value); // Write back to memory

    uint16_t result = (uint16_t)cpu->a - (uint16_t)value; // Compare A with memory
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->a >= value); // Set carry flag if no borrow
    CPU_UpdateZeroNegativeFlags(cpu, (uint8_t)result); // Update flags using the result
}

void CPU_ISC(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    value++; // Increment memory
    BUS_Write(cpu->nes, address, value); // Write back to memory

    uint16_t result = (uint16_t)cpu->a - (uint16_t)value - (CPU_GetFlag(cpu, CPU_FLAG_CARRY) ? 0 : 1); // Subtract with carry
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->a >= value); // Set carry flag if no borrow
    CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, ((cpu->a ^ value) & (cpu->a ^ (uint8_t)result) & 0x80) != 0); // Set overflow flag
    cpu->a = (uint8_t)result; // Store result in A
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_ANC(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    cpu->a &= value; // AND A with memory
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (cpu->a & 0x80) != 0); // Set carry flag if bit 7 is set
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_ALR(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (value & 0x01) != 0); // Set carry flag
    value >>= 1; // Shift right
    cpu->a &= value; // AND A with shifted value
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_SBX(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    uint16_t result = (uint16_t)cpu->x - (uint16_t)value; // Compare X with memory
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->x >= value); // Set carry flag if no borrow
    CPU_UpdateZeroNegativeFlags(cpu, (uint8_t)(result & 0xFF)); // Update flags

    cpu->x -= value; // Decrement X
}

void CPU_SHY(CPU *cpu, uint16_t address) 
{
    // Store Y & (high byte of address + 1) (fixed)
    uint8_t value = cpu->y & ((address >> 8) + 1);
    BUS_Write(cpu->nes, address, value);
}

void CPU_SHX(CPU *cpu, uint16_t address) 
{
    // Store X & (high byte of address + 1) (fixed)
    uint8_t value = cpu->x & ((address >> 8) + 1);
    BUS_Write(cpu->nes, address, value);
}

void CPU_LAS(CPU *cpu, uint16_t address) 
{
    uint8_t value = BUS_Read(cpu->nes, address); // Read value from memory
    cpu->a &= value; // AND A with memory
    cpu->x = cpu->a; // Transfer A to X
    cpu->y = cpu->a; // Transfer A to Y
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Update flags
}

void CPU_TAS(CPU *cpu) 
{
    cpu->sp = cpu->a; // Transfer A to SP
    CPU_UpdateZeroNegativeFlags(cpu, cpu->sp); // Update flags
}

int CPU_Step(CPU *cpu) 
{
    uint16_t initial_pc = cpu->pc; // For debugging/logging
    
    uint8_t opcode = BUS_Read(cpu->nes, cpu->pc);
    cpu->pc++; // Increment PC past opcode

    uint16_t addr = 0; // Effective address for operand
    int cycles = 2;    // Default cycles (most common)

    switch (opcode) 
    {
        case 0xA9: addr = CPU_Immediate(cpu);      CPU_LDA(cpu, addr); cycles=2; break;
        case 0xA5: addr = CPU_ZeroPage(cpu);       CPU_LDA(cpu, addr); cycles=3; break;
        case 0xB5: addr = CPU_ZeroPageX(cpu);      CPU_LDA(cpu, addr); cycles=4; break;
        case 0xAD: addr = CPU_Absolute(cpu);       CPU_LDA(cpu, addr); cycles=4; break;
        case 0xBD: addr = CPU_AbsoluteX(cpu);      CPU_LDA(cpu, addr); cycles=4; break; // +1 page cross
        case 0xB9: addr = CPU_AbsoluteY(cpu);      CPU_LDA(cpu, addr); cycles=4; break; // +1 page cross
        case 0xA1: addr = CPU_IndexedIndirect(cpu);CPU_LDA(cpu, addr); cycles=6; break;
        case 0xB1: addr = CPU_IndirectIndexed(cpu);CPU_LDA(cpu, addr); cycles=5; break; // +1 page cross

        case 0xA2: addr = CPU_Immediate(cpu);      CPU_LDX(cpu, addr); cycles=2; break;
        case 0xA6: addr = CPU_ZeroPage(cpu);       CPU_LDX(cpu, addr); cycles=3; break;
        case 0xB6: addr = CPU_ZeroPageY(cpu);      CPU_LDX(cpu, addr); cycles=4; break;
        case 0xAE: addr = CPU_Absolute(cpu);       CPU_LDX(cpu, addr); cycles=4; break;
        case 0xBE: addr = CPU_AbsoluteY(cpu);      CPU_LDX(cpu, addr); cycles=4; break; // +1 page cross

        case 0xA0: addr = CPU_Immediate(cpu);      CPU_LDY(cpu, addr); cycles=2; break;
        case 0xA4: addr = CPU_ZeroPage(cpu);       CPU_LDY(cpu, addr); cycles=3; break;
        case 0xB4: addr = CPU_ZeroPageX(cpu);      CPU_LDY(cpu, addr); cycles=4; break;
        case 0xAC: addr = CPU_Absolute(cpu);       CPU_LDY(cpu, addr); cycles=4; break;
        case 0xBC: addr = CPU_AbsoluteX(cpu);      CPU_LDY(cpu, addr); cycles=4; break; // +1 page cross

        case 0x85: addr = CPU_ZeroPage(cpu);       CPU_STA(cpu, addr); cycles=3; break;
        case 0x95: addr = CPU_ZeroPageX(cpu);      CPU_STA(cpu, addr); cycles=4; break;
        case 0x8D: addr = CPU_Absolute(cpu);       CPU_STA(cpu, addr); cycles=4; break;
        case 0x9D: addr = CPU_AbsoluteX(cpu);      CPU_STA(cpu, addr); cycles=5; if ((addr & 0xFF00) != ((addr - cpu->x) & 0xFF00)) cycles--; break;
        case 0x99: addr = CPU_AbsoluteY(cpu);      CPU_STA(cpu, addr); cycles=5; if ((addr & 0xFF00) != ((addr - cpu->y) & 0xFF00)) cycles--; break;
        case 0x81: addr = CPU_IndexedIndirect(cpu);CPU_STA(cpu, addr); cycles=6; break;
        case 0x91: addr = CPU_IndirectIndexed(cpu);CPU_STA(cpu, addr); cycles=6; break;

        case 0x86: addr = CPU_ZeroPage(cpu);       CPU_STX(cpu, addr); cycles=3; break;
        case 0x96: addr = CPU_ZeroPageY(cpu);      CPU_STX(cpu, addr); cycles=4; break;
        case 0x8E: addr = CPU_Absolute(cpu);       CPU_STX(cpu, addr); cycles=4; break;

        case 0x84: addr = CPU_ZeroPage(cpu);       CPU_STY(cpu, addr); cycles=3; break;
        case 0x94: addr = CPU_ZeroPageX(cpu);      CPU_STY(cpu, addr); cycles=4; break;
        case 0x8C: addr = CPU_Absolute(cpu);       CPU_STY(cpu, addr); cycles=4; break;

        case 0xAA: addr = CPU_Implied(cpu);        CPU_TAX(cpu); cycles=2; break;
        case 0xA8: addr = CPU_Implied(cpu);        CPU_TAY(cpu); cycles=2; break;
        case 0x8A: addr = CPU_Implied(cpu);        CPU_TXA(cpu); cycles=2; break;
        case 0x98: addr = CPU_Implied(cpu);        CPU_TYA(cpu); cycles=2; break;
        case 0xBA: addr = CPU_Implied(cpu);        CPU_TSX(cpu); cycles=2; break;
        case 0x9A: addr = CPU_Implied(cpu);        CPU_TXS(cpu); cycles=2; break;

        case 0x48: addr = CPU_Implied(cpu);        CPU_PHA(cpu); cycles=3; break;
        case 0x68: addr = CPU_Implied(cpu);        CPU_PLA(cpu); cycles=4; break;
        case 0x08: addr = CPU_Implied(cpu);        CPU_PHP(cpu); cycles=3; break;
        case 0x28: addr = CPU_Implied(cpu);        CPU_PLP(cpu); cycles=4; break;

        case 0xC6: addr = CPU_ZeroPage(cpu);       CPU_DEC(cpu, addr); cycles=5; break;
        case 0xD6: addr = CPU_ZeroPageX(cpu);      CPU_DEC(cpu, addr); cycles=6; break;
        case 0xCE: addr = CPU_Absolute(cpu);       CPU_DEC(cpu, addr); cycles=6; break;
        case 0xDE: addr = CPU_AbsoluteX(cpu);      CPU_DEC(cpu, addr); cycles=7; break;
        case 0xCA: addr = CPU_Implied(cpu);        CPU_DEX(cpu); cycles=2; break;
        case 0x88: addr = CPU_Implied(cpu);        CPU_DEY(cpu); cycles=2; break;

        case 0xE6: addr = CPU_ZeroPage(cpu);       CPU_INC(cpu, addr); cycles=5; break;
        case 0xF6: addr = CPU_ZeroPageX(cpu);      CPU_INC(cpu, addr); cycles=6; break;
        case 0xEE: addr = CPU_Absolute(cpu);       CPU_INC(cpu, addr); cycles=6; break;
        case 0xFE: addr = CPU_AbsoluteX(cpu);      CPU_INC(cpu, addr); cycles=7; break;
        case 0xE8: addr = CPU_Implied(cpu);        CPU_INX(cpu); cycles=2; break;
        case 0xC8: addr = CPU_Implied(cpu);        CPU_INY(cpu); cycles=2; break;

        case 0x69: addr = CPU_Immediate(cpu);      CPU_ADC(cpu, addr); cycles=2; break;
        case 0x65: addr = CPU_ZeroPage(cpu);       CPU_ADC(cpu, addr); cycles=3; break;
        case 0x75: addr = CPU_ZeroPageX(cpu);      CPU_ADC(cpu, addr); cycles=4; break;
        case 0x6D: addr = CPU_Absolute(cpu);       CPU_ADC(cpu, addr); cycles=4; break;
        case 0x7D: addr = CPU_AbsoluteX(cpu);      CPU_ADC(cpu, addr); cycles=4; break; // +1 page cross
        case 0x79: addr = CPU_AbsoluteY(cpu);      CPU_ADC(cpu, addr); cycles=4; break; // +1 page cross
        case 0x61: addr = CPU_IndexedIndirect(cpu);CPU_ADC(cpu, addr); cycles=6; break;
        case 0x71: addr = CPU_IndirectIndexed(cpu);CPU_ADC(cpu, addr); cycles=5; break; // +1 page cross

        case 0xE9: addr = CPU_Immediate(cpu);      CPU_SBC(cpu, addr); cycles=2; break;
        case 0xE5: addr = CPU_ZeroPage(cpu);       CPU_SBC(cpu, addr); cycles=3; break;
        case 0xF5: addr = CPU_ZeroPageX(cpu);      CPU_SBC(cpu, addr); cycles=4; break;
        case 0xED: addr = CPU_Absolute(cpu);       CPU_SBC(cpu, addr); cycles=4; break;
        case 0xFD: addr = CPU_AbsoluteX(cpu);      CPU_SBC(cpu, addr); cycles=4; break; // +1 if page crossed
        case 0xF9: addr = CPU_AbsoluteY(cpu);      CPU_SBC(cpu, addr); cycles=4; break; // +1 if page crossed
        case 0xE1: addr = CPU_IndexedIndirect(cpu);CPU_SBC(cpu, addr); cycles=6; break;
        case 0xF1: addr = CPU_IndirectIndexed(cpu);CPU_SBC(cpu, addr); cycles=5; break; // +1 if page crossed
        case 0xEB: addr = CPU_Immediate(cpu);      CPU_SBC(cpu, addr); cycles=2; break; // Unofficial SBC

        case 0xC9: addr = CPU_Immediate(cpu);      CPU_CMP(cpu, addr); cycles=2; break;
        case 0xC5: addr = CPU_ZeroPage(cpu);       CPU_CMP(cpu, addr); cycles=3; break;
        case 0xD5: addr = CPU_ZeroPageX(cpu);      CPU_CMP(cpu, addr); cycles=4; break;
        case 0xCD: addr = CPU_Absolute(cpu);       CPU_CMP(cpu, addr); cycles=4; break;
        case 0xDD: addr = CPU_AbsoluteX(cpu);      CPU_CMP(cpu, addr); cycles=4; break; // +1 page cross
        case 0xD9: addr = CPU_AbsoluteY(cpu);      CPU_CMP(cpu, addr); cycles=4; break; // +1 page cross
        case 0xC1: addr = CPU_IndexedIndirect(cpu);CPU_CMP(cpu, addr); cycles=6; break;
        case 0xD1: addr = CPU_IndirectIndexed(cpu);CPU_CMP(cpu, addr); cycles=5; break; // +1 page cross

        case 0xE0: addr = CPU_Immediate(cpu);      CPU_CPX(cpu, addr); cycles=2; break;
        case 0xE4: addr = CPU_ZeroPage(cpu);       CPU_CPX(cpu, addr); cycles=3; break;
        case 0xEC: addr = CPU_Absolute(cpu);       CPU_CPX(cpu, addr); cycles=4; break;

        case 0xC0: addr = CPU_Immediate(cpu);      CPU_CPY(cpu, addr); cycles=2; break;
        case 0xC4: addr = CPU_ZeroPage(cpu);       CPU_CPY(cpu, addr); cycles=3; break;
        case 0xCC: addr = CPU_Absolute(cpu);       CPU_CPY(cpu, addr); cycles=4; break;

        case 0x29: addr = CPU_Immediate(cpu);      CPU_AND(cpu, addr); cycles=2; break;
        case 0x25: addr = CPU_ZeroPage(cpu);       CPU_AND(cpu, addr); cycles=3; break;
        case 0x35: addr = CPU_ZeroPageX(cpu);      CPU_AND(cpu, addr); cycles=4; break;
        case 0x2D: addr = CPU_Absolute(cpu);       CPU_AND(cpu, addr); cycles=4; break;
        case 0x3D: addr = CPU_AbsoluteX(cpu);      CPU_AND(cpu, addr); cycles=4; break; // +1 page cross
        case 0x39: addr = CPU_AbsoluteY(cpu);      CPU_AND(cpu, addr); cycles=4; break; // +1 page cross
        case 0x21: addr = CPU_IndexedIndirect(cpu);CPU_AND(cpu, addr); cycles=6; break;
        case 0x31: addr = CPU_IndirectIndexed(cpu);CPU_AND(cpu, addr); cycles=5; break; // +1 page cross

        case 0x49: addr = CPU_Immediate(cpu);      CPU_EOR(cpu, addr); cycles=2; break;
        case 0x45: addr = CPU_ZeroPage(cpu);       CPU_EOR(cpu, addr); cycles=3; break;
        case 0x55: addr = CPU_ZeroPageX(cpu);      CPU_EOR(cpu, addr); cycles=4; break;
        case 0x4D: addr = CPU_Absolute(cpu);       CPU_EOR(cpu, addr); cycles=4; break;
        case 0x5D: addr = CPU_AbsoluteX(cpu);      CPU_EOR(cpu, addr); cycles=4; break; // +1 page cross
        case 0x59: addr = CPU_AbsoluteY(cpu);      CPU_EOR(cpu, addr); cycles=4; break; // +1 page cross
        case 0x41: addr = CPU_IndexedIndirect(cpu);CPU_EOR(cpu, addr); cycles=6; break;
        case 0x51: addr = CPU_IndirectIndexed(cpu);CPU_EOR(cpu, addr); cycles=5; break; // +1 page cross

        case 0x09: addr = CPU_Immediate(cpu);      CPU_ORA(cpu, addr); cycles=2; break;
        case 0x05: addr = CPU_ZeroPage(cpu);       CPU_ORA(cpu, addr); cycles=3; break;
        case 0x15: addr = CPU_ZeroPageX(cpu);      CPU_ORA(cpu, addr); cycles=4; break;
        case 0x0D: addr = CPU_Absolute(cpu);       CPU_ORA(cpu, addr); cycles=4; break;
        case 0x1D: addr = CPU_AbsoluteX(cpu);      CPU_ORA(cpu, addr); cycles=4; break; // +1 page cross
        case 0x19: addr = CPU_AbsoluteY(cpu);      CPU_ORA(cpu, addr); cycles=4; break; // +1 page cross
        case 0x01: addr = CPU_IndexedIndirect(cpu);CPU_ORA(cpu, addr); cycles=6; break;
        case 0x11: addr = CPU_IndirectIndexed(cpu);CPU_ORA(cpu, addr); cycles=5; break; // +1 page cross

        case 0x24: addr = CPU_ZeroPage(cpu);       CPU_BIT(cpu, addr); cycles=3; break;
        case 0x2C: addr = CPU_Absolute(cpu);       CPU_BIT(cpu, addr); cycles=4; break;

        case 0x0A: addr = CPU_Accumulator(cpu);    CPU_ASL_A(cpu); cycles=2; break;
        case 0x06: addr = CPU_ZeroPage(cpu);       CPU_ASL(cpu, addr); cycles=5; break;
        case 0x16: addr = CPU_ZeroPageX(cpu);      CPU_ASL(cpu, addr); cycles=6; break;
        case 0x0E: addr = CPU_Absolute(cpu);       CPU_ASL(cpu, addr); cycles=6; break;
        case 0x1E: addr = CPU_AbsoluteX(cpu);      CPU_ASL(cpu, addr); cycles=7; break;

        case 0x4A: addr = CPU_Accumulator(cpu);    CPU_LSR_A(cpu); cycles=2; break;
        case 0x46: addr = CPU_ZeroPage(cpu);       CPU_LSR(cpu, addr); cycles=5; break;
        case 0x56: addr = CPU_ZeroPageX(cpu);      CPU_LSR(cpu, addr); cycles=6; break;
        case 0x4E: addr = CPU_Absolute(cpu);       CPU_LSR(cpu, addr); cycles=6; break;
        case 0x5E: addr = CPU_AbsoluteX(cpu);      CPU_LSR(cpu, addr); cycles=7; break;

        case 0x2A: addr = CPU_Accumulator(cpu);    CPU_ROL_A(cpu); cycles=2; break;
        case 0x26: addr = CPU_ZeroPage(cpu);       CPU_ROL(cpu, addr); cycles=5; break;
        case 0x36: addr = CPU_ZeroPageX(cpu);      CPU_ROL(cpu, addr); cycles=6; break;
        case 0x2E: addr = CPU_Absolute(cpu);       CPU_ROL(cpu, addr); cycles=6; break;
        case 0x3E: addr = CPU_AbsoluteX(cpu);      CPU_ROL(cpu, addr); cycles=7; break;

        case 0x6A: addr = CPU_Accumulator(cpu);    CPU_ROR_A(cpu); cycles=2; break;
        case 0x66: addr = CPU_ZeroPage(cpu);       CPU_ROR(cpu, addr); cycles=5; break;
        case 0x76: addr = CPU_ZeroPageX(cpu);      CPU_ROR(cpu, addr); cycles=6; break;
        case 0x6E: addr = CPU_Absolute(cpu);       CPU_ROR(cpu, addr); cycles=6; break;
        case 0x7E: addr = CPU_AbsoluteX(cpu);      CPU_ROR(cpu, addr); cycles=7; break;
        case 0x90: addr = CPU_Relative(cpu);       CPU_BCC(cpu, addr); cycles=2; break; // +1/+2
        case 0xB0: addr = CPU_Relative(cpu);       CPU_BCS(cpu, addr); cycles=2; break; // +1/+2
        case 0xF0: addr = CPU_Relative(cpu);       CPU_BEQ(cpu, addr); cycles=2; break; // +1/+2
        case 0x30: addr = CPU_Relative(cpu);       CPU_BMI(cpu, addr); cycles=2; break; // +1/+2
        case 0xD0: addr = CPU_Relative(cpu);       CPU_BNE(cpu, addr); cycles=2; break; // +1/+2
        case 0x10: addr = CPU_Relative(cpu);       CPU_BPL(cpu, addr); cycles=2; break; // +1/+2
        case 0x50: addr = CPU_Relative(cpu);       CPU_BVC(cpu, addr); cycles=2; break; // +1/+2
        case 0x70: addr = CPU_Relative(cpu);       CPU_BVS(cpu, addr); cycles=2; break; // +1/+2
        
        case 0x4C: addr = CPU_Absolute(cpu);       CPU_JMP(cpu, addr); cycles=3; break;
        case 0x6C: addr = CPU_Indirect(cpu);       CPU_JMP(cpu, addr); cycles=5; break;
        case 0x20: addr = CPU_Absolute(cpu);       CPU_JSR(cpu, addr); cycles=6; break;
        case 0x60: addr = CPU_Implied(cpu);        CPU_RTS(cpu); cycles=6; break;

        case 0x00: addr = CPU_Implied(cpu);        CPU_BRK(cpu); cycles=7; break;
        case 0x40: addr = CPU_Implied(cpu);        CPU_RTI(cpu); cycles=6; break;

        case 0x18: addr = CPU_Implied(cpu);        CPU_CLC(cpu); cycles=2; break;
        case 0x38: addr = CPU_Implied(cpu);        CPU_SEC(cpu); cycles=2; break;
        case 0x58: addr = CPU_Implied(cpu);        CPU_CLI(cpu); cycles=2; break;
        case 0x78: addr = CPU_Implied(cpu);        CPU_SEI(cpu); cycles=2; break;
        case 0xB8: addr = CPU_Implied(cpu);        CPU_CLV(cpu); cycles=2; break;
        case 0xD8: addr = CPU_Implied(cpu);        CPU_CLD(cpu); cycles=2; break;
        case 0xF8: addr = CPU_Implied(cpu);        CPU_SED(cpu); cycles=2; break;

        case 0xEA: addr = CPU_Implied(cpu);        CPU_NOP(cpu); cycles=2; break;

        // Unofficial Opcodes

        // KIL/JAM Opcodes (Halt execution)
        case 0x02: case 0x12: case 0x22: case 0x32: case 0x42: case 0x52: 
        case 0x62: case 0x72: case 0x92: case 0xB2: case 0xD2: case 0xF2:
            DEBUG_DEBUG("KIL/JAM opcode encountered: 0x%02X at PC: 0x%04X", opcode, initial_pc);
            return -1;

        // NOPs (Various addressing modes, different cycle counts)
        case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA: // NOP implied
            addr = CPU_Implied(cpu); CPU_NOP(cpu); cycles = 2; break;
        case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2: // NOP immediate
            addr = CPU_Immediate(cpu); CPU_NOP(cpu); cycles = 2; break;
        case 0x04: case 0x44: case 0x64: // NOP zero page
            addr = CPU_ZeroPage(cpu); CPU_NOP(cpu); cycles = 3; break;
        case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4: // NOP zero page, X
            addr = CPU_ZeroPageX(cpu); CPU_NOP(cpu); cycles = 4; break;
        case 0x0C: // NOP absolute
            addr = CPU_Absolute(cpu); CPU_NOP(cpu); cycles = 4; break;
        case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC: // NOP absolute, X
            addr = CPU_AbsoluteX(cpu); CPU_NOP(cpu); cycles = 4; break; // +1 page cross

            // LAX (Load A & X)
        case 0xA7: addr = CPU_ZeroPage(cpu);        CPU_LAX(cpu, addr); cycles=3; break;
        case 0xB7: addr = CPU_ZeroPageY(cpu);       CPU_LAX(cpu, addr); cycles=4; break;
        case 0xAF: addr = CPU_Absolute(cpu);        CPU_LAX(cpu, addr); cycles=4; break;
        case 0xBF: addr = CPU_AbsoluteY(cpu);       CPU_LAX(cpu, addr); cycles=4; break; // +1 page cross
        case 0xA3: addr = CPU_IndexedIndirect(cpu); CPU_LAX(cpu, addr); cycles=6; break;
        case 0xB3: addr = CPU_IndirectIndexed(cpu); CPU_LAX(cpu, addr); cycles=5; break; // +1 page cross

            // SAX (Store A & X)
        case 0x87: addr = CPU_ZeroPage(cpu);        CPU_SAX(cpu, addr); cycles=3; break;
        case 0x97: addr = CPU_ZeroPageY(cpu);       CPU_SAX(cpu, addr); cycles=4; break;
        case 0x8F: addr = CPU_Absolute(cpu);        CPU_SAX(cpu, addr); cycles=4; break;
        case 0x83: addr = CPU_IndexedIndirect(cpu); CPU_SAX(cpu, addr); cycles=6; break;

            // SBC #imm (Unofficial) - Already covered by 0xEB

            // DCP (DEC then CMP)
        case 0xC7: addr = CPU_ZeroPage(cpu);        CPU_DCP(cpu, addr); cycles=5; break;
        case 0xD7: addr = CPU_ZeroPageX(cpu);       CPU_DCP(cpu, addr); cycles=6; break;
        case 0xCF: addr = CPU_Absolute(cpu);        CPU_DCP(cpu, addr); cycles=6; break;
        case 0xDF: 
            addr = CPU_AbsoluteX(cpu); 
            // 7 cycles normally, 6 if page boundary crossed
            if (((addr - cpu->x) & 0xFF00) != (addr & 0xFF00)) {
            cycles = 6;
            } else {
            cycles = 7;
            }
            CPU_DCP(cpu, addr); 
            break;
        case 0xDB: 
            addr = CPU_AbsoluteY(cpu); 
            // 7 cycles normally, 6 if page boundary crossed
            if (((addr - cpu->y) & 0xFF00) != (addr & 0xFF00)) {
            cycles = 6;
            } else {
            cycles = 7;
            }
            CPU_DCP(cpu, addr); 
            break;
        case 0xC3: addr = CPU_IndexedIndirect(cpu); CPU_DCP(cpu, addr); cycles=8; break;
        case 0xD3: 
            addr = CPU_IndirectIndexed(cpu); 
            // Add cycle if page boundary is crossed
            {
            uint8_t zp_addr = BUS_Read(cpu->nes, cpu->pc - 1); // The zero page address used
            uint16_t base_addr = (uint16_t)BUS_Read(cpu->nes, zp_addr) | ((uint16_t)BUS_Read(cpu->nes, (zp_addr + 1) & 0xFF) << 8);
            uint16_t final_addr = base_addr + cpu->y;
            if ((base_addr & 0xFF00) != (final_addr & 0xFF00)) {
                cycles = 7;
            } else {
                cycles = 8;
            }
            }
            CPU_DCP(cpu, addr); 
            break;

            // ISC / ISB (INC then SBC)
        case 0xE7: addr = CPU_ZeroPage(cpu);        CPU_ISC(cpu, addr); cycles=5; break;
        case 0xF7: addr = CPU_ZeroPageX(cpu);       CPU_ISC(cpu, addr); cycles=6; break;
        case 0xEF: addr = CPU_Absolute(cpu);        CPU_ISC(cpu, addr); cycles=6; break;
        case 0xFF: 
            addr = CPU_AbsoluteX(cpu); 
            // 7 cycles normally, 6 if page boundary crossed
            if (((addr - cpu->x) & 0xFF00) != (addr & 0xFF00)) {
            cycles = 6;
            } else {
            cycles = 7;
            }
            CPU_ISC(cpu, addr); 
            break;
        case 0xFB: 
            addr = CPU_AbsoluteY(cpu); 
            // 7 cycles normally, 6 if page boundary crossed
            if (((addr - cpu->y) & 0xFF00) != (addr & 0xFF00)) {
            cycles = 6;
            } else {
            cycles = 7;
            }
            CPU_ISC(cpu, addr); 
            break;
        case 0xE3: addr = CPU_IndexedIndirect(cpu); CPU_ISC(cpu, addr); cycles=8; break;
        case 0xF3: 
            addr = CPU_IndirectIndexed(cpu); 
            // 8 cycles normally, 7 if page boundary crossed
            {
            uint8_t zp_addr = BUS_Read(cpu->nes, cpu->pc - 1); // The zero page address used
            uint16_t base_addr = (uint16_t)BUS_Read(cpu->nes, zp_addr) | ((uint16_t)BUS_Read(cpu->nes, (zp_addr + 1) & 0xFF) << 8);
            uint16_t final_addr = base_addr + cpu->y;
            if ((base_addr & 0xFF00) != (final_addr & 0xFF00)) {
                cycles = 7;
            } else {
                cycles = 8;
            }
            }
            CPU_ISC(cpu, addr); 
            break;
        
        // SLO (ASL then ORA)
        case 0x07: addr = CPU_ZeroPage(cpu);        CPU_SLO(cpu, addr); cycles=5; break;
        case 0x17: addr = CPU_ZeroPageX(cpu);       CPU_SLO(cpu, addr); cycles=6; break;
        case 0x0F: addr = CPU_Absolute(cpu);        CPU_SLO(cpu, addr); cycles=6; break;
        case 0x1F: 
            addr = CPU_AbsoluteX(cpu);
            if (((addr - cpu->x) & 0xFF00) != (addr & 0xFF00)) {
                cycles = 6;
            } else {
                cycles = 7;
            }
            CPU_SLO(cpu, addr); 
            break;
        case 0x1B: 
            addr = CPU_AbsoluteY(cpu);
            if (((addr - cpu->y) & 0xFF00) != (addr & 0xFF00)) {
                cycles = 6;
            } else {
                cycles = 7;
            }
            CPU_SLO(cpu, addr); 
            break;
        case 0x03: addr = CPU_IndexedIndirect(cpu); CPU_SLO(cpu, addr); cycles=8; break;
        case 0x13: 
            addr = CPU_IndirectIndexed(cpu);
            {
            uint8_t zp_addr = BUS_Read(cpu->nes, cpu->pc - 1); // The zero page address used
            uint16_t base_addr = (uint16_t)BUS_Read(cpu->nes, zp_addr) | ((uint16_t)BUS_Read(cpu->nes, (zp_addr + 1) & 0xFF) << 8);
            uint16_t final_addr = base_addr + cpu->y;
            if ((base_addr & 0xFF00) != (final_addr & 0xFF00)) {
                cycles = 7;
            } else {
                cycles = 8;
            }
            }
            CPU_SLO(cpu, addr); 
            break;

            // RLA (ROL then AND)
        case 0x27: addr = CPU_ZeroPage(cpu);        CPU_RLA(cpu, addr); cycles=5; break;
        case 0x37: addr = CPU_ZeroPageX(cpu);       CPU_RLA(cpu, addr); cycles=6; break;
        case 0x2F: addr = CPU_Absolute(cpu);        CPU_RLA(cpu, addr); cycles=6; break;
        case 0x3F: 
            addr = CPU_AbsoluteX(cpu); 
            // 7 cycles normally, 8 if page boundary crossed
            if (((addr - cpu->x) & 0xFF00) != (addr & 0xFF00)) {
            cycles = 6;
            } else {
            cycles = 7;
            }
            CPU_RLA(cpu, addr); 
            break;
        case 0x3B: 
            addr = CPU_AbsoluteY(cpu); 
            // 7 cycles normally, 8 if page boundary crossed
            if (((addr - cpu->y) & 0xFF00) != (addr & 0xFF00)) {
            cycles = 6;
            } else {
            cycles = 7;
            }
            CPU_RLA(cpu, addr); 
            break;
        case 0x23: addr = CPU_IndexedIndirect(cpu); CPU_RLA(cpu, addr); cycles=8; break;
        case 0x33: 
            addr = CPU_IndirectIndexed(cpu); 
            {
            uint8_t zp_addr = BUS_Read(cpu->nes, cpu->pc - 1); // The zero page address used
            uint16_t base_addr = (uint16_t)BUS_Read(cpu->nes, zp_addr) | ((uint16_t)BUS_Read(cpu->nes, (zp_addr + 1) & 0xFF) << 8);
            uint16_t final_addr = base_addr + cpu->y;
            if ((base_addr & 0xFF00) != (final_addr & 0xFF00)) {
                cycles = 7;
            } else {
                cycles = 8;
            }
            }
            CPU_RLA(cpu, addr); 
            break;

            // SRE (LSR then EOR)
        case 0x47: addr = CPU_ZeroPage(cpu);        CPU_SRE(cpu, addr); cycles=5; break;
        case 0x57: addr = CPU_ZeroPageX(cpu);       CPU_SRE(cpu, addr); cycles=6; break;
        case 0x4F: addr = CPU_Absolute(cpu);        CPU_SRE(cpu, addr); cycles=6; break;
        case 0x5F: 
            addr = CPU_AbsoluteX(cpu);
            // 7 cycles normally, 6 if page boundary crossed
            if (((addr - cpu->x) & 0xFF00) != (addr & 0xFF00)) {
            cycles = 6;
            } else {
            cycles = 7;
            }
            CPU_SRE(cpu, addr); 
            break;
        case 0x5B: 
            addr = CPU_AbsoluteY(cpu);
            // 7 cycles normally, 6 if page boundary crossed
            if (((addr - cpu->y) & 0xFF00) != (addr & 0xFF00)) {
            cycles = 6;
            } else {
            cycles = 7;
            }
            CPU_SRE(cpu, addr); 
            break;
        case 0x43: addr = CPU_IndexedIndirect(cpu); CPU_SRE(cpu, addr); cycles=8; break;
        case 0x53: 
            addr = CPU_IndirectIndexed(cpu); 
            {
            uint8_t zp_addr = BUS_Read(cpu->nes, cpu->pc - 1); // The zero page address used
            uint16_t base_addr = (uint16_t)BUS_Read(cpu->nes, zp_addr) | ((uint16_t)BUS_Read(cpu->nes, (zp_addr + 1) & 0xFF) << 8);
            uint16_t final_addr = base_addr + cpu->y;
            if ((base_addr & 0xFF00) != (final_addr & 0xFF00)) {
                cycles = 7;
            } else {
                cycles = 8;
            }
            }
            CPU_SRE(cpu, addr); 
            break;

            // RRA (ROR then ADC)
        case 0x67: addr = CPU_ZeroPage(cpu);        CPU_RRA(cpu, addr); cycles=5; break;
        case 0x77: addr = CPU_ZeroPageX(cpu);       CPU_RRA(cpu, addr); cycles=6; break;
        case 0x6F: addr = CPU_Absolute(cpu);        CPU_RRA(cpu, addr); cycles=6; break;
        case 0x7F: 
            addr = CPU_AbsoluteX(cpu);
            // 7 cycles normally, 6 if page boundary crossed
            if (((addr - cpu->x) & 0xFF00) != (addr & 0xFF00)) {
            cycles = 6;
            } else {
            cycles = 7;
            }
            CPU_RRA(cpu, addr); 
            break;
        case 0x7B: 
            addr = CPU_AbsoluteY(cpu);
            // 7 cycles normally, 6 if page boundary crossed
            if (((addr - cpu->y) & 0xFF00) != (addr & 0xFF00)) {
            cycles = 6;
            } else {
            cycles = 7;
            }
            CPU_RRA(cpu, addr); 
            break;
        case 0x63: addr = CPU_IndexedIndirect(cpu); CPU_RRA(cpu, addr); cycles=8; break;
        case 0x73: 
            addr = CPU_IndirectIndexed(cpu); 
            {
            uint8_t zp_addr = BUS_Read(cpu->nes, cpu->pc - 1); // The zero page address used
            uint16_t base_addr = (uint16_t)BUS_Read(cpu->nes, zp_addr) | ((uint16_t)BUS_Read(cpu->nes, (zp_addr + 1) & 0xFF) << 8);
            uint16_t final_addr = base_addr + cpu->y;
            if ((base_addr & 0xFF00) != (final_addr & 0xFF00)) {
                cycles = 7;
            } else {
                cycles = 8;
            }
            }
            CPU_RRA(cpu, addr); 
            break;

            // ANC (AND Immediate then N->C)
        case 0x0B: case 0x2B: addr = CPU_Immediate(cpu); CPU_ANC(cpu, addr); cycles = 2; break;

            // ALR / ASR (AND Immediate then LSR A)
        case 0x4B: addr = CPU_Immediate(cpu); CPU_ALR(cpu, addr); cycles = 2; break;

            // ARR (AND Immediate then ROR A, special flags)
        case 0x6B: addr = CPU_Immediate(cpu); CPU_ARR(cpu, addr); cycles = 2; break;

            // SBX / AXS ( (A&X) - Immediate -> X )
        case 0xCB: addr = CPU_Immediate(cpu); CPU_SBX(cpu, addr); cycles = 2; break;

            // SHY (Store Y & (Hi+1)) - Simplified to STY for stability
        case 0x9C: addr = CPU_AbsoluteX(cpu); CPU_SHY(cpu, addr); cycles = 5; break;

            // SHX (Store X & (Hi+1)) - Simplified to STX for stability
        case 0x9E: addr = CPU_AbsoluteY(cpu); CPU_SHX(cpu, addr); cycles = 5; break;

            // LAS (LDA/TSX variant: Mem & SP -> A, X, SP)
        case 0xBB: addr = CPU_AbsoluteY(cpu); CPU_LAS(cpu, addr); cycles = 4; break; //+1 page cross

            // TAS / SHS (SP = A & X, Mem = SP & (Hi+1)) - Simplified to SP = A & X
        case 0x9B: addr = CPU_AbsoluteY(cpu); CPU_TAS(cpu); cycles = 5; break;

        default:
            DEBUG_ERROR("Unimplemented or Unknown opcode 0x%02X at 0x%04X", opcode, initial_pc);
            return -1; // Indicate error/halt for truly unknown opcodes
    }

    // TODO: Add accurate cycle calculation logic here based on page crossings, branches taken, etc.
    cpu->total_cycles += cycles;
    return cycles;
}