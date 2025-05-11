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
int disassemble(NES *nes, uint16_t address, char *buffer, size_t buffer_size) 
{
    uint8_t opcode = BUS_Read(nes, address); // Use mem_read for consistency
    uint16_t next_addr = address;
    char mnemonic[5] = "???";
    char operand_str[20] = "";

#define FORMAT_OPERAND(fmt, ...) format_operand(operand_str, sizeof(operand_str), fmt, __VA_ARGS__)

    switch (opcode) {
        // --- Official Opcodes ---
        case 0xA9: strcpy(mnemonic, "LDA"); FORMAT_OPERAND("#$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xA5: strcpy(mnemonic, "LDA"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xB5: strcpy(mnemonic, "LDA"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xAD: strcpy(mnemonic, "LDA"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xBD: strcpy(mnemonic, "LDA"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xB9: strcpy(mnemonic, "LDA"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xA1: strcpy(mnemonic, "LDA"); FORMAT_OPERAND("($%02X,X)", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xB1: strcpy(mnemonic, "LDA"); FORMAT_OPERAND("($%02X),Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xA2: strcpy(mnemonic, "LDX"); FORMAT_OPERAND("#$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xA6: strcpy(mnemonic, "LDX"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xB6: strcpy(mnemonic, "LDX"); FORMAT_OPERAND("$%02X,Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xAE: strcpy(mnemonic, "LDX"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xBE: strcpy(mnemonic, "LDX"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xA0: strcpy(mnemonic, "LDY"); FORMAT_OPERAND("#$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xA4: strcpy(mnemonic, "LDY"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xB4: strcpy(mnemonic, "LDY"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xAC: strcpy(mnemonic, "LDY"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xBC: strcpy(mnemonic, "LDY"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x85: strcpy(mnemonic, "STA"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x95: strcpy(mnemonic, "STA"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x8D: strcpy(mnemonic, "STA"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x9D: strcpy(mnemonic, "STA"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x99: strcpy(mnemonic, "STA"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x81: strcpy(mnemonic, "STA"); FORMAT_OPERAND("($%02X,X)", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x91: strcpy(mnemonic, "STA"); FORMAT_OPERAND("($%02X),Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x86: strcpy(mnemonic, "STX"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x96: strcpy(mnemonic, "STX"); FORMAT_OPERAND("$%02X,Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x8E: strcpy(mnemonic, "STX"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x84: strcpy(mnemonic, "STY"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x94: strcpy(mnemonic, "STY"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x8C: strcpy(mnemonic, "STY"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xAA: strcpy(mnemonic, "TAX"); next_addr += 1; break;
        case 0xA8: strcpy(mnemonic, "TAY"); next_addr += 1; break;
        case 0x8A: strcpy(mnemonic, "TXA"); next_addr += 1; break;
        case 0x98: strcpy(mnemonic, "TYA"); next_addr += 1; break;
        case 0xBA: strcpy(mnemonic, "TSX"); next_addr += 1; break;
        case 0x9A: strcpy(mnemonic, "TXS"); next_addr += 1; break;
        case 0x48: strcpy(mnemonic, "PHA"); next_addr += 1; break;
        case 0x68: strcpy(mnemonic, "PLA"); next_addr += 1; break;
        case 0x08: strcpy(mnemonic, "PHP"); next_addr += 1; break;
        case 0x28: strcpy(mnemonic, "PLP"); next_addr += 1; break;
        case 0xC6: strcpy(mnemonic, "DEC"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xD6: strcpy(mnemonic, "DEC"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xCE: strcpy(mnemonic, "DEC"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xDE: strcpy(mnemonic, "DEC"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xCA: strcpy(mnemonic, "DEX"); next_addr += 1; break;
        case 0x88: strcpy(mnemonic, "DEY"); next_addr += 1; break;
        case 0xE6: strcpy(mnemonic, "INC"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xF6: strcpy(mnemonic, "INC"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xEE: strcpy(mnemonic, "INC"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xFE: strcpy(mnemonic, "INC"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xE8: strcpy(mnemonic, "INX"); next_addr += 1; break;
        case 0xC8: strcpy(mnemonic, "INY"); next_addr += 1; break;
        case 0x69: strcpy(mnemonic, "ADC"); FORMAT_OPERAND("#$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x65: strcpy(mnemonic, "ADC"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x75: strcpy(mnemonic, "ADC"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x6D: strcpy(mnemonic, "ADC"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x7D: strcpy(mnemonic, "ADC"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x79: strcpy(mnemonic, "ADC"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x61: strcpy(mnemonic, "ADC"); FORMAT_OPERAND("($%02X,X)", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x71: strcpy(mnemonic, "ADC"); FORMAT_OPERAND("($%02X),Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xE9: strcpy(mnemonic, "SBC"); FORMAT_OPERAND("#$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xE5: strcpy(mnemonic, "SBC"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xF5: strcpy(mnemonic, "SBC"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xED: strcpy(mnemonic, "SBC"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xFD: strcpy(mnemonic, "SBC"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xF9: strcpy(mnemonic, "SBC"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xE1: strcpy(mnemonic, "SBC"); FORMAT_OPERAND("($%02X,X)", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xF1: strcpy(mnemonic, "SBC"); FORMAT_OPERAND("($%02X),Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xC9: strcpy(mnemonic, "CMP"); FORMAT_OPERAND("#$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xC5: strcpy(mnemonic, "CMP"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xD5: strcpy(mnemonic, "CMP"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xCD: strcpy(mnemonic, "CMP"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xDD: strcpy(mnemonic, "CMP"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xD9: strcpy(mnemonic, "CMP"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xC1: strcpy(mnemonic, "CMP"); FORMAT_OPERAND("($%02X,X)", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xD1: strcpy(mnemonic, "CMP"); FORMAT_OPERAND("($%02X),Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xE0: strcpy(mnemonic, "CPX"); FORMAT_OPERAND("#$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xE4: strcpy(mnemonic, "CPX"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xEC: strcpy(mnemonic, "CPX"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xC0: strcpy(mnemonic, "CPY"); FORMAT_OPERAND("#$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xC4: strcpy(mnemonic, "CPY"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xCC: strcpy(mnemonic, "CPY"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x29: strcpy(mnemonic, "AND"); FORMAT_OPERAND("#$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x25: strcpy(mnemonic, "AND"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x35: strcpy(mnemonic, "AND"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x2D: strcpy(mnemonic, "AND"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x3D: strcpy(mnemonic, "AND"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x39: strcpy(mnemonic, "AND"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x21: strcpy(mnemonic, "AND"); FORMAT_OPERAND("($%02X,X)", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x31: strcpy(mnemonic, "AND"); FORMAT_OPERAND("($%02X),Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x49: strcpy(mnemonic, "EOR"); FORMAT_OPERAND("#$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x45: strcpy(mnemonic, "EOR"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x55: strcpy(mnemonic, "EOR"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x4D: strcpy(mnemonic, "EOR"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x5D: strcpy(mnemonic, "EOR"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x59: strcpy(mnemonic, "EOR"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x41: strcpy(mnemonic, "EOR"); FORMAT_OPERAND("($%02X,X)", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x51: strcpy(mnemonic, "EOR"); FORMAT_OPERAND("($%02X),Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x09: strcpy(mnemonic, "ORA"); FORMAT_OPERAND("#$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x05: strcpy(mnemonic, "ORA"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x15: strcpy(mnemonic, "ORA"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x0D: strcpy(mnemonic, "ORA"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x1D: strcpy(mnemonic, "ORA"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x19: strcpy(mnemonic, "ORA"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x01: strcpy(mnemonic, "ORA"); FORMAT_OPERAND("($%02X,X)", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x11: strcpy(mnemonic, "ORA"); FORMAT_OPERAND("($%02X),Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x24: strcpy(mnemonic, "BIT"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x2C: strcpy(mnemonic, "BIT"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x0A: strcpy(mnemonic, "ASL"); strcpy(operand_str, "A"); next_addr += 1; break;
        case 0x06: strcpy(mnemonic, "ASL"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x16: strcpy(mnemonic, "ASL"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x0E: strcpy(mnemonic, "ASL"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x1E: strcpy(mnemonic, "ASL"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x4A: strcpy(mnemonic, "LSR"); strcpy(operand_str, "A"); next_addr += 1; break;
        case 0x46: strcpy(mnemonic, "LSR"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x56: strcpy(mnemonic, "LSR"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x4E: strcpy(mnemonic, "LSR"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x5E: strcpy(mnemonic, "LSR"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x2A: strcpy(mnemonic, "ROL"); strcpy(operand_str, "A"); next_addr += 1; break;
        case 0x26: strcpy(mnemonic, "ROL"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x36: strcpy(mnemonic, "ROL"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x2E: strcpy(mnemonic, "ROL"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x3E: strcpy(mnemonic, "ROL"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x6A: strcpy(mnemonic, "ROR"); strcpy(operand_str, "A"); next_addr += 1; break;
        case 0x66: strcpy(mnemonic, "ROR"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x76: strcpy(mnemonic, "ROR"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x6E: strcpy(mnemonic, "ROR"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x7E: strcpy(mnemonic, "ROR"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        //case 0x4C: strcpy(mnemonic, "JMP"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        //case 0x6C: strcpy(mnemonic, "JMP"); FORMAT_OPERAND("($%04X)", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        //case 0x20: strcpy(mnemonic, "JSR"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x60: strcpy(mnemonic, "RTS"); next_addr += 1; break;
        case 0x40: strcpy(mnemonic, "RTI"); next_addr += 1; break;
        case 0x90: { strcpy(mnemonic, "BCC"); int8_t o=(int8_t)BUS_Read(nes, address+1); FORMAT_OPERAND("$%04X", address+2+o); next_addr += 2; break; }
        case 0xB0: { strcpy(mnemonic, "BCS"); int8_t o=(int8_t)BUS_Read(nes, address+1); FORMAT_OPERAND("$%04X", address+2+o); next_addr += 2; break; }
        case 0xF0: { strcpy(mnemonic, "BEQ"); int8_t o=(int8_t)BUS_Read(nes, address+1); FORMAT_OPERAND("$%04X", address+2+o); next_addr += 2; break; }
        case 0x30: { strcpy(mnemonic, "BMI"); int8_t o=(int8_t)BUS_Read(nes, address+1); FORMAT_OPERAND("$%04X", address+2+o); next_addr += 2; break; }
        case 0xD0: { strcpy(mnemonic, "BNE"); int8_t o=(int8_t)BUS_Read(nes, address+1); FORMAT_OPERAND("$%04X", address+2+o); next_addr += 2; break; }
        case 0x10: { strcpy(mnemonic, "BPL"); int8_t o=(int8_t)BUS_Read(nes, address+1); FORMAT_OPERAND("$%04X", address+2+o); next_addr += 2; break; }
        case 0x50: { strcpy(mnemonic, "BVC"); int8_t o=(int8_t)BUS_Read(nes, address+1); FORMAT_OPERAND("$%04X", address+2+o); next_addr += 2; break; }
        case 0x70: { strcpy(mnemonic, "BVS"); int8_t o=(int8_t)BUS_Read(nes, address+1); FORMAT_OPERAND("$%04X", address+2+o); next_addr += 2; break; }
        case 0x18: strcpy(mnemonic, "CLC"); next_addr += 1; break;
        case 0xD8: strcpy(mnemonic, "CLD"); next_addr += 1; break;
        case 0x58: strcpy(mnemonic, "CLI"); next_addr += 1; break;
        case 0xB8: strcpy(mnemonic, "CLV"); next_addr += 1; break;
        case 0x38: strcpy(mnemonic, "SEC"); next_addr += 1; break;
        case 0xF8: strcpy(mnemonic, "SED"); next_addr += 1; break;
        case 0x78: strcpy(mnemonic, "SEI"); next_addr += 1; break;
        case 0x00: strcpy(mnemonic, "BRK"); next_addr += 1; break;
        case 0xEA: strcpy(mnemonic, "NOP"); next_addr += 1; break;

            // --- Unofficial Opcodes (Disassembly Mnemonics) ---
        case 0x02: case 0x12: case 0x22: case 0x32: case 0x42: case 0x52: case 0x62:
        case 0x72: case 0x92: case 0xB2: case 0xD2: case 0xF2: strcpy(mnemonic, "*KIL"); next_addr += 1; break;
        case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA: strcpy(mnemonic, "*NOP"); next_addr += 1; break;
        case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2: strcpy(mnemonic, "*NOP"); FORMAT_OPERAND("#$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x04: case 0x44: case 0x64: strcpy(mnemonic, "*NOP"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4: strcpy(mnemonic, "*NOP"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x0C: strcpy(mnemonic, "*NOP"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC: strcpy(mnemonic, "*NOP"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xA7: strcpy(mnemonic, "*LAX"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xB7: strcpy(mnemonic, "*LAX"); FORMAT_OPERAND("$%02X,Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xAF: strcpy(mnemonic, "*LAX"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xBF: strcpy(mnemonic, "*LAX"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xA3: strcpy(mnemonic, "*LAX"); FORMAT_OPERAND("($%02X,X)", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xB3: strcpy(mnemonic, "*LAX"); FORMAT_OPERAND("($%02X),Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x87: strcpy(mnemonic, "*SAX"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x97: strcpy(mnemonic, "*SAX"); FORMAT_OPERAND("$%02X,Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x8F: strcpy(mnemonic, "*SAX"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x83: strcpy(mnemonic, "*SAX"); FORMAT_OPERAND("($%02X,X)", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xEB: strcpy(mnemonic, "*SBC"); FORMAT_OPERAND("#$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xC7: strcpy(mnemonic, "*DCP"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xD7: strcpy(mnemonic, "*DCP"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xCF: strcpy(mnemonic, "*DCP"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xDF: strcpy(mnemonic, "*DCP"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xDB: strcpy(mnemonic, "*DCP"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xC3: strcpy(mnemonic, "*DCP"); FORMAT_OPERAND("($%02X,X)", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xD3: strcpy(mnemonic, "*DCP"); FORMAT_OPERAND("($%02X),Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xE7: strcpy(mnemonic, "*ISC"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xF7: strcpy(mnemonic, "*ISC"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xEF: strcpy(mnemonic, "*ISC"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xFF: strcpy(mnemonic, "*ISC"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xFB: strcpy(mnemonic, "*ISC"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0xE3: strcpy(mnemonic, "*ISC"); FORMAT_OPERAND("($%02X,X)", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xF3: strcpy(mnemonic, "*ISC"); FORMAT_OPERAND("($%02X),Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x07: strcpy(mnemonic, "*SLO"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x17: strcpy(mnemonic, "*SLO"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x0F: strcpy(mnemonic, "*SLO"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x1F: strcpy(mnemonic, "*SLO"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x1B: strcpy(mnemonic, "*SLO"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x03: strcpy(mnemonic, "*SLO"); FORMAT_OPERAND("($%02X,X)", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x13: strcpy(mnemonic, "*SLO"); FORMAT_OPERAND("($%02X),Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x27: strcpy(mnemonic, "*RLA"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x37: strcpy(mnemonic, "*RLA"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x2F: strcpy(mnemonic, "*RLA"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x3F: strcpy(mnemonic, "*RLA"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x3B: strcpy(mnemonic, "*RLA"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x23: strcpy(mnemonic, "*RLA"); FORMAT_OPERAND("($%02X,X)", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x33: strcpy(mnemonic, "*RLA"); FORMAT_OPERAND("($%02X),Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x47: strcpy(mnemonic, "*SRE"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x57: strcpy(mnemonic, "*SRE"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x4F: strcpy(mnemonic, "*SRE"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x5F: strcpy(mnemonic, "*SRE"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x5B: strcpy(mnemonic, "*SRE"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x43: strcpy(mnemonic, "*SRE"); FORMAT_OPERAND("($%02X,X)", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x53: strcpy(mnemonic, "*SRE"); FORMAT_OPERAND("($%02X),Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x67: strcpy(mnemonic, "*RRA"); FORMAT_OPERAND("$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x77: strcpy(mnemonic, "*RRA"); FORMAT_OPERAND("$%02X,X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x6F: strcpy(mnemonic, "*RRA"); FORMAT_OPERAND("$%04X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x7F: strcpy(mnemonic, "*RRA"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x7B: strcpy(mnemonic, "*RRA"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x63: strcpy(mnemonic, "*RRA"); FORMAT_OPERAND("($%02X,X)", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x73: strcpy(mnemonic, "*RRA"); FORMAT_OPERAND("($%02X),Y", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x0B: case 0x2B: strcpy(mnemonic, "*ANC"); FORMAT_OPERAND("#$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x4B: strcpy(mnemonic, "*ALR"); FORMAT_OPERAND("#$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x6B: strcpy(mnemonic, "*ARR"); FORMAT_OPERAND("#$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0xCB: strcpy(mnemonic, "*SBX"); FORMAT_OPERAND("#$%02X", BUS_Read(nes, address + 1)); next_addr += 2; break;
        case 0x9C: strcpy(mnemonic, "*SHY"); FORMAT_OPERAND("$%04X,X", BUS_Read16(nes, address + 1)); next_addr += 3; break; // Addr uses Abs,X
        case 0x9E: strcpy(mnemonic, "*SHX"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break; // Addr uses Abs,Y
        case 0xBB: strcpy(mnemonic, "*LAS"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x9B: strcpy(mnemonic, "*TAS"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break;
        case 0x93: strcpy(mnemonic, "*SHA"); FORMAT_OPERAND("($%02X),Y", BUS_Read(nes, address + 1)); next_addr += 2; break; // Often unstable, like STA
        case 0x9F: strcpy(mnemonic, "*SHA"); FORMAT_OPERAND("$%04X,Y", BUS_Read16(nes, address + 1)); next_addr += 3; break; // Often unstable, like STA

        case 0x4C: // JMP Absolute
            strcpy(mnemonic, "JMP");
            {
                uint16_t target = BUS_Read16(nes, address + 1);
                FORMAT_OPERAND("$%04X", target);
                next_addr = target;
            }
            break;
        case 0x6C: // JMP Indirect
            strcpy(mnemonic, "JMP");
            {
                uint16_t ptr = BUS_Read16(nes, address + 1);
                uint16_t target;
                // 6502 bug: If the indirect vector falls on a page boundary, the LSB wraps around
                if ((ptr & 0x00FF) == 0x00FF) {
                    uint8_t low = BUS_Read(nes, ptr);
                    uint8_t high = BUS_Read(nes, ptr & 0xFF00);
                    target = (high << 8) | low;
                } else {
                    target = BUS_Read16(nes, ptr);
                }
                FORMAT_OPERAND("($%04X)", ptr);
                next_addr = target;
            }
            break;
        case 0x20: // JSR Absolute
            strcpy(mnemonic, "JSR");
            {
                uint16_t target = BUS_Read16(nes, address + 1);
                FORMAT_OPERAND("$%04X", target);
                next_addr = target;
            }
            break;

        default:
            break;
    }

    if (operand_str[0] != '\0') {
        snprintf(buffer, buffer_size, "%s %s", mnemonic, operand_str);
    } else {
        snprintf(buffer, buffer_size, "%s", mnemonic);
    }
#undef FORMAT_OPERAND
    return next_addr;
}