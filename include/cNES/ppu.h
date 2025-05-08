#ifndef PPU_H
#define PPU_H

typedef struct PPU {
    uint8_t vram[0x4000];   // Pattern tables, name tables, palettes
    uint8_t oam[256];       // Sprite RAM

    // PPU registers
    uint8_t ctrl;
    uint8_t mask;
    uint8_t status;
    uint8_t scroll;
    uint8_t oam_addr;
    uint8_t oam_data;
    uint8_t addr;
    uint8_t data;

    uint16_t scanline;
    uint16_t cycle;
    uint8_t frame_buffer[256 * 240]; // Output pixels
    // ...other internal state...
} PPU;

#endif // PPU_H