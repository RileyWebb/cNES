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

void Frontend_GameScreenWindow(NES *nes)
{
    (void)nes;

    if (!frontend_showGameScreen)
        return;

    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){0, 0});
    if (igBegin("Game Screen", &frontend_showGameScreen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        static uint32_t frame_buffer_copy[256 * 240];
        bool has_frame = Frontend_CopyFrameBuffer(frame_buffer_copy, 256 * 240);

        if (!gpu_device)
        {
            igTextColored((ImVec4){1.f, 1.f, 0.f, 1.f}, "Error: No SDL_GPUDevice available.");
        }
        else if (!has_frame)
        {
            ImVec2 avail_size;
            igGetContentRegionAvail(&avail_size);
            const char *msg = "No frame available yet.";
            ImVec2 text_size;
            igCalcTextSize(&text_size, msg, NULL, false, 0.0f);
            igSetCursorPosX((avail_size.x - text_size.x) * 0.5f);
            igSetCursorPosY((avail_size.y - text_size.y) * 0.5f);
            igTextDisabled("%s", msg);
        }
        else
        {
            // Create texture if needed
            if (ppu_game_texture == NULL)
            {
                SDL_GPUTextureCreateInfo texture_info = {0};
                texture_info.type = SDL_GPU_TEXTURETYPE_2D;
                texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                texture_info.width = 256;
                texture_info.height = 240;
                texture_info.layer_count_or_depth = 1;
                texture_info.num_levels = 1;
                texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
                ppu_game_texture = SDL_CreateGPUTexture(gpu_device, &texture_info);
                if (!ppu_game_texture)
                {
                    DEBUG_ERROR("GameScreen: Failed to create PPU game texture: %s", SDL_GetError());
                    igTextColored((ImVec4){1.f, 0.f, 0.f, 1.f}, "Failed to create PPU game texture.");
                    igEnd();
                    igPopStyleVar(1);
                    return;
                }

                ppu_game_texture_sampler_binding.texture = ppu_game_texture;
            }

            // Create sampler if needed
            if (ppu_game_sampler == NULL)
            {
                SDL_GPUSamplerCreateInfo sampler_info = {0};
                sampler_info.min_filter = SDL_GPU_FILTER_NEAREST;
                sampler_info.mag_filter = SDL_GPU_FILTER_NEAREST;
                sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
                sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
                sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
                sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
                ppu_game_sampler = SDL_CreateGPUSampler(gpu_device, &sampler_info);
                if (!ppu_game_sampler)
                {
                    DEBUG_ERROR("GameScreen: Failed to create PPU game sampler: %s", SDL_GetError());
                    igTextColored((ImVec4){1.f, 0.f, 0.f, 1.f}, "Failed to create PPU game sampler.");
                    igEnd();
                    igPopStyleVar(1);
                    return;
                }

                ppu_game_texture_sampler_binding.sampler = ppu_game_sampler;
            }

            // Create transfer buffer if needed
            if (ppu_game_transfer_buffer == NULL)
            {
                SDL_GPUTransferBufferCreateInfo transfer_create_info = {0};
                transfer_create_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                transfer_create_info.size = 256 * 240 * 4; // Only need space for one frame
                ppu_game_transfer_buffer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_create_info);
                if (!ppu_game_transfer_buffer)
                {
                    DEBUG_ERROR("GameScreen: Failed to create PPU transfer buffer: %s", SDL_GetError());
                    igTextColored((ImVec4){1.f, 0.f, 0.f, 1.f}, "Failed to create PPU transfer buffer.");
                    igEnd();
                    igPopStyleVar(1);
                    return;
                }
            }

            // Update texture if we have new frame data
            if (ppu_game_texture)
            {
                // Map transfer buffer
                void *mapped_memory = SDL_MapGPUTransferBuffer(gpu_device, ppu_game_transfer_buffer, true);
                if (!mapped_memory)
                {
                    DEBUG_ERROR("GameScreen: Failed to map GPU transfer buffer: %s", SDL_GetError());
                }
                else
                {
                    // Copy frame data to transfer buffer
                    memcpy(mapped_memory, frame_buffer_copy, 256 * 240 * sizeof(uint32_t));
                    SDL_UnmapGPUTransferBuffer(gpu_device, ppu_game_transfer_buffer);

                    // Create command buffer for the copy operation
                    SDL_GPUCommandBuffer *cmd_buffer = SDL_AcquireGPUCommandBuffer(gpu_device);
                    if (cmd_buffer)
                    {
                        SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd_buffer);
                        if (copy_pass)
                        {
                            SDL_UploadToGPUTexture(
                                copy_pass,
                                &(SDL_GPUTextureTransferInfo){
                                    .transfer_buffer = ppu_game_transfer_buffer,
                                    .offset = 0,
                                    .pixels_per_row = 256,
                                    .rows_per_layer = 240},
                                &(SDL_GPUTextureRegion){
                                    .texture = ppu_game_texture,
                                    .x = 0,
                                    .y = 0,
                                    .z = 0,
                                    .w = 256,
                                    .h = 240,
                                    .d = 1},
                                true);
                            SDL_EndGPUCopyPass(copy_pass);
                        }
                        else
                        {
                            DEBUG_ERROR("GameScreen: Failed to begin GPU copy pass: %s", SDL_GetError());
                        }
                        SDL_SubmitGPUCommandBuffer(cmd_buffer);
                    }
                    else
                    {
                        DEBUG_ERROR("GameScreen: Failed to acquire GPU command buffer for texture upload: %s", SDL_GetError());
                    }
                }
            }

            // Calculate display size maintaining aspect ratio
            ImVec2 window_content_region_size;
            igGetContentRegionAvail(&window_content_region_size);
            float aspect_ratio = 256.0f / 240.0f;
            ImVec2 image_size = window_content_region_size;

            if (window_content_region_size.x / aspect_ratio > window_content_region_size.y)
            {
                image_size.x = window_content_region_size.y * aspect_ratio;
            }
            else
            {
                image_size.y = window_content_region_size.x / aspect_ratio;
            }

            // Center the image
            float offset_x = (window_content_region_size.x - image_size.x) * 0.5f;
            float offset_y = (window_content_region_size.y - image_size.y) * 0.5f;
            igSetCursorPos((ImVec2){igGetCursorPosX() + offset_x, igGetCursorPosY() + offset_y});

            // Display the texture
            if (ppu_game_texture_sampler_binding.texture && ppu_game_texture_sampler_binding.sampler)
            {
                igImage((ImTextureID)(uintptr_t)&ppu_game_texture_sampler_binding, image_size, (ImVec2){0, 0}, (ImVec2){1, 1});
            }
            else
            {
                igText("Game texture or sampler not ready.");
            }
        }
    }
    igEnd();
    igPopStyleVar(1);
}



