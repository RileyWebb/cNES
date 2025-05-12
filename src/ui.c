#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <SDL2/SDL.h>
#include <cimgui.h>
#include <cimgui_impl.h>
#include <cimplot.h>
#include <GL/glew.h>
#include <SDL2/SDL_opengl.h>

#include "debug.h"
#include "cNES/nes.h"
#include "cNES/cpu.h"
#include "cNES/ppu.h"
#include "cNES/debugging.h"

#include "ui.h"

SDL_Window *window;
SDL_GLContext glContext;

ImGuiIO* ioptr;

void UI_InitStlye()
{
    igStyleColorsDark(NULL);
    ImGuiStyle* style = igGetStyle();
    ImVec4* colors = style->Colors;

    colors[ImGuiCol_Text] = (ImVec4){1.00f, 1.00f, 1.00f, 1.00f};
    colors[ImGuiCol_TextDisabled] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_WindowBg] = (ImVec4){0.14f, 0.14f, 0.14f, 1.00f};
    colors[ImGuiCol_ChildBg] = (ImVec4){0.14f, 0.14f, 0.14f, 1.00f};
    colors[ImGuiCol_PopupBg] = (ImVec4){0.14f, 0.14f, 0.14f, 1.00f};
    colors[ImGuiCol_Border] = (ImVec4){0.43f, 0.43f, 0.50f, 0.50f};
    colors[ImGuiCol_BorderShadow] = (ImVec4){0.00f, 0.00f, 0.00f, 0.00f};
    colors[ImGuiCol_FrameBg] = (ImVec4){0.20f, 0.20f, 0.20f, 1.00f};
    colors[ImGuiCol_FrameBgHovered] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_FrameBgActive] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_TitleBg] = (ImVec4){0.14f, 0.14f, 0.14f, 1.00f};
    colors[ImGuiCol_TitleBgActive] = (ImVec4){0.14f, 0.14f, 0.14f, 1.00f};
    colors[ImGuiCol_TitleBgCollapsed] = (ImVec4){0.14f, 0.14f, 0.14f, 1.00f};
    colors[ImGuiCol_MenuBarBg] = (ImVec4){0.20f, 0.20f, 0.20f, 1.00f};
    colors[ImGuiCol_ScrollbarBg] = (ImVec4){0.14f, 0.14f, 0.14f, 1.00f};
    colors[ImGuiCol_ScrollbarGrab] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_ScrollbarGrabHovered] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_ScrollbarGrabActive] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_CheckMark] = (ImVec4){0.86f, 0.93f, 0.89f, 1.00f};
    colors[ImGuiCol_SliderGrab] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_SliderGrabActive] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_Button] = (ImVec4){0.20f, 0.20f, 0.20f, 1.00f};
    colors[ImGuiCol_ButtonHovered] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_ButtonActive] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_Header] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_HeaderHovered] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_HeaderActive] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_Separator] = (ImVec4){0.43f, 0.43f, 0.50f, 0.50f};
    colors[ImGuiCol_SeparatorHovered] = (ImVec4){0.43f, 0.43f, 0.50f, 0.50f};
    colors[ImGuiCol_SeparatorActive] = (ImVec4){0.43f, 0.43f, 0.50f, 0.50f};
    colors[ImGuiCol_ResizeGrip] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_ResizeGripHovered] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_ResizeGripActive] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_Tab] = (ImVec4){0.14f, 0.14f, 0.14f, 1.00f};
    colors[ImGuiCol_TabHovered] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    //colors[ImGuiCol_TabActive] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    //colors[ImGuiCol_TabUnfocused] = (ImVec4){0.14f, 0.14f, 0.14f, 1.00f};
    //colors[ImGuiCol_TabUnfocusedActive] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_PlotLines] = (ImVec4){0.86f, 0.93f, 0.89f, 1.00f};
    colors[ImGuiCol_PlotLinesHovered] = (ImVec4){0.86f, 0.93f, 0.89f, 1.00f};
    colors[ImGuiCol_PlotHistogram] = (ImVec4){0.86f, 0.93f, 0.89f, 1.00f};
    colors[ImGuiCol_PlotHistogramHovered] = (ImVec4){0.86f, 0.93f, 0.89f, 1.00f};
    colors[ImGuiCol_TextSelectedBg] = (ImVec4){0.24f, 0.24f, 0.24f, 1.00f};
    colors[ImGuiCol_DragDropTarget] = (ImVec4){0.86f, 0.93f, 0.89f, 1.00f};
    //colors[ImGuiCol_NavHighlight] = (ImVec4){0.86f, 0.93f, 0.89f, 1.00f};
    colors[ImGuiCol_NavWindowingHighlight] = (ImVec4){0.86f, 0.93f, 0.89f, 1.00f};
    colors[ImGuiCol_NavWindowingDimBg] = (ImVec4){0.14f, 0.14f, 0.14f, 1.00f};
    colors[ImGuiCol_ModalWindowDimBg] = (ImVec4){0.14f, 0.14f, 0.14f, 1.00f};

    style->WindowRounding = 0.0f;
    style->FrameRounding = 0.0f;
    style->GrabRounding = 0.0f;
    style->ScrollbarRounding = 0.0f;
    style->TabRounding = 0.0f;

    //DELETE IF BAD
    /*
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.ItemSpacing.x = 8.0f;
    style.ItemSpacing.y = 4.0f;
    style.WindowPadding.x = 8.0f;
    style.WindowPadding.y = 4.0f;
    style.FramePadding.x = 8.0f;
    style.FramePadding.y = 4.0f;
    */
}

void UI_Init()
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
        return;
    }

    window = SDL_CreateWindow("cEMU", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        fprintf(stderr, "Could not create window: %s\n", SDL_GetError());
        return;
    }

    glContext = SDL_GL_CreateContext(window);
    if (glContext == NULL)
    {
        fprintf(stderr, "Could not create OpenGL context: %s\n", SDL_GetError());
        return;
    }

    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK && err != GLEW_ERROR_NO_GLX_DISPLAY)
    {
        fprintf(stderr, "Could not initialize GLEW: %s\n", glewGetErrorString(err));
        return;
    }

    ioptr = igGetIO_ContextPtr(igCreateContext(NULL));
    ioptr->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    ioptr->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls
    ioptr->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport / Platform Windows
    ioptr->ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 130");

    //SDL_GL_SetSwapInterval(1); // Enable vsync

    UI_InitStlye();
}

void UI_Draw(NES* nes)
{
    igSetNextWindowDockID(igGetID_Str("MyDockSpace"), ImGuiCond_Always);

    igBegin("cEMU", NULL, ImGuiWindowFlags_DockNodeHost | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar);
    igBeginMenuBar();
    igSetCursorPosY(0); // Move cursor to top
    igText("cEMU");
    igEndMenuBar();

    static GLuint ppu_texture = 0;
    if (ppu_texture == 0) {
        glGenTextures(1, &ppu_texture);
        glBindTexture(GL_TEXTURE_2D, ppu_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 240, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }

    // Update texture with PPU framebuffer data
    glBindTexture(GL_TEXTURE_2D, ppu_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 240, GL_RGBA, GL_UNSIGNED_BYTE, nes->ppu->framebuffer);

    // Draw the texture using ImGui
    igImage((ImTextureID)(intptr_t)ppu_texture, (ImVec2){256, 240}, (ImVec2){0, 0}, (ImVec2){1, 1});
    // Draw the CPU registers
    igTextColored((ImVec4){1.0f, 0.0f, 0.0f, 1.0f}, "%s", "CPU Registers");
    igText("A: 0x%02X", nes->cpu->a);
    igText("X: 0x%02X", nes->cpu->x);
    igText("Y: 0x%02X", nes->cpu->y);
    igText("SP: 0x%02X", nes->cpu->sp);
    igText("PC: 0x%04X", nes->cpu->pc);
    igText("Status: 0x%02X", nes->cpu->status);
    igText("Total Cycles: %llu", nes->cpu->total_cycles);
    igText("PPU Scanline: %d", nes->ppu->scanline);
    igText("PPU Cycle: %d", nes->ppu->cycle);
    igText("PPU Frame Odd: %d", nes->ppu->frame_odd);
    igText("PPU NMI Occured: %d", nes->ppu->nmi_occured);
    igText("PPU NMI Output: %d", nes->ppu->nmi_output);
    igText("PPU NMI Previous: %d", nes->ppu->nmi_previous);
    igText("PPU NMI Interrupt: %d", nes->ppu->nmi_interrupt);
    igText("PPU Framebuffer: %p", nes->ppu->framebuffer);
    igText("PPU VRAM: %p", nes->ppu->vram);
    igText("PPU Palette: %p", nes->ppu->palette);
    igText("PPU OAM: %p", nes->ppu->oam);
    igText("PPU OAM Address: %d", nes->ppu->oam_addr);
    igText("PPU Control: %d", nes->ppu->ctrl);
    igText("PPU Mask: %d", nes->ppu->mask);
    igText("PPU Status: %d", nes->ppu->status);
    igText("PPU Scroll Latch: %d", nes->ppu->scroll_latch);
    igText("PPU Address Latch: %d", nes->ppu->addr_latch);
    igText("PPU Scroll X: %d", nes->ppu->scroll_x);
    igText("PPU Scroll Y: %d", nes->ppu->scroll_y);
    igText("PPU VRAM Address: %d", nes->ppu->vram_addr);
    igText("PPU Temp Address: %d", nes->ppu->temp_addr);
    igText("PPU Fine X: %d", nes->ppu->fine_x);
    igText("PPU Data Buffer: %d", nes->ppu->data_buffer);
    igText("PPU OAM Address: %d", nes->ppu->oam_addr);
    igText("PPU OAM Data: %d", nes->ppu->oam[nes->ppu->oam_addr]);
    igText("PPU OAM Data Address: %d", nes->ppu->oam_addr);
    igText("PPU OAM Data Value: %d", nes->ppu->oam[nes->ppu->oam_addr]);
    igText("PPU OAM Data Address: %d", nes->ppu->oam_addr);
    igText("PPU OAM Data Value: %d", nes->ppu->oam[nes->ppu->oam_addr]);
    igText("PPU OAM Data Address: %d", nes->ppu->oam_addr);
    igEnd();
    //igShowDemoWindow(NULL);
}

bool ui_showDisassembler = 0;

void UI_DrawDisassembler(NES* nes) 
{
    igBegin("Disassembler", &ui_showDisassembler, 0);
    igTextColored((ImVec4){1.0f, 0.0f, 0.0f, 1.0f}, "%s", "Disassembled View");
    uint16_t currentPC = nes->cpu->pc;

    igBeginTable("Disassembled View", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable, (ImVec2){250,16*16}, 30);

    igTableSetupColumn("Address", ImGuiTableColumnFlags_WidthStretch, 10, 0);
    igTableSetupColumn("Opcode", ImGuiTableColumnFlags_WidthStretch, 10, 0);
    igTableHeadersRow();

    uint16_t next = 0;
    for (size_t i = 0; i < 16; i++)
    {
        igTableNextRow(0, 20);
        char disasm_buf[128];



        if (i == 0)
        {
            next = disassemble(nes, currentPC, disasm_buf, sizeof(disasm_buf));
            igTableSetColumnIndex(0);
            igText("0x%04X", currentPC);
        } else {
            igTableSetColumnIndex(0);
            igText("0x%04X", next);
            next = disassemble(nes, next, disasm_buf, sizeof(disasm_buf));
        }


        igTableSetColumnIndex(1);
        igText("%-20s", disasm_buf);
        //igTableEndRow(igTableFindByID("Disassembled View"))
    }
    



    igEndTable();
    
    

    if(igButton("Step", (ImVec2){100,20}))
        NES_Step(nes);

    igEnd();
}

void UI_Update(NES* nes)
{
    SDL_Event e;

    while (SDL_PollEvent(&e)) {

        switch (e.type)
        {
            case SDL_QUIT:
                exit(0);
                break;
            case SDL_WINDOWEVENT:
                switch (e.window.event)
                {
                    case SDL_WINDOWEVENT_RESIZED:
                    case SDL_WINDOWEVENT_SIZE_CHANGED:
                        break;
                }
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
            case SDL_MOUSEMOTION:
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                //C_InputProcessEvent(&e);
                break;
        }

        ImGui_ImplSDL2_ProcessEvent(&e);
    }

    //glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    igNewFrame();

    //igShowDemoWindow((bool *) &ui_running);

    if (ioptr->ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        ImGuiID dockspace_id = igGetID_Str("MyDockSpace");
        igDockSpaceOverViewport(dockspace_id, igGetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode, NULL);
        // Or create a specific window as a dock space:
        // ImGui::Begin("MainDockArea", ..., ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ...);
        // ImGuiID dockspace_id = ImGui::GetID("MainDockArea");
        // ImGui::DockSpace(dockspace_id, ImVec2(0,0), ImGuiDockNodeFlags_None);
        // ImGui::End();
    }    

    UI_Draw(nes);
    if (ui_showDisassembler) UI_DrawDisassembler(nes);

    igRender();
    ImGui_ImplOpenGL3_RenderDrawData(igGetDrawData());

    if (ioptr->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        SDL_Window* backup_current_window = SDL_GL_GetCurrentWindow();
        SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
        igUpdatePlatformWindows();
        igRenderPlatformWindowsDefault(NULL,NULL);
        SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
    }

    SDL_GL_SwapWindow(window);
}

void UI_Shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
}