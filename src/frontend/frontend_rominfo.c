#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS

#include <SDL3/SDL.h>
#include <cimgui.h>
#include <stdio.h>
#include <string.h>

#include "cNES/nes.h"
#include "cNES/rom.h"
#include "frontend/frontend.h"
#include "frontend/frontend_internal.h"

static const char *Frontend_RomInfo_FormatName(const ROM *rom)
{
    if (!rom) {
        return "No ROM";
    }

    if (rom->size >= sizeof(rom->header)) {
        if ((rom->header[7] & 0x0C) == 0x08) {
            return "NES 2.0";
        }
        if (memcmp(rom->header, "NES\x1A", 4) == 0) {
            return "iNES";
        }
    }

    return "Unknown";
}

static const char *Frontend_RomInfo_MirroringName(const ROM *rom)
{
    if (!rom || rom->size < sizeof(rom->header)) {
        return "Unknown";
    }

    if (rom->header[6] & 0x08) {
        return "Four-screen";
    }
    return (rom->header[6] & 0x01) ? "Vertical" : "Horizontal";
}

static void Frontend_RomInfo_Row(const char *label, const char *value)
{
    igTableNextRow(0, 0);
    igTableSetColumnIndex(0);
    igTextUnformatted(label, NULL);
    igTableSetColumnIndex(1);
    igTextUnformatted(value, NULL);
}

void Frontend_ROMInfoWindow(NES *nes)
{
    if (!frontend_showRomInfoWindow) {
        return;
    }

    if (igBegin("ROM Info", &frontend_showRomInfoWindow, ImGuiWindowFlags_None))
    {
        const ROM *rom = (nes != NULL) ? nes->rom : NULL;
        if (!rom)
        {
            igTextDisabled("No ROM loaded.");
            igEnd();
            return;
        }

        igText("%s", rom->name ? rom->name : "Untitled ROM");
        igTextDisabled("Loaded from %s", rom->path ? rom->path : "unknown path");
        igSeparator();

        if (igBeginTable("ROMInfoTable", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, (ImVec2){0, 0}, 0))
        {
            igTableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 0.0f, 0);
            igTableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
            igTableHeadersRow();

            char value_buffer[128];
            snprintf(value_buffer, sizeof(value_buffer), "%s", Frontend_RomInfo_FormatName(rom));
            Frontend_RomInfo_Row("Format", value_buffer);

            snprintf(value_buffer, sizeof(value_buffer), "0x%02X (%s)", (unsigned)rom->mapper_id, NES_Mapper_Get(rom->mapper_id).name ? NES_Mapper_Get(rom->mapper_id).name : "Unknown Mapper");
            Frontend_RomInfo_Row("Mapper", value_buffer);

            snprintf(value_buffer, sizeof(value_buffer), "%zu bytes (%.1f KiB)", rom->prg_rom_size, (double)rom->prg_rom_size / 1024.0);
            Frontend_RomInfo_Row("PRG ROM", value_buffer);

            snprintf(value_buffer, sizeof(value_buffer), "%zu bytes (%.1f KiB)", rom->chr_rom_size, (double)rom->chr_rom_size / 1024.0);
            Frontend_RomInfo_Row("CHR ROM", value_buffer);

            snprintf(value_buffer, sizeof(value_buffer), "%s", Frontend_RomInfo_MirroringName(rom));
            Frontend_RomInfo_Row("Mirroring", value_buffer);

            snprintf(value_buffer, sizeof(value_buffer), "%s", (rom->header[6] & 0x02) ? "Yes" : "No");
            Frontend_RomInfo_Row("Battery", value_buffer);

            snprintf(value_buffer, sizeof(value_buffer), "%s", (rom->header[6] & 0x04) ? "Yes" : "No");
            Frontend_RomInfo_Row("Trainer", value_buffer);

            snprintf(value_buffer, sizeof(value_buffer), "%s", (rom->header[7] & 0x02) ? "Yes" : "No");
            Frontend_RomInfo_Row("PlayChoice-10", value_buffer);

            snprintf(value_buffer, sizeof(value_buffer), "%s", (rom->header[7] & 0x01) ? "VS System" : "No");
            Frontend_RomInfo_Row("VS System", value_buffer);

            igEndTable();
        }

        igSeparator();
        igText("Header Bytes");
        igTextDisabled("Bytes 0x00-0x0F from the file header.");
        for (int row = 0; row < 2; ++row)
        {
            char line[64];
            size_t offset = (size_t)row * 8;
            snprintf(line, sizeof(line), "%02zu: %02X %02X %02X %02X %02X %02X %02X %02X",
                     offset,
                     rom->header[offset + 0], rom->header[offset + 1], rom->header[offset + 2], rom->header[offset + 3],
                     rom->header[offset + 4], rom->header[offset + 5], rom->header[offset + 6], rom->header[offset + 7]);
            igTextUnformatted(line, NULL);
        }
    }
    igEnd();
}
