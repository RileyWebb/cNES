#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "cNES/nes.h"
#include "cNES/cpu.h"
#include "cNES/bus.h"
#include "cNES/ppu.h"

#include "cNES/debugging.h"
#include "cNES/cpu.h" // for cpu_opcodes

void format_operand(char *buffer, size_t buffer_size, const char *fmt, ...) {
    if (buffer == NULL || buffer_size == 0) return;
    va_list args;
    va_start(args, fmt);
    int result = vsnprintf(buffer, buffer_size, fmt, args);
    va_end(args);
    if (result < 0) { buffer[0] = '\0'; }
    // No warning for truncation here, as it's less critical for disassembly logging
}

// Disassemble one instruction at 'address'
uint16_t disassemble(NES *nes, uint16_t address, char *buffer, size_t buffer_size) 
{
    uint8_t opcode = BUS_Read(nes, address);
    uint16_t next_addr = address;
    char mnemonic[4] = "???";
    char operand_str[32] = "";

    // Special handling for JMP/JSR
    if (opcode == 0x4C) { // JMP Absolute
        strcpy(mnemonic, "JMP");
        uint16_t target = BUS_Read16(nes, address + 1);
        format_operand(operand_str, sizeof(operand_str), "$%04X", target);
        next_addr = target;
    } else if (opcode == 0x6C) { // JMP Indirect
        strcpy(mnemonic, "JMP");
        uint16_t ptr = BUS_Read16(nes, address + 1);
        // Emulate 6502 JMP indirect bug: if low byte is 0xFF, high byte wraps within the page
        uint16_t indirect_addr;
        if ((ptr & 0x00FF) == 0x00FF) {
            uint8_t low = BUS_Read(nes, ptr);
            uint8_t high = BUS_Read(nes, ptr & 0xFF00);
            indirect_addr = (high << 8) | low;
        } else {
            indirect_addr = BUS_Read16(nes, ptr);
        }
        format_operand(operand_str, sizeof(operand_str), "($%04X)", ptr);
        next_addr = indirect_addr;
    } else if (opcode == 0x20) { // JSR Absolute
        strcpy(mnemonic, "JSR");
        uint16_t target = BUS_Read16(nes, address + 1);
        format_operand(operand_str, sizeof(operand_str), "$%04X", target);
        next_addr = target;
    } else {
        // Use cpu_opcodes table for everything else
        const CPU_Opcode *op = &cpu_opcodes[opcode];
        strncpy(mnemonic, op->mnemonic, sizeof(mnemonic) - 1);
        mnemonic[sizeof(mnemonic) - 1] = '\0';

        switch (op->addressing_mode) {
            case CPU_MODE_IMMEDIATE:
                format_operand(operand_str, sizeof(operand_str), "#$%02X", BUS_Read(nes, address + 1));
                next_addr = address + 2;
                break;
            case CPU_MODE_ZERO_PAGE:
                format_operand(operand_str, sizeof(operand_str), "$%02X", BUS_Read(nes, address + 1));
                next_addr = address + 2;
                break;
            case CPU_MODE_ZERO_PAGE_X:
                format_operand(operand_str, sizeof(operand_str), "$%02X,X", BUS_Read(nes, address + 1));
                next_addr = address + 2;
                break;
            case CPU_MODE_ZERO_PAGE_Y:
                format_operand(operand_str, sizeof(operand_str), "$%02X,Y", BUS_Read(nes, address + 1));
                next_addr = address + 2;
                break;
            case CPU_MODE_ABSOLUTE:
                format_operand(operand_str, sizeof(operand_str), "$%04X", BUS_Read16(nes, address + 1));
                next_addr = address + 3;
                break;
            case CPU_MODE_ABSOLUTE_X:
                format_operand(operand_str, sizeof(operand_str), "$%04X,X", BUS_Read16(nes, address + 1));
                next_addr = address + 3;
                break;
            case CPU_MODE_ABSOLUTE_Y:
                format_operand(operand_str, sizeof(operand_str), "$%04X,Y", BUS_Read16(nes, address + 1));
                next_addr = address + 3;
                break;
            case CPU_MODE_INDIRECT:
                format_operand(operand_str, sizeof(operand_str), "($%04X)", BUS_Read16(nes, address + 1));
                next_addr = address + 3;
                break;
            case CPU_MODE_INDEXED_INDIRECT:
                format_operand(operand_str, sizeof(operand_str), "($%02X,X)", BUS_Read(nes, address + 1));
                next_addr = address + 2;
                break;
            case CPU_MODE_INDIRECT_INDEXED:
                format_operand(operand_str, sizeof(operand_str), "($%02X),Y", BUS_Read(nes, address + 1));
                next_addr = address + 2;
                break;
            case CPU_MODE_RELATIVE: {
                int8_t offset = (int8_t)BUS_Read(nes, address + 1);
                format_operand(operand_str, sizeof(operand_str), "$%04X", address + 2 + offset);
                next_addr = address + 2;
                break;
            }
            case CPU_MODE_ACCUMULATOR:
                strcpy(operand_str, "A");
                next_addr = address + 1;
                break;
            case CPU_MODE_IMPLIED:
                operand_str[0] = '\0';
                next_addr = address + 1;
                break;
            default:
                operand_str[0] = '\0';
                next_addr = address + 1;
                break;
        }
    }

    if (operand_str[0] != '\0') {
        snprintf(buffer, buffer_size, "%s %s", mnemonic, operand_str);
    } else {
        snprintf(buffer, buffer_size, "%s", mnemonic);
    }
    
    return next_addr;
}