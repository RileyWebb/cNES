#include <stdlib.h>
#include <string.h>

#include "debug.h"

#include "cNES/bus.h"
#include "cNES/cpu.h" // For OAM DMA CPU stalls (if implemented, currently not in this file)
#include "cNES/nes.h"
#include "cNES/ppu.h"

static uint8_t bus_read_default(struct BUS* bus, uint16_t address) { (void)bus; (void)address; return (uint8_t)0; }
static void bus_write_default(struct BUS* bus, uint16_t address, uint8_t value) { (void)bus; (void)address; (void)value; }

uint8_t (*ppu_read_default)(struct BUS* bus, uint16_t address);
void    (*ppu_write_default)(struct BUS* bus, uint16_t address, uint8_t value);

BUS *BUS_Create(NES *nes)
{
	if (!nes) 
    {
		DEBUG_ERROR("Unable to create BUS: NES pointer is NULL");
		return NULL; // Error: NES pointer must not be NULL
	}

	BUS *bus = malloc(sizeof(BUS));
	if (!bus)
	{
		DEBUG_ERROR("Unable to create BUS: Failed to allocate memory for BUS");
		return NULL;
	}

	memset(bus, 0, sizeof(BUS)); // Initialize BUS structure to zero

	bus->nes = nes; // Set the NES pointer for inter-component communication

	bus->cpu_read = bus_read_default; // Default read function for cartridge
	bus->cpu_write = bus_write_default; // Default write function for cartridge

	return bus;
}

void BUS_Destroy(BUS *bus)
{
	if (!bus) {
		DEBUG_ERROR("BUS_Destroy: BUS pointer is NULL.");
		return; // Error: BUS pointer must not be NULL
	}

	// Free dynamically allocated memory regions
	free(bus->cpu_ram);
	free(bus->vram);
	free(bus->palette_ram);
	free(bus->prg_rom_data);
	free(bus->chr_mem_data);

	// Free the BUS structure itself
	free(bus);
}


inline uint8_t BUS_Read(NES *nes, uint16_t address)
{
	return nes->bus->cpu_read(nes->bus, address);
}

void BUS_Write(NES *nes, uint16_t address, uint8_t value)
{
	nes->bus->cpu_write(nes->bus, address, value);
}

uint16_t BUS_Read16(NES *nes, uint16_t address)
{
	// NES is little-endian, so low byte is at address, high byte at address + 1
	uint8_t lo = BUS_Read(nes, address);
	uint8_t hi = BUS_Read(nes, address + 1);
	return (uint16_t)lo | ((uint16_t)hi << 8);
}

// Reads 16 bits from the bus, but handles the 6502 page boundary bug for
// indirect JMP (only relevant for indirect addressing mode's pointer fetch, not
// general 16-bit reads)
uint16_t BUS_Read16_PageBug(NES *nes, uint16_t address)
{
	uint8_t lo = BUS_Read(nes, address);
	uint16_t hi_addr;
	if ((address & 0x00FF) ==
		0x00FF) { // If low byte of address is $FF (page boundary)
		hi_addr = address & 0xFF00; // High byte comes from start of same page
	}
	else {
		hi_addr = address + 1; // Normal case
	}
	uint8_t hi = BUS_Read(nes, hi_addr);
	return (uint16_t)lo | ((uint16_t)hi << 8);
}

void BUS_Write16(NES *nes, uint16_t address, uint16_t value)
{
	uint8_t lo = (uint8_t)(value & 0x00FF);
	uint8_t hi = (uint8_t)(value >> 8);
	BUS_Write(nes, address, lo);
	BUS_Write(nes, address + 1, hi);
}

// --- PPU Bus Mapping (for PPU's internal access to CHR and VRAM/Palette) ---

// PPU reads from CHR ROM/RAM
uint8_t BUS_PPU_ReadCHR(struct BUS *bus_ptr, uint16_t address)
{
	// CHR data is mapped at $0000-$1FFF in PPU address space.
	address &= 0x1FFF; // Ensure address is within 8KB range.
	// Actual CHR size can vary (e.g. 0 for CHR-RAM). Mappers handle banking for
	// larger CHR. This direct access assumes bus_ptr->chrRom points to the
	// currently mapped 8KB bank.
	return bus_ptr->chrRom[address]; // Assuming chrRom is at least 8KB
}

// PPU writes to CHR RAM
void BUS_PPU_WriteCHR(struct BUS *bus_ptr, uint16_t address, uint8_t value)
{
	address &= 0x1FFF;
	// Writes to CHR are only effective if it's CHR RAM.
	// bus_ptr->chrRomSize == 0 often indicates CHR RAM.
	if (bus_ptr->chrRomSize == 0) { // Heuristic for CHR RAM
		bus_ptr->chrRom[address] = value;
	}
	// If CHR ROM, writes are typically ignored by hardware.
}

// PPU reads from VRAM (nametables) and palette RAM
uint8_t BUS_PPU_Read(struct BUS *bus, uint16_t address)
{
	address &= 0x3FFF;
	if (address < 0x2000) {
		// Pattern tables (CHR ROM/RAM)
		return BUS_PPU_ReadCHR(bus, address);
	}
	else if (address < 0x3F00) {
		// Nametables and mirrors ($2000-$2FFF, mirrored to $3EFF)
		// 2KB internal VRAM, mirrored every 0x1000
		uint16_t vram_addr = (address - 0x2000) & 0x0FFF;
		return bus->vram[vram_addr];
	}
	else if (address < 0x4000) {
		// Palette RAM indexes ($3F00-$3FFF), mirrored every 32 bytes
		uint16_t pal_addr = (address - 0x3F00) & 0x1F;
		return bus->palette[pal_addr];
	}
	return 0;
}

// PPU writes to VRAM (nametables) and palette RAM
void BUS_PPU_Write(struct BUS *bus, uint16_t address, uint8_t value)
{
	address &= 0x3FFF;
	if (address < 0x2000) {
		// Pattern tables (CHR RAM only; ignore if CHR ROM)
		BUS_PPU_WriteCHR(bus, address, value);
	}
	else if (address < 0x3F00) {
		// Nametables and mirrors ($2000-$2FFF, mirrored to $3EFF)
		uint16_t vram_addr = (address - 0x2000) & 0x0FFF;
		bus->vram[vram_addr] = value;
	}
	else if (address < 0x4000) {
		// Palette RAM indexes ($3F00-$3FFF), mirrored every 32 bytes
		uint16_t pal_addr = (address - 0x3F00) & 0x1F;
		bus->palette[pal_addr] = value;
	}
}
