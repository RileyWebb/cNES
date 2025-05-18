#include "cNES/nes.h"
#include "cNES/bus.h"
#include "cNES/ppu.h"
#include "cNES/cpu.h" // For OAM DMA CPU stalls (if implemented, currently not in this file)

uint8_t BUS_Read(NES* nes, uint16_t address) {
    // Handle CPU memory map (0x0000 - 0xFFFF)
    if (address < 0x2000) { // Internal RAM
        return nes->bus->memory[address & 0x07FF]; // 2KB RAM, mirrored every 0x0800 bytes
    } else if (address >= 0x2000 && address < 0x4000) { // PPU Registers
        // PPU registers ($2000-$2007), mirrored every 8 bytes up to $3FFF
        return PPU_ReadRegister(nes->ppu, 0x2000 + (address & 0x0007));
    } else if (address == 0x4016) { // Controller 1 Read
        uint8_t result = nes->controller_shift[0] & 0x01;
        if (nes->controller_strobe) {
            result = nes->controllers[0] & 0x01; // Only bit 0 is returned if strobe is active
        } else {
            nes->controller_shift[0] >>= 1;
            // On real hardware, bits 1-4 might be open bus or return fixed values after shifting all 8 bits.
            // For simplicity, we allow it to shift to 0. Bit 0 is the important one.
            // Some emulators return 1 for bits after the 8 data bits have been shifted out for $4016.
        }
        // Bits 1-7 are typically open bus, returning a mix of values.
        // Returning just bit 0 is a common simplification.
        // For more accuracy, one might return (result | (BUS_Peek(nes, address) & 0xFE)) or similar.
        return result; // Only bit 0 is significant
    } else if (address == 0x4017) { // Controller 2 Read
        uint8_t result = nes->controller_shift[1] & 0x01;
        if (nes->controller_strobe) {
            result = nes->controllers[1] & 0x01;
        } else {
            nes->controller_shift[1] >>= 1;
        }
        // Similar to 0x4016, only bit 0 is significant.
        // Some emulators might return fixed values (e.g. from an expansion device) on other bits.
        return result;
    } else if (address >= 0x4000 && address < 0x4020) { // APU and I/O Registers
        // APU registers are mostly write-only or have specific read behavior (e.g., status reads)
        // Not fully implemented here, typically returns open bus or last written value.
        // $4015: APU Status Read
        // For now, returning 0 for unhandled IO reads in this range besides controllers.
        return 0; // Placeholder for APU/IO reads
    } else if (address >= 0x6000 && address < 0x8000) { // PRG RAM (WRAM)
        // PRG RAM (if present, typically battery-backed save RAM)
        // This example doesn't explicitly model a separate PRG RAM in nes->bus struct.
        // If mappers provide it, they would handle it.
        // For now, returning 0 as if no PRG RAM or it's not enabled.
        return 0; // Placeholder for PRG RAM
    } else if (address >= 0x8000) { // PRG ROM
        // $8000-$FFFF: PRG ROM
        // Mapper might alter this address. For NROM, it's direct.
        // Assuming nes->bus->prgRom is 32KB and correctly loaded/mirrored for 16KB ROMs.
        return nes->bus->prgRom[address & (nes->bus->prgRomSize * 0x4000 -1)]; // Mask to PRG ROM size
        // Simpler NROM: return nes->bus->prgRom[address - 0x8000]; (if prgRom is exactly 32KB for $8000-$FFFF)
        // A more robust solution would be:
        // if (nes->bus->prgRomSize == 1) { // 16KB ROM, mirrored
        //    return nes->bus->prgRom[(address - 0x8000) & 0x3FFF];
        // } else { // 32KB ROM (or more, handled by mapper)
        //    return nes->bus->prgRom[(address - 0x8000)]; // Assumes mapper handles larger ROMs
        // }
        // For now, using a common NROM-like access:
        return nes->bus->prgRom[(address - 0x8000) & 0x7FFF]; // Access within a 32KB window
                                                              // Actual mapping depends on mapper and ROM size.
                                                              // For this example, let's assume prgRom array is 32KB
                                                              // and loaded appropriately (16KB ROMs mirrored).
    }
    // Default for unmapped regions (e.g. 0x4020-0x5FFF) is often open bus.
    // Open bus behavior returns the last value read or a mix of things.
    // Returning 0 is a simplification.
    return 0;
}

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
    } else if (address >= 0x6000 && address < 0x8000) {
        return 0; // PRG RAM peek
    } else if (address >= 0x8000) {
         return nes->bus->prgRom[(address - 0x8000) & 0x7FFF]; // NROM-like peek
    }
    return 0;
}

void BUS_Write(NES* nes, uint16_t address, uint8_t value) {
    if (address < 0x2000) { // Internal RAM
        nes->bus->memory[address & 0x07FF] = value;
    } else if (address >= 0x2000 && address < 0x4000) { // PPU Registers
        PPU_WriteRegister(nes->ppu, 0x2000 + (address & 0x0007), value);
    } else if (address == 0x4014) { // OAM DMA
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
        // Handle APU register writes
        // Not fully implemented here
        // e.g. nes->apu->WriteRegister(address, value);
    } else if (address >= 0x6000 && address < 0x8000) { // PRG RAM (WRAM)
        // Write to PRG RAM if present and enabled by mapper
        // For now, writes are ignored as no explicit PRG RAM modelled here.
    } else if (address >= 0x8000) { // PRG ROM
        // Writes to PRG ROM are usually ignored, or handled by mapper for bank switching etc.
        // For NROM, ignored.
    }
    // Writes to unmapped regions are ignored.
}

uint16_t BUS_Read16(NES* nes, uint16_t address) {
    // NES is little-endian, so low byte is at the initial address, high byte at address + 1
    uint8_t lo = BUS_Read(nes, address);
    uint8_t hi = BUS_Read(nes, address + 1);
    return (uint16_t)lo | ((uint16_t)hi << 8);
}

// Reads 16 bits from the bus, but handles the 6502 page boundary bug for indirect JMP
// (only relevant for indirect addressing mode's pointer fetch, not general 16-bit reads)
uint16_t BUS_Read16_PageBug(NES* nes, uint16_t address) {
    uint8_t lo = BUS_Read(nes, address);
    uint16_t hi_addr;
    if ((address & 0x00FF) == 0x00FF) { // If low byte of address is $FF (page boundary)
        hi_addr = address & 0xFF00; // High byte comes from start of same page
    } else {
        hi_addr = address + 1; // Normal case
    }
    uint8_t hi = BUS_Read(nes, hi_addr);
    return (uint16_t)lo | ((uint16_t)hi << 8);
}


void BUS_Write16(NES* nes, uint16_t address, uint16_t value) {
    uint8_t lo = (uint8_t)(value & 0x00FF);
    uint8_t hi = (uint8_t)(value >> 8);
    BUS_Write(nes, address, lo);
    BUS_Write(nes, address + 1, hi);
}

// --- PPU Bus Mapping (for PPU's internal access to CHR and VRAM/Palette) ---

// PPU reads from CHR ROM/RAM
uint8_t BUS_PPU_ReadCHR(struct BUS* bus_ptr, uint16_t address) {
    // CHR data is mapped at $0000-$1FFF in PPU address space.
    address &= 0x1FFF; // Ensure address is within 8KB range.
    // Actual CHR size can vary (e.g. 0 for CHR-RAM). Mappers handle banking for larger CHR.
    // This direct access assumes bus_ptr->chrRom points to the currently mapped 8KB bank.
    return bus_ptr->chrRom[address]; // Assuming chrRom is at least 8KB
}

// PPU writes to CHR RAM
void BUS_PPU_WriteCHR(struct BUS* bus_ptr, uint16_t address, uint8_t value) {
    address &= 0x1FFF;
    // Writes to CHR are only effective if it's CHR RAM.
    // bus_ptr->chrRomSize == 0 often indicates CHR RAM.
    if (bus_ptr->chrRomSize == 0) { // Heuristic for CHR RAM
        bus_ptr->chrRom[address] = value;
    }
    // If CHR ROM, writes are typically ignored by hardware.
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
