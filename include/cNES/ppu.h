#ifndef PPU_H
#define PPU_H

#define PPU_VRAM_SIZE 0x800
#define PPU_PALETTE_SIZE 0x20
#define PPU_OAM_SIZE 0x100

typedef struct PPU {
    NES *nes;
    uint8_t vram[PPU_VRAM_SIZE];      // Nametable RAM (2KB)
    uint8_t palette[PPU_PALETTE_SIZE];// Palette RAM (32 bytes)
    uint8_t oam[PPU_OAM_SIZE];        // OAM (256 bytes)
    uint8_t oam_addr;                 // OAMADDR register

    // Registers
    uint8_t ctrl, mask, status;
    uint8_t scroll_latch, addr_latch;
    uint8_t scroll_x, scroll_y;
    uint16_t vram_addr, temp_addr;
    uint8_t fine_x;
    uint8_t data_buffer;

    // Rendering state
    int scanline, cycle;
    int frame_odd;
    int nmi_occured, nmi_output, nmi_previous, nmi_interrupt;

    // Frame buffer (ARGB32)
    uint32_t framebuffer[256 * 240];
} PPU;

PPU *PPU_Create(NES *nes);
void PPU_Step(PPU *ppu);
void PPU_Reset(PPU *ppu);
void PPU_Destroy(PPU *ppu);
uint8_t PPU_ReadRegister(PPU *ppu, uint16_t addr);
void PPU_WriteRegister(PPU *ppu, uint16_t addr, uint8_t value);
uint8_t PPU_CHR_Read(PPU *ppu, uint16_t addr);
void PPU_CHR_Write(PPU *ppu, uint16_t addr, uint8_t value);
void PPU_TriggerNMI(PPU *ppu);
void PPU_SetNMIOutput(PPU *ppu, int enable);

#endif // PPU_H