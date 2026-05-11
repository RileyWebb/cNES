#include "cNES/nes.h"
#include "cNES/bus.h"
#include "cNES/apu.h"
#include "cNES/mapper.h"
#include "cNES/ppu.h"
#include "cNES/cpu.h" // For OAM DMA CPU stalls (if implemented, currently not in this file)

// OPTIMIZATION: These are now inlined in bus.h (see bus.h for inline implementations).
// This implementation is only kept as a reference and is not compiled.

// BUS_Peek is for debuggers/tools that need to read memory without side effects.
uint8_t BUS_Peek(NES* nes, uint16_t address) {
    if (address < 0x2000) {
        return nes->bus->memory[address & 0x07FF];
    } else if (address >= 0x2000 && address < 0x4000) {
        // For PPU registers, peeking should ideally not trigger side effects like PPUSTATUS VBlank clear.
        // PPU_PeekRegister should be implemented in ppu.c if needed.
        // For now, forwarding to PPU_ReadRegister, which *does* have side effects for PPUSTATUS.
        // A true peek would need PPU to have its own peek functions.
        // Corrected to use the same PPU register mapping as BUS_Read.
        return PPU_ReadRegister(nes->ppu, 0x2000 + (address & 0x0007));
    } else if (address == 0x4016) {
        int controller_idx = 0;
        // Return current state of shift register bit 0 without shifting or reloading from strobe
        return nes->controller_shift[controller_idx] & 0x01;
    } else if (address == 0x4017) {
        int controller_idx = 1;
        return nes->controller_shift[controller_idx] & 0x01;
    } else if (address >= 0x4000 && address < 0x4020) {
        // APU/IO, return 0 for peek
        return 0;
    } else if (address >= 0x6000) {
        NES_MapperInfo mapper = NES_Mapper_Get(nes->bus->mapper);
        return mapper.cpu_read(nes->bus, address);
    }
    return 0;
}

void BUS_Write(NES* nes, uint16_t address, uint8_t value) {
    if (address < 0x2000) { // Internal RAM
        nes->bus->memory[address & 0x07FF] = value;
    } else if (address >= 0x2000 && address < 0x4000) { // PPU Registers
        PPU_WriteRegister(nes->ppu, 0x2000 + (address & 0x0007), value);
    } else if (address == 0x4014) { // OAM DMA
        // Drive the PPU open bus with the value being written
        PPU_DriveOpenBus(nes->ppu, value);
        
        uint16_t dma_page_addr = (uint16_t)value << 8;
        uint8_t oam_start_addr = nes->ppu->oam_addr; // OAMADDR might not be 0 before DMA
        
        // DMA takes ~513-514 CPU cycles. CPU is halted.
        // For emulation, this can be an instant copy.
        // CPU_HaltDMA(); // Pseudo-function for cycle accounting if needed
        for (uint16_t i = 0; i < 256; ++i) {
            // OAM DMA reads from CPU bus, so use BUS_Read
            uint8_t byte_to_write = BUS_Read(nes, dma_page_addr + i);
            nes->ppu->oam[(oam_start_addr + i) & 0xFF] = byte_to_write;
        }
        // CPU_ResumeDMA();
        // OAM_ADDR is not changed by DMA hardware. Sprites are written starting at current OAM_ADDR, wrapping around.
    } else if (address == 0x4016) { // Controller Strobe
        nes->controller_strobe = value & 0x01;
        if (nes->controller_strobe == 0) { // When strobe transitions from 1 to 0 (or is set to 0)
            // Capture current state of controllers into shift registers
            nes->controller_shift[0] = nes->controllers[0];
            nes->controller_shift[1] = nes->controllers[1];
        }
    } else if (address >= 0x4000 && address < 0x4020) { // APU and I/O Registers
        if (nes->apu) {
            APU_WriteRegister(nes->apu, address, value);
        }
    } else if (address >= 0x6000) {
        NES_MapperInfo mapper = NES_Mapper_Get(nes->bus->mapper);
        mapper.cpu_write(nes->bus, address, value);
    }
    // Writes to unmapped regions are ignored.
}

// OPTIMIZATION: BUS_Read16 is now inlined in bus.h (see bus.h for inline implementation).
// This implementation is only kept as a reference and is not compiled.

// NOTE: BUS_Read16_PageBug is commented out - not used in current implementation
// Reads 16 bits from the bus, but handles the 6502 page boundary bug for indirect JMP
// (only relevant for indirect addressing mode's pointer fetch, not general 16-bit reads)
// uint16_t BUS_Read16_PageBug(NES* nes, uint16_t address) {
//     uint8_t lo = BUS_Read(nes, address);
//     uint16_t hi_addr;
//     if ((address & 0x00FF) == 0x00FF) { // If low byte of address is $FF (page boundary)
//         hi_addr = address & 0xFF00; // High byte comes from start of same page
//     } else {
//         hi_addr = address + 1; // Normal case
//     }
//     uint8_t hi = BUS_Read(nes, hi_addr);
//     return (uint16_t)lo | ((uint16_t)hi << 8);
// }


void BUS_Write16(NES* nes, uint16_t address, uint16_t value) {
    uint8_t lo = (uint8_t)(value & 0x00FF);
    uint8_t hi = (uint8_t)(value >> 8);
    BUS_Write(nes, address, lo);
    BUS_Write(nes, address + 1, hi);
}

// --- PPU Bus Mapping (for PPU's internal access to CHR and VRAM/Palette) ---

// PPU reads from CHR ROM/RAM
uint8_t BUS_PPU_ReadCHR(struct BUS* bus_ptr, uint16_t address) {
    NES_MapperInfo mapper = NES_Mapper_Get(bus_ptr->mapper);
    return mapper.ppu_read(bus_ptr, address);
}

// PPU writes to CHR RAM
void BUS_PPU_WriteCHR(struct BUS* bus_ptr, uint16_t address, uint8_t value) {
    NES_MapperInfo mapper = NES_Mapper_Get(bus_ptr->mapper);
    mapper.ppu_write(bus_ptr, address, value);
}

// PPU reads from VRAM (nametables) and palette RAM
uint8_t BUS_PPU_Read(struct BUS* bus, uint16_t address)
{
    address &= 0x3FFF;
    if (address < 0x2000) {
        // Pattern tables (CHR ROM/RAM)
        return BUS_PPU_ReadCHR(bus, address);
    } else if (address < 0x3F00) {
        // Nametables and mirrors ($2000-$2FFF, mirrored to $3EFF)
        // 2KB internal VRAM, mirrored every 0x1000
        uint16_t vram_addr = (address - 0x2000) & 0x0FFF;
        return bus->vram[vram_addr];
    } else if (address < 0x4000) {
        // Palette RAM indexes ($3F00-$3FFF), mirrored every 32 bytes
        uint16_t pal_addr = (address - 0x3F00) & 0x1F;
        return bus->palette[pal_addr];
    }
    return 0;
}

// PPU writes to VRAM (nametables) and palette RAM
void BUS_PPU_Write(struct BUS* bus, uint16_t address, uint8_t value)
{
    address &= 0x3FFF;
    if (address < 0x2000) {
        // Pattern tables (CHR RAM only; ignore if CHR ROM)
        BUS_PPU_WriteCHR(bus, address, value);
    } else if (address < 0x3F00) {
        // Nametables and mirrors ($2000-$2FFF, mirrored to $3EFF)
        uint16_t vram_addr = (address - 0x2000) & 0x0FFF;
        bus->vram[vram_addr] = value;
    } else if (address < 0x4000) {
        // Palette RAM indexes ($3F00-$3FFF), mirrored every 32 bytes
        uint16_t pal_addr = (address - 0x3F00) & 0x1F;
        bus->palette[pal_addr] = value;
    }
}
