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
#include "frontend/cimgui_hex.h"

void Frontend_CpuWindow(NES *nes)
{
    if (!frontend_showCpuWindow) return;
    if (!frontend_paused) {
        if (igBegin("CPU Registers", &frontend_showCpuWindow, ImGuiWindowFlags_None)) {
            igTextDisabled("Pause emulation to inspect CPU state.");
        }
        igEnd();
        return;
    }
    if (!nes || !nes->cpu) {
        if (frontend_showCpuWindow && (!nes || !nes->cpu)) frontend_showCpuWindow = false;
        return;
    }
    if (igBegin("CPU Registers", &frontend_showCpuWindow, ImGuiWindowFlags_None)) {
        igText("A:  0x%02X (%3d)", nes->cpu->a, nes->cpu->a); // %3d for consistent spacing
        igText("X:  0x%02X (%3d)", nes->cpu->x, nes->cpu->x);
        igText("Y:  0x%02X (%3d)", nes->cpu->y, nes->cpu->y);
        igText("SP: 0x01%02X", nes->cpu->sp);
        igText("PC: 0x%04X", nes->cpu->pc);

        igText("Status: 0x%02X [", nes->cpu->status);
        igSameLine(0, 0);
        const char *flag_names = "NV-BDIZC"; // Bit 5 is often shown as '-', though it has a value in the register
        for (int i = 7; i >= 0; i--) {
            bool is_set = (nes->cpu->status >> i) & 1;
            // Bit 5 ('-') is conventionally shown as set if its bit in the status byte is 1.
            // The B flag (bit 4) has two meanings depending on context (interrupt vs PHP/BRK).
            // Here we just show the raw status register bits.

            if (is_set) {
                igTextColored((ImVec4){0.3f, 1.0f, 0.3f, 1.0f}, "%c", flag_names[7 - i]); // Green for set
            } else {
                igTextColored((ImVec4){1.0f, 0.4f, 0.4f, 1.0f}, "%c", flag_names[7 - i]); // Red for clear
            }
            if (i > 0) igSameLine(0, 2);
        }
        igSameLine(0, 0);
        igText("]");
        igNewLine();
        igText("Total Cycles: %llu", (unsigned long long)nes->cpu->total_cycles);
        // REFACTOR-NOTE: Add display for pending interrupts (NMI, IRQ lines state from bus/CPU).
        // REFACTOR-NOTE: Add instruction timing/cycle count for current/last instruction (requires more detailed CPU state).
    }
    igEnd();
}

static ImGuiHexEditorState hex_state;
static bool                hex_state_initialized = false;

void Frontend_MemoryViewer(NES *nes)
{
    if (!frontend_showMemoryViewer) return;
    if (!nes) {
        frontend_showMemoryViewer = false;
        return;
    }

    if (igBegin("Memory Viewer", &frontend_showMemoryViewer, ImGuiWindowFlags_None)) {
        static const char *memory_regions[] = {"CPU Bus", "PPU OAM", "PPU Palette", "PPU VRAM", "PRG ROM", "CHR ROM"};
        static int         current_region   = 0;
        static size_t      region_count     = sizeof(memory_regions) / sizeof(memory_regions[0]);

        size_t memory_size  = sizeof(nes->bus->memory);
        size_t oam_size     = sizeof(nes->ppu->oam);
        size_t palette_size = sizeof(nes->ppu->palette);
        size_t vram_size    = sizeof(nes->ppu->vram);

        void   *region_ptr   = {nes->bus->memory, nes->ppu->oam,        nes->ppu->palette,
                                nes->ppu->vram,   nes->bus->prgRomData, nes->bus->chrRomData}; // Default to CPU bus
        size_t *region_sizes = {
            &memory_size, &oam_size, &palette_size, &vram_size, &nes->bus->prgRomDataSize, &nes->bus->chrRomDataSize};

        if (igCombo_Str_arr("##region_selector", &current_region, memory_regions, region_count, 0) ||
            !hex_state_initialized) {
            // Initialize the struct with the default values
            ImGuiHexEditorState_Init(&hex_state);

            // Setup data pointers
            hex_state.Bytes    = &region_ptr[current_region];
            hex_state.MaxBytes = (int)region_sizes[current_region];

            // Optional: Customize editor behavior
            hex_state.ShowAscii       = false;
            hex_state.EnableClipboard = true;
            hex_state.ShowAddress     = true;
            hex_state.Separators      = 4;     // Add a visual gap every 4 bytes
            hex_state.AddressChars    = 4;     // Show 4 hex digits for addresses (up to 64KB)
            hex_state.LowercaseBytes  = false; // Use A-F instead of a-f
            hex_state.ReadOnly        = false; // Allow editing
            hex_state_initialized     = true;
        }

        static int highlight_addr = 0;
        
        static int step = 1;
        static int quick_steps = 16;
        if (igInputScalar("##find_addr", ImGuiDataType_S32, &highlight_addr, &step, &quick_steps, "%04X", 0)) {
            hex_state.HighlightRanges.Capacity = 1;
            hex_state.HighlightRanges.Size = 1;
            hex_state.HighlightRanges.Data = malloc(sizeof(ImGuiHexEditorHighlightRange));
            hex_state.HighlightRanges.Data[0] = 
            (ImGuiHexEditorHighlightRange){
                .From = highlight_addr,
                .To = highlight_addr,
                .Color = 0xFFFF0000, // Red highlight
                .BorderColor = 0xFF000000, // Black border
                .Flags = ImGuiHexEditorHighlightFlags_FullSized | ImGuiHexEditorHighlightFlags_Ascii
            };
        }

        ImVec2 available_size;
        igGetContentRegionAvail(&available_size);

        // 3. Render the hex editor child widget
        if (igBeginHexEditor("##hex_view", &hex_state, available_size, ImGuiChildFlags_Borders,
                             ImGuiWindowFlags_None)) {
            igEndHexEditor();
        }
    }
    igEnd();
}

void Frontend_DrawDisassembler(NES *nes)
{
    if (!frontend_showDisassembler) return;
    if (!nes || !nes->cpu) {
        if (frontend_showDisassembler && (!nes || !nes->cpu)) frontend_showDisassembler = false;
        return;
    }

    if (igBegin("Disassembler", &frontend_showDisassembler, ImGuiWindowFlags_None)) {
        // REFACTOR-NOTE: Add breakpoint setting, step-over/step-out controls, and syntax highlighting for a richer debugger.
        uint16_t pc_to_disassemble = nes->cpu->pc;
        igText("Current PC: 0x%04X", pc_to_disassemble);
        igSameLine(0, 20);
        if (igButton("Step Op (F7)", (ImVec2){0, 0})) { // REFACTOR-NOTE: F7 mapping added here for local control
            if (frontend_paused)
                Frontend_RequestStepCpu();
            else
                DEBUG_INFO("Cannot step instruction while running. Pause first (F6).");
        }

        igBeginChild_Str("DisassemblyRegion", (ImVec2){0, igGetTextLineHeightWithSpacing() * 18},
                         ImGuiChildFlags_Borders,
                         ImGuiWindowFlags_None); // Removed ImGuiWindowFlags_HorizontalScrollbar as table handles it
        if (igBeginTable("DisassembledView", 3,
                         ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
                         (ImVec2){0, 0}, 0)) {
            igTableSetupColumn(" ", ImGuiTableColumnFlags_WidthFixed, igGetFontSize() * 1.5f, 0);
            igTableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, igGetFontSize() * 5.0f, 0);
            igTableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch, 0, 0);

            char     disasm_buf[128];
            uint16_t addr_iter = nes->cpu->pc;

            // Attempt to show a few lines before PC (very simplified, true back-disassembly is complex)
            // This naive approach just starts a bit earlier and hopes instructions align.
            uint16_t start_addr = nes->cpu->pc;
            for (int pre_lines = 0; pre_lines < 8; ++pre_lines) {
                if (start_addr < 5) {
                    start_addr = 0;
                    break;
                } // Avoid underflow by too much
                start_addr -= 3; // Guess average instruction length
            }
            if (start_addr > nes->cpu->pc) start_addr = nes->cpu->pc; // Safety if PC is very low

            addr_iter = start_addr;

            for (int i = 0; i < 32; i++) { // Show more lines
                igTableNextRow(0, 0);

                igTableSetColumnIndex(0);
                if (addr_iter == nes->cpu->pc) {
                    igText(">");
                } else {
                    igText(" ");
                }

                igTableSetColumnIndex(1);
                igText("0x%04X", addr_iter);

                igTableSetColumnIndex(2);
                uint16_t prev_addr_iter = addr_iter;
                //addr_iter = disassemble(nes, addr_iter, disasm_buf, sizeof(disasm_buf)); // disassemble should return next instruction's address
                igTextUnformatted(disasm_buf, NULL);

                if (addr_iter <= prev_addr_iter &&
                    i < 31) { // Prevent infinite loop if disassembly stalls, but allow last line
                    igTableNextRow(0, 0);
                    igTableSetColumnIndex(1);
                    igText("----");
                    igTableSetColumnIndex(2);
                    igText("Disassembly error or end of known code.");
                    break;
                }
                if (addr_iter == 0 && i < 31) break; // Stop if disassemble returns 0 (e.g. invalid opcode sequence)
            }
            // Auto-scroll to PC if it's not visible
            // This is a bit tricky with tables, might need manual scroll management or ImGuiListClipper
            igEndTable();
        }
        igEndChild();
    }
    igEnd();
}
