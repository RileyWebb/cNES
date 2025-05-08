#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <SDL2/SDL.h>
#include <cimgui.h>
#include <cimgui_impl.h>
#include <cimplot.h>
#include <GL/glew.h>

#include "cNES/nes.h"
#include "cNES/cpu.h"
#include "cNES/debugging.h"

#include "ui.h"

SDL_Window *window;
SDL_GLContext *glContext;

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
    if (!glContext)
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

    UI_InitStlye();
}

void UI_Draw()
{
    igBegin("cEMU", NULL, 0);
    igText("Hello, world!");
    igEnd();
    igShowDemoWindow(NULL);
}

bool ui_showDisassembler = 1;

void UI_DrawDisassembler(NES* nes) 
{
    igBegin("Disassembler", &ui_showDisassembler, 0);
    igText("Hello, world!");
    
    uint16_t currentPC = nes->cpu->pc;

    igBeginTable("Disassembled View", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable, (ImVec2){250,500}, 30);

    //igTableSetupColumn("Address", ImGuiTableColumnFlags_WidthStretch, 10, 0);
    //igTableSetupColumn("Opcode", ImGuiTableColumnFlags_WidthStretch, 10, 0);
    igTableHeadersRow();

    for (size_t i = 0; i < 16; i++)
    {

        igTableNextRow(ImGuiTableRowFlags_Headers, 20);
        char disasm_buf[128];

        disassemble(nes, currentPC + i, disasm_buf, sizeof(disasm_buf));

        igTableSetColumnIndex(0);
        igText("0x%04X", currentPC + i);
        igTableSetColumnIndex(1);
        igText("%-20s",disasm_buf);

        //igTableEndRow(igTableFindByID("Disassembled View"))
    }
    



    igEndTable();
    
    

    if(igButton("Step", (ImVec2){100,20}))
        CPU_Step(nes->cpu);

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
                        //SDL_GL_GetDrawableSize(window, &c_windowWidth, &c_windowHeight);
                        //R_ResizeViewport(c_windowWidth, c_windowHeight);
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

    UI_Draw();
    UI_DrawDisassembler(nes);

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