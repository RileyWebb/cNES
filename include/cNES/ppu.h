#ifndef PPU_H
#define PPU_H

typedef struct PPU {
    uint8_t vram[0x4000];   // Pattern tables, name tables, palettes
    uint8_t oam[256];       // Sprite RAM
    uint8_t registers[8];   // PPU registers
    uint16_t scanline;
    uint16_t cycle;
    uint8_t frame_buffer[256 * 240]; // Output pixels
    // ...other internal state...
} PPU;

#endif // PPU_H