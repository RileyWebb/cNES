#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <cimgui.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cNES/nes.h"
#include "frontend/frontend.h"
#include "frontend/frontend_internal.h"

void Frontend_RenderPatternTable_GPU(SDL_GPUDevice *device, NES *nes, int table_idx,
                                      SDL_GPUTexture *texture, SDL_GPUTransferBuffer *transfer_buffer,
                                      uint32_t *pixel_buffer_rgba32, const uint32_t *palette_to_use_rgba32)
{
    if (!device || !nes || !nes->ppu || !texture || !transfer_buffer || !pixel_buffer_rgba32 || !palette_to_use_rgba32)
        return;

    // PPU_GetPatternTableData provides raw tile data.
    // Each tile is 8x8 pixels. A pattern table is 16x16 tiles (128x128 pixels).
    // Each tile is 16 bytes: 8 bytes for plane 0, 8 bytes for plane 1.
    uint8_t pt_data[16 * 16 * 16]; // Max size for one pattern table (256 tiles * 16 bytes/tile = 4096 bytes)
    PPU_GetPatternTableData(nes->ppu, table_idx, pt_data);

    for (int tile_y = 0; tile_y < 16; ++tile_y)
    {
        for (int tile_x = 0; tile_x < 16; ++tile_x)
        {
            int tile_offset_in_pt_data = (tile_y * 16 + tile_x) * 16; // Offset to the current tile's data
            for (int y = 0; y < 8; ++y)
            { // Pixel row within a tile
                uint8_t plane0_byte = pt_data[tile_offset_in_pt_data + y];
                uint8_t plane1_byte = pt_data[tile_offset_in_pt_data + y + 8];
                for (int x = 0; x < 8; ++x)
                {                    // Pixel column within a tile
                    int bit = 7 - x; // NES pixels are drawn left-to-right from MSB
                    uint8_t color_idx_in_palette = ((plane1_byte >> bit) & 1) << 1 | ((plane0_byte >> bit) & 1);
                    uint32_t final_pixel_color_rgba = palette_to_use_rgba32[color_idx_in_palette];

                    // Calculate position in the 128x128 pixel_buffer_rgba32
                    int buffer_x = tile_x * 8 + x;
                    int buffer_y = tile_y * 8 + y;
                    pixel_buffer_rgba32[buffer_y * 128 + buffer_x] = final_pixel_color_rgba;
                }
            }
        }
    }

    // Upload to GPU Texture
    void *mapped_memory = SDL_MapGPUTransferBuffer(device, transfer_buffer, true);
    if (!mapped_memory)
    {
        DEBUG_ERROR("PPUViewer: Failed to map transfer buffer: %s", SDL_GetError());
        return;
    }

    memcpy(mapped_memory, pixel_buffer_rgba32, sizeof(uint32_t) * 128 * 128);
    SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

    SDL_GPUCommandBuffer *cmd_buffer = SDL_AcquireGPUCommandBuffer(device);
    if (cmd_buffer)
    {
        SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd_buffer);
        if (copy_pass)
        {
            SDL_GPUTextureTransferInfo source_transfer_info = {0};
            source_transfer_info.transfer_buffer = transfer_buffer;
            source_transfer_info.offset = 0;
            source_transfer_info.pixels_per_row = 128;
            source_transfer_info.rows_per_layer = 128;     // Number of rows

            SDL_GPUTextureRegion destination_region = {0};
            destination_region.texture = texture;
            destination_region.w = 128;
            destination_region.h = 128;
            destination_region.d = 1;

            SDL_UploadToGPUTexture(copy_pass, &source_transfer_info, &destination_region, true);
            SDL_EndGPUCopyPass(copy_pass);
        }
        else
        {
            DEBUG_ERROR("PPUViewer: Failed to begin GPU copy pass: %s", SDL_GetError());
        }
        SDL_SubmitGPUCommandBuffer(cmd_buffer);
    }
    else
    {
        DEBUG_ERROR("PPUViewer: Failed to acquire GPU command buffer: %s", SDL_GetError());
    }
}

void Frontend_PPUViewer(NES *nes)
{
    if (!frontend_showPpuViewer)
        return;
    if (!nes || !nes->ppu)
    { // If window was open but NES becomes null, hide it.
        if (frontend_showPpuViewer && (!nes || !nes->ppu))
            frontend_showPpuViewer = false;
        return;
    }

    if (!frontend_paused)
    {
        if (igBegin("PPU Viewer", &frontend_showPpuViewer, ImGuiWindowFlags_None))
        {
            igTextDisabled("Pause emulation to inspect PPU state.");
        }
        igEnd();
        return;
    }

    if (igBegin("PPU Viewer", &frontend_showPpuViewer, ImGuiWindowFlags_None))
    {
        // Ensure GPU resources for PPU Viewer are created
        if (!pt_sampler)
        { // Create sampler once
            SDL_GPUSamplerCreateInfo sampler_info = {0};
            sampler_info.min_filter = SDL_GPU_FILTER_NEAREST; // For pixel art
            sampler_info.mag_filter = SDL_GPU_FILTER_NEAREST;
            sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            pt_sampler = SDL_CreateGPUSampler(gpu_device, &sampler_info);
            if (!pt_sampler)
                DEBUG_ERROR("PPUViewer: Failed to create sampler: %s", SDL_GetError());
        }
        if (!pt_transfer_buffer)
        { // Create transfer buffer once (can be reused)
            SDL_GPUTransferBufferCreateInfo transfer_create_info = {0};
            transfer_create_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transfer_create_info.size = 128 * 128 * 4 * 8 * 8; // For one 128x128 RGBA texture
            pt_transfer_buffer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_create_info);
            if (!pt_transfer_buffer)
                DEBUG_ERROR("PPUViewer: Failed to create transfer buffer: %s", SDL_GetError());
        }
        if (!pt_texture0 && gpu_device)
        {
            SDL_GPUTextureCreateInfo tex_info = {0};
            tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            tex_info.width = 128;
            tex_info.height = 128;
            tex_info.layer_count_or_depth = 1;
            tex_info.num_levels = 1;
            tex_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
            tex_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            pt_texture0 = SDL_CreateGPUTexture(gpu_device, &tex_info);
            if (!pt_texture0)
                DEBUG_ERROR("PPUViewer: Failed to create pt_texture0: %s", SDL_GetError());
        }
        if (!pt_texture1 && gpu_device)
        {
            SDL_GPUTextureCreateInfo tex_info = {0};
            tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            tex_info.width = 128;
            tex_info.height = 128;
            tex_info.layer_count_or_depth = 1;
            tex_info.num_levels = 1;
            tex_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
            tex_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            pt_texture1 = SDL_CreateGPUTexture(gpu_device, &tex_info);
            if (!pt_texture1)
                DEBUG_ERROR("PPUViewer: Failed to create pt_texture1: %s", SDL_GetError());
        }

        static uint32_t pt_pixel_buffer_rgba32[128 * 128]; // CPU-side buffer for pixel data

        const char *palette_names[] = {
            "BG Pal 0", "BG Pal 1", "BG Pal 2", "BG Pal 3",
            "Sprite Pal 0", "Sprite Pal 1", "Sprite Pal 2", "Sprite Pal 3"};
        igSetNextItemWidth(150);
        igCombo_Str_arr("PT Palette", &frontend_ppuViewerSelectedPalette, palette_names, 8, 4);

        static uint32_t viewer_palette_rgba[4]; // RGBA for display
        const uint32_t *master_nes_palette_abgr = PPU_GetPalette(nes->ppu);

        // Determine the 4 colors for the selected sub-palette
        for (int i = 0; i < 4; ++i)
        {
            uint8_t palette_ram_idx; // Index into PPU's $3F00-$3F1F
            if (i == 0)
            {                           // Color 0 is universal background or transparent for sprites
                palette_ram_idx = 0x00; // Always use $3F00 for the first color in any displayed palette
            }
            else
            {
                // frontend_ppuViewerSelectedPalette: 0-3 for BG, 4-7 for Sprites
                // BG palettes: $3F00, $3F01, $3F02, $3F03 (for pal 0)
                //              $3F00, $3F05, $3F06, $3F07 (for pal 1)
                // Sprite palettes: $3F00 (or $3F10), $3F11, $3F12, $3F13 (for pal 0 / overall pal 4)
                //                  $3F00 (or $3F10), $3F15, $3F16, $3F17 (for pal 1 / overall pal 5)
                uint8_t base_offset = (frontend_ppuViewerSelectedPalette < 4) ? (frontend_ppuViewerSelectedPalette * 4) : // BG palettes start at 0x00, 0x04, 0x08, 0x0C
                                      (0x10 + (frontend_ppuViewerSelectedPalette - 4) * 4);                     // Sprite palettes start at 0x10, 0x14, 0x18, 0x1C
                palette_ram_idx = base_offset + i;
            }
            // Ensure mirroring for palette addresses (e.g. $3F04 -> $3F00, $3F10 -> $3F00 etc.)
            if (palette_ram_idx % 4 == 0)
                palette_ram_idx = 0x00; // All palette[0] colors mirror $3F00

            uint8_t master_palette_color_index = nes->ppu->palette[palette_ram_idx % 32]; // Read from PPU palette RAM
            uint32_t nes_col_abgr = master_nes_palette_abgr[master_palette_color_index % 64];

            // Convert ABGR (0xAABBGGRR) to RGBA (0xRRGGBBAA)
            viewer_palette_rgba[i] = ((nes_col_abgr & 0x000000FF) << 24) | // R to R0000000
                                     ((nes_col_abgr & 0x0000FF00) << 8) |  // G to 00GG0000
                                     ((nes_col_abgr & 0x00FF0000) >> 8) |  // B to 0000BB00
                                     0x000000FF;                           // A (force opaque FF)
        }

        if (pt_texture0 && pt_sampler && pt_transfer_buffer)
        {
            igText("Pattern Table 0 ($0000):");
            Frontend_RenderPatternTable_GPU(gpu_device, nes, 0, pt_texture0, pt_transfer_buffer, pt_pixel_buffer_rgba32, viewer_palette_rgba);
            SDL_GPUTextureSamplerBinding binding0 = {pt_texture0, pt_sampler};
            igImage((ImTextureID)(uintptr_t)&binding0, (ImVec2){128 * 2.0f, 128 * 2.0f}, (ImVec2){0, 0}, (ImVec2){1, 1});
        }

        igSameLine(0, 20);
        igBeginGroup();
        if (pt_texture1 && pt_sampler && pt_transfer_buffer)
        {
            igText("Pattern Table 1 ($1000):");
            Frontend_RenderPatternTable_GPU(gpu_device, nes, 1, pt_texture1, pt_transfer_buffer, pt_pixel_buffer_rgba32, viewer_palette_rgba);
            SDL_GPUTextureSamplerBinding binding1 = {pt_texture1, pt_sampler};
            igImage((ImTextureID)(uintptr_t)&binding1, (ImVec2){128 * 2.0f, 128 * 2.0f}, (ImVec2){0, 0}, (ImVec2){1, 1});
        }
        igEndGroup();

        igSeparator();
        if (igCollapsingHeader_TreeNodeFlags("PPU Registers", ImGuiTreeNodeFlags_DefaultOpen))
        {
            igText("CTRL (0x2000): 0x%02X", nes->ppu->ctrl);
            igText("MASK (0x2001): 0x%02X", nes->ppu->mask);
            igText("STATUS (0x2002): 0x%02X", nes->ppu->status);
            igText("OAMADDR (0x2003): 0x%02X", nes->ppu->oam_addr);
            igText("VRAM Addr (v): 0x%04X, Temp Addr (t): 0x%04X", nes->ppu->vram_addr, nes->ppu->temp_addr);
            igText("Fine X: %d, Write Latch (w): %d", nes->ppu->fine_x, nes->ppu->addr_latch);
            igText("Scanline: %d, Cycle: %d", nes->ppu->scanline, nes->ppu->cycle);
            igText("Frame Odd: %s", nes->ppu->frame_odd ? "true" : "false");
            igText("NMI Occurred: %s, NMI Output: %s", nes->ppu->nmi_occured ? "true" : "false", nes->ppu->nmi_output ? "true" : "false");
        }

        if (igCollapsingHeader_TreeNodeFlags("Palettes", ImGuiTreeNodeFlags_None))
        {
            igText("Current PPU Palette RAM ($3F00 - $3F1F):");
            for (int i = 0; i < 32; ++i)
            {
                uint8_t palette_idx_val = nes->ppu->palette[i];
                uint32_t nes_col_abgr = master_nes_palette_abgr[palette_idx_val % 64];
                ImVec4 im_col;                                           // RGBA for ImGui
                im_col.x = (float)((nes_col_abgr & 0x000000FF)) / 255.0f;       // R
                im_col.y = (float)((nes_col_abgr & 0x0000FF00) >> 8) / 255.0f;  // G
                im_col.z = (float)((nes_col_abgr & 0x00FF0000) >> 16) / 255.0f; // B
                im_col.w = 1.0f;

                if (i > 0 && i % 16 == 0)
                    igNewLine();
                if (i > 0 && i % 4 == 0 && i % 16 != 0)
                    igSameLine(0, 8);

                char pal_label[16];
                snprintf(pal_label, sizeof(pal_label), "$3F%02X", i);
                igColorButton(pal_label, im_col, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, (ImVec2){20, 20});
                if (igIsItemHovered(0))
                {
                    igBeginTooltip();
                    igText("$3F%02X: Master Idx 0x%02X", i, palette_idx_val);
                    igColorButton("##tooltipcol", im_col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel, (ImVec2){64, 64});
                    igEndTooltip();
                }
                if ((i + 1) % 4 != 0)
                    igSameLine(0, 2);
            }
        }

        if (igCollapsingHeader_TreeNodeFlags("OAM (Sprites)", ImGuiTreeNodeFlags_None))
        {
            const uint8_t *oam_data = PPU_GetOAM(nes->ppu);
            if (oam_data)
            {
                if (igBeginTable("OAMTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit, (ImVec2){0, 0}, 0))
                {
                    igTableSetupColumn("Sprite #", 0, 0, 0);
                    igTableSetupColumn("Y", 0, 0, 0);
                    igTableSetupColumn("Tile ID", 0, 0, 0);
                    igTableSetupColumn("Attr", 0, 0, 0); // REFACTOR-NOTE: Decode attributes (palette, priority, flip) for better display.
                    igTableSetupColumn("X", 0, 0, 0);
                    igTableHeadersRow();
                    for (int i = 0; i < 8; ++i)
                    { // Display first 8 sprites
                        igTableNextRow(0, 0);
                        igTableSetColumnIndex(0);
                        igText("%d", i);
                        igTableSetColumnIndex(1);
                        igText("0x%02X (%d)", oam_data[i * 4 + 0], oam_data[i * 4 + 0]);
                        igTableSetColumnIndex(2);
                        igText("0x%02X", oam_data[i * 4 + 1]);
                        igTableSetColumnIndex(3);
                        igText("0x%02X", oam_data[i * 4 + 2]);
                        igTableSetColumnIndex(4);
                        igText("0x%02X (%d)", oam_data[i * 4 + 3], oam_data[i * 4 + 3]);
                    }
                    igEndTable();
                }
            }
        }
        // REFACTOR-NOTE: Add Nametable viewer section here (complex, involves rendering tiles based on nametable, attribute table, and current PPU scroll).
    }
    igEnd();
}



