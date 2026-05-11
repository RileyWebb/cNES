#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>

#include "frontend/frontend_style.h"

static inline ImVec4 hexToVec4(unsigned int hex_color, float alpha)
{
    return (ImVec4){
        ((float)((hex_color >> 16U) & 0xFFU)) / 255.0f,
        ((float)((hex_color >> 8U) & 0xFFU)) / 255.0f,
        ((float)(hex_color & 0xFFU)) / 255.0f,
        alpha};
}

void FrontendStyle_Apply(Frontend_Theme theme)
{
    ImGuiStyle *style = igGetStyle();
    ImVec4 *colors = style->Colors;

    if (theme == FRONTEND_THEME_EXCELLENCY)
    {
        style->Alpha = 1.0f;
        style->DisabledAlpha = 0.6f;
        style->WindowPadding = (ImVec2){10.0f, 10.0f};
        style->WindowRounding = 0.0f;
        style->WindowBorderSize = 1.0f;
        style->WindowMinSize = (ImVec2){32.0f, 32.0f};
        style->WindowTitleAlign = (ImVec2){0.5f, 0.5f};
        style->WindowMenuButtonPosition = ImGuiDir_None;
        style->ChildRounding = 6.0f;
        style->ChildBorderSize = 1.0f;
        style->PopupRounding = 6.0f;
        style->PopupBorderSize = 1.0f;
        style->FramePadding = (ImVec2){8.0f, 6.0f};
        style->FrameRounding = 6.0f;
        style->FrameBorderSize = 1.0f;
        style->ItemSpacing = (ImVec2){6.0f, 6.0f};
        style->ItemInnerSpacing = (ImVec2){4.0f, 4.0f};
        style->CellPadding = (ImVec2){4.0f, 2.0f};
        style->IndentSpacing = 11.0f;
        style->ColumnsMinSpacing = 6.0f;
        style->ScrollbarSize = 14.0f;
        style->ScrollbarRounding = 6.0f;
        style->GrabMinSize = 10.0f;
        style->GrabRounding = 6.0f;
        style->TabRounding = 6.0f;
        style->TabBorderSize = 1.0f;
        style->TabCloseButtonMinWidthSelected = 0.0f;
        style->TabCloseButtonMinWidthUnselected = 0.0f;
        style->ColorButtonPosition = ImGuiDir_Right;
        style->ButtonTextAlign = (ImVec2){0.5f, 0.5f};
        style->SelectableTextAlign = (ImVec2){0.0f, 0.0f};

        colors[ImGuiCol_Text] = (ImVec4){1.0f, 1.0f, 1.0f, 1.0f};
        colors[ImGuiCol_TextDisabled] = (ImVec4){0.5019608f, 0.5019608f, 0.5019608f, 1.0f};
        colors[ImGuiCol_WindowBg] = (ImVec4){0.08235294f, 0.08235294f, 0.08235294f, 1.0f};
        colors[ImGuiCol_ChildBg] = (ImVec4){0.15686275f, 0.15686275f, 0.15686275f, 1.0f};
        colors[ImGuiCol_PopupBg] = (ImVec4){0.19607843f, 0.19607843f, 0.19607843f, 1.0f};
        colors[ImGuiCol_Border] = (ImVec4){0.101960786f, 0.101960786f, 0.101960786f, 1.0f};
        colors[ImGuiCol_BorderShadow] = (ImVec4){0.0f, 0.0f, 0.0f, 0.0f};
        colors[ImGuiCol_FrameBg] = (ImVec4){0.05882353f, 0.05882353f, 0.05882353f, 1.0f};
        colors[ImGuiCol_FrameBgHovered] = (ImVec4){0.09019608f, 0.09019608f, 0.09019608f, 1.0f};
        colors[ImGuiCol_FrameBgActive] = (ImVec4){0.05882353f, 0.05882353f, 0.05882353f, 1.0f};
        colors[ImGuiCol_TitleBg] = (ImVec4){0.08235294f, 0.08235294f, 0.08235294f, 1.0f};
        colors[ImGuiCol_TitleBgActive] = (ImVec4){0.08235294f, 0.08235294f, 0.08235294f, 1.0f};
        colors[ImGuiCol_TitleBgCollapsed] = (ImVec4){0.15294118f, 0.15294118f, 0.15294118f, 1.0f};
        colors[ImGuiCol_MenuBarBg] = (ImVec4){0.0f, 0.0f, 0.0f, 0.0f};
        colors[ImGuiCol_ScrollbarBg] = (ImVec4){0.019607844f, 0.019607844f, 0.019607844f, 0.53f};
        colors[ImGuiCol_ScrollbarGrab] = (ImVec4){0.30980393f, 0.30980393f, 0.30980393f, 1.0f};
        colors[ImGuiCol_ScrollbarGrabHovered] = (ImVec4){0.4117647f, 0.4117647f, 0.4117647f, 1.0f};
        colors[ImGuiCol_ScrollbarGrabActive] = (ImVec4){0.50980395f, 0.50980395f, 0.50980395f, 1.0f};
        colors[ImGuiCol_CheckMark] = (ImVec4){0.7529412f, 0.7529412f, 0.7529412f, 1.0f};
        colors[ImGuiCol_SliderGrab] = (ImVec4){0.50980395f, 0.50980395f, 0.50980395f, 0.7f};
        colors[ImGuiCol_SliderGrabActive] = (ImVec4){0.65882355f, 0.65882355f, 0.65882355f, 1.0f};
        colors[ImGuiCol_Button] = (ImVec4){0.21960784f, 0.21960784f, 0.21960784f, 0.784f};
        colors[ImGuiCol_ButtonHovered] = (ImVec4){0.27450982f, 0.27450982f, 0.27450982f, 1.0f};
        colors[ImGuiCol_ButtonActive] = (ImVec4){0.21960784f, 0.21960784f, 0.21960784f, 0.588f};
        colors[ImGuiCol_Header] = (ImVec4){0.18431373f, 0.18431373f, 0.18431373f, 1.0f};
        colors[ImGuiCol_HeaderHovered] = (ImVec4){0.18431373f, 0.18431373f, 0.18431373f, 1.0f};
        colors[ImGuiCol_HeaderActive] = (ImVec4){0.18431373f, 0.18431373f, 0.18431373f, 1.0f};
        colors[ImGuiCol_Separator] = (ImVec4){0.101960786f, 0.101960786f, 0.101960786f, 1.0f};
        colors[ImGuiCol_SeparatorHovered] = (ImVec4){0.15294118f, 0.7254902f, 0.9490196f, 0.588f};
        colors[ImGuiCol_SeparatorActive] = (ImVec4){0.15294118f, 0.7254902f, 0.9490196f, 1.0f};
        colors[ImGuiCol_ResizeGrip] = (ImVec4){0.9098039f, 0.9098039f, 0.9098039f, 0.25f};
        colors[ImGuiCol_ResizeGripHovered] = (ImVec4){0.8117647f, 0.8117647f, 0.8117647f, 0.67f};
        colors[ImGuiCol_ResizeGripActive] = (ImVec4){0.45882353f, 0.45882353f, 0.45882353f, 0.95f};
        colors[ImGuiCol_Tab] = (ImVec4){0.08235294f, 0.08235294f, 0.08235294f, 1.0f};
        colors[ImGuiCol_TabHovered] = (ImVec4){1.0f, 0.88235295f, 0.5294118f, 0.118f};
        colors[ImGuiCol_TabSelected] = (ImVec4){1.0f, 0.88235295f, 0.5294118f, 0.235f};
        colors[ImGuiCol_TabDimmed] = (ImVec4){0.08235294f, 0.08235294f, 0.08235294f, 1.0f};
        colors[ImGuiCol_TabDimmedSelected] = (ImVec4){1.0f, 0.88235295f, 0.5294118f, 0.118f};
        colors[ImGuiCol_PlotLines] = (ImVec4){0.6117647f, 0.6117647f, 0.6117647f, 1.0f};
        colors[ImGuiCol_PlotLinesHovered] = (ImVec4){1.0f, 0.43137255f, 0.34901962f, 1.0f};
        colors[ImGuiCol_PlotHistogram] = (ImVec4){0.9019608f, 0.7019608f, 0.0f, 1.0f};
        colors[ImGuiCol_PlotHistogramHovered] = (ImVec4){1.0f, 0.6f, 0.0f, 1.0f};
        colors[ImGuiCol_TableHeaderBg] = (ImVec4){0.18431373f, 0.18431373f, 0.18431373f, 1.0f};
        colors[ImGuiCol_TableBorderStrong] = (ImVec4){0.30980393f, 0.30980393f, 0.34901962f, 1.0f};
        colors[ImGuiCol_TableBorderLight] = (ImVec4){0.101960786f, 0.101960786f, 0.101960786f, 1.0f};
        colors[ImGuiCol_TableRowBg] = (ImVec4){0.0f, 0.0f, 0.0f, 0.0f};
        colors[ImGuiCol_TableRowBgAlt] = (ImVec4){1.0f, 1.0f, 1.0f, 0.06f};
        colors[ImGuiCol_TextSelectedBg] = (ImVec4){0.15294118f, 0.7254902f, 0.9490196f, 0.35f};
        colors[ImGuiCol_DragDropTarget] = (ImVec4){1.0f, 1.0f, 0.0f, 0.9f};
        colors[ImGuiCol_NavCursor] = (ImVec4){0.15294118f, 0.7254902f, 0.9490196f, 0.8f};
        colors[ImGuiCol_NavWindowingHighlight] = (ImVec4){1.0f, 1.0f, 1.0f, 0.7f};
        colors[ImGuiCol_NavWindowingDimBg] = (ImVec4){0.8f, 0.8f, 0.8f, 0.2f};
        colors[ImGuiCol_ModalWindowDimBg] = (ImVec4){0.8f, 0.8f, 0.8f, 0.35f};
    }
    else if (theme == FRONTEND_THEME_LIGHT)
    {
        igStyleColorsLight(NULL);
        colors[ImGuiCol_WindowBg] = (ImVec4){0.94f, 0.94f, 0.94f, 1.00f};
        colors[ImGuiCol_FrameBg] = (ImVec4){1.00f, 1.00f, 1.00f, 1.00f};
        colors[ImGuiCol_TitleBgActive] = (ImVec4){0.82f, 0.82f, 0.82f, 1.00f};
        colors[ImGuiCol_MenuBarBg] = (ImVec4){0.86f, 0.86f, 0.86f, 1.00f};
        colors[ImGuiCol_Header] = (ImVec4){0.90f, 0.90f, 0.90f, 1.00f};
        colors[ImGuiCol_Button] = (ImVec4){0.2f, 0.5f, 0.8f, 1.0f};
        colors[ImGuiCol_ButtonHovered] = (ImVec4){0.3f, 0.6f, 0.9f, 1.0f};
        colors[ImGuiCol_ButtonActive] = (ImVec4){0.1f, 0.4f, 0.7f, 1.0f};
        colors[ImGuiCol_CheckMark] = (ImVec4){0.26f, 0.59f, 0.98f, 1.00f};
        colors[ImGuiCol_SliderGrab] = (ImVec4){0.24f, 0.52f, 0.88f, 1.00f};
        colors[ImGuiCol_SliderGrabActive] = (ImVec4){0.26f, 0.59f, 0.98f, 1.00f};
    }
    else
    {
        igStyleColorsDark(NULL);
        ImVec4 dt_text = hexToVec4(0xE2E8F0, 1.0f);
        ImVec4 dt_text_disabled = hexToVec4(0x718096, 1.0f);
        ImVec4 dt_bg_main = hexToVec4(0x1A202C, 1.0f);
        ImVec4 dt_bg_secondary = hexToVec4(0x2D3748, 1.0f);
        ImVec4 dt_bg_frame = hexToVec4(0x252C3B, 0.85f);
        ImVec4 dt_border = hexToVec4(0x4A5568, 0.7f);
        ImVec4 dt_accent_primary = hexToVec4(0x4299E1, 1.0f);
        ImVec4 dt_accent_primary_hover = hexToVec4(0x3182CE, 1.0f);
        ImVec4 dt_accent_primary_active = hexToVec4(0x2B6CB0, 1.0f);
        ImVec4 dt_accent_secondary = hexToVec4(0x38B2AC, 1.0f);
        ImVec4 dt_accent_secondary_hover = hexToVec4(0x319795, 1.0f);
        ImVec4 dt_accent_secondary_active = hexToVec4(0x2C7A7B, 1.0f);

        colors[ImGuiCol_Text] = dt_text;
        colors[ImGuiCol_TextDisabled] = dt_text_disabled;
        colors[ImGuiCol_WindowBg] = dt_bg_main;
        colors[ImGuiCol_ChildBg] = dt_bg_main;
        colors[ImGuiCol_PopupBg] = dt_bg_secondary;
        colors[ImGuiCol_Border] = dt_border;
        colors[ImGuiCol_BorderShadow] = (ImVec4){0.00f, 0.00f, 0.00f, 0.00f};
        colors[ImGuiCol_FrameBg] = dt_bg_frame;
        colors[ImGuiCol_FrameBgHovered] = hexToVec4(0x313A4C, 0.9f);
        colors[ImGuiCol_FrameBgActive] = hexToVec4(0x3A445D, 1.0f);
        colors[ImGuiCol_TitleBg] = hexToVec4(0x161A23, 1.0f);
        colors[ImGuiCol_TitleBgActive] = dt_accent_primary;
        colors[ImGuiCol_TitleBgCollapsed] = hexToVec4(0x1A202C, 0.75f);
        colors[ImGuiCol_MenuBarBg] = dt_bg_secondary;
        colors[ImGuiCol_ScrollbarBg] = hexToVec4(0x171923, 0.53f);
        colors[ImGuiCol_ScrollbarGrab] = hexToVec4(0x4A5568, 1.0f);
        colors[ImGuiCol_ScrollbarGrabHovered] = hexToVec4(0x718096, 1.0f);
        colors[ImGuiCol_ScrollbarGrabActive] = hexToVec4(0xA0AEC0, 1.0f);
        colors[ImGuiCol_CheckMark] = dt_accent_secondary;
        colors[ImGuiCol_SliderGrab] = dt_accent_secondary;
        colors[ImGuiCol_SliderGrabActive] = dt_accent_secondary_active;
        colors[ImGuiCol_Button] = dt_accent_primary;
        colors[ImGuiCol_ButtonHovered] = dt_accent_primary_hover;
        colors[ImGuiCol_ButtonActive] = dt_accent_primary_active;
        colors[ImGuiCol_Header] = (ImVec4){dt_accent_secondary.x, dt_accent_secondary.y, dt_accent_secondary.z, 0.6f};
        colors[ImGuiCol_HeaderHovered] = (ImVec4){dt_accent_secondary_hover.x, dt_accent_secondary_hover.y, dt_accent_secondary_hover.z, 0.8f};
        colors[ImGuiCol_HeaderActive] = dt_accent_secondary_active;
        colors[ImGuiCol_Separator] = dt_border;
        colors[ImGuiCol_SeparatorHovered] = dt_accent_secondary_hover;
        colors[ImGuiCol_SeparatorActive] = dt_accent_secondary_active;
        colors[ImGuiCol_ResizeGrip] = (ImVec4){dt_accent_secondary.x, dt_accent_secondary.y, dt_accent_secondary.z, 0.30f};
        colors[ImGuiCol_ResizeGripHovered] = (ImVec4){dt_accent_secondary_hover.x, dt_accent_secondary_hover.y, dt_accent_secondary_hover.z, 0.67f};
        colors[ImGuiCol_ResizeGripActive] = dt_accent_secondary_active;
        colors[ImGuiCol_Tab] = dt_bg_frame;
        colors[ImGuiCol_TabHovered] = (ImVec4){dt_accent_primary_hover.x, dt_accent_primary_hover.y, dt_accent_primary_hover.z, 0.4f};
        colors[ImGuiCol_DockingPreview] = (ImVec4){dt_accent_primary.x, dt_accent_primary.y, dt_accent_primary.z, 0.35f};
        colors[ImGuiCol_DockingEmptyBg] = hexToVec4(0x2D3748, 1.0f);
        colors[ImGuiCol_PlotLines] = hexToVec4(0x90CDF4, 1.0f);
        colors[ImGuiCol_PlotLinesHovered] = hexToVec4(0x63B3ED, 1.0f);
        colors[ImGuiCol_PlotHistogram] = hexToVec4(0xF6AD55, 1.0f);
        colors[ImGuiCol_PlotHistogramHovered] = hexToVec4(0xED8936, 1.0f);
        colors[ImGuiCol_TextSelectedBg] = (ImVec4){dt_accent_primary.x, dt_accent_primary.y, dt_accent_primary.z, 0.35f};
        colors[ImGuiCol_DragDropTarget] = hexToVec4(0xF6E05E, 0.90f);
        colors[ImGuiCol_NavWindowingHighlight] = hexToVec4(0xFFFFFF, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg] = hexToVec4(0x808080, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg] = hexToVec4(0x1A202C, 0.65f);
    }

    style->WindowRounding = 5.0f;
    style->FrameRounding = 4.0f;
    style->GrabRounding = 4.0f;
    style->PopupRounding = 4.0f;
    style->ScrollbarRounding = 6.0f;
    style->TabRounding = 4.0f;

    style->WindowPadding = (ImVec2){8.0f, 8.0f};
    style->FramePadding = (ImVec2){6.0f, 4.0f};
    style->ItemSpacing = (ImVec2){8.0f, 5.0f};
    style->ItemInnerSpacing = (ImVec2){5.0f, 5.0f};
    style->IndentSpacing = 20.0f;
    style->ScrollbarSize = 15.0f;
    style->GrabMinSize = 12.0f;

    style->WindowBorderSize = 1.0f;
    style->FrameBorderSize = 0.0f;
    style->PopupBorderSize = 1.0f;
    style->TabBorderSize = 0.0f;
}
