/*
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_iostream.h> // For SDL_RWFromFile
#include <stdio.h> // For printf, if debugging
#include "cNES/ppu.h"
#include "cNES/nes.h" // For NES struct if needed for settings
#include "debug.h"    // For UI_Log or similar

// Placeholder for PPU registers structure to be sent to GPU
typedef struct {
    uint8_t ctrl;
    uint8_t mask;
    uint16_t vram_addr_v; // Loopy's v
    uint16_t temp_addr_t; // Loopy's t
    uint8_t fine_x;
    int scanline;
    int cycle;
    // Add other relevant PPU state for the shader
} PPU_Registers_GPU;


bool PPU_SDLGPU_Init(PPU* ppu, SDL_GPUDevice* device)
{
    if (!ppu || !device) {
        DEBUG_ERROR("PPU_SDLGPU_Init: Invalid PPU or SDL_GPUDevice pointer.");
        return false;
    }
    ppu->gpu_device_ptr = device;

    // 1. Create GPU Buffers
    SDL_GPUBufferCreateInfo buffer_info = {0};
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    buffer_info.size = PPU_VRAM_SIZE;
    ppu->vram_buffer_gpu = SDL_CreateGPUBuffer(device, &buffer_info);
    if (!ppu->vram_buffer_gpu) { DEBUG_ERROR("Failed to create VRAM GPU buffer"); goto cleanup; }

    buffer_info.size = PPU_OAM_SIZE;
    ppu->oam_buffer_gpu = SDL_CreateGPUBuffer(device, &buffer_info);
    if (!ppu->oam_buffer_gpu) { DEBUG_ERROR("Failed to create OAM GPU buffer"); goto cleanup; }

    buffer_info.size = PPU_PALETTE_RAM_SIZE;
    ppu->palette_buffer_gpu = SDL_CreateGPUBuffer(device, &buffer_info);
    if (!ppu->palette_buffer_gpu) { DEBUG_ERROR("Failed to create Palette RAM GPU buffer"); goto cleanup; }

    buffer_info.usage = SDL_GPU_BUFFERUSAGE_UNIFORM | SDL_GPU_BUFFERUSAGE_TRANSFER_DST;
    buffer_info.size = sizeof(PPU_Registers_GPU); // Buffer for PPU registers
    ppu->registers_buffer_gpu = SDL_CreateGPUBuffer(device, &buffer_info);
    if (!ppu->registers_buffer_gpu) { DEBUG_ERROR("Failed to create Registers GPU buffer"); goto cleanup; }

    buffer_info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_TRANSFER_SRC;
    buffer_info.size = PPU_FRAMEBUFFER_WIDTH * PPU_FRAMEBUFFER_HEIGHT * sizeof(uint32_t);
    ppu->framebuffer_gpu = SDL_CreateGPUBuffer(device, &buffer_info);
    if (!ppu->framebuffer_gpu) { DEBUG_ERROR("Failed to create Framebuffer GPU buffer"); goto cleanup; }

    // 2. Load and Create Compute Shader
    // IMPORTANT: "ppu_compute.spv" must exist and be a valid SPIR-V compute shader.
    SDL_IOStream* shader_file = SDL_IOFromFile("ppu_compute.spv", "rb");
    if (!shader_file) {
        DEBUG_ERROR("Failed to open ppu_compute.spv. Make sure the file exists.");
        goto cleanup;
    }

    size_t shader_size = SDL_GetIOSize(shader_file);
    uint8_t* shader_bytecode = (uint8_t*)SDL_malloc((size_t)shader_size);
    if (!shader_bytecode) { SDL_CloseIO(shader_file); DEBUG_ERROR("Failed to allocate memory for shader bytecode"); goto cleanup; }
    
    if (SDL_ReadIO(shader_file, shader_bytecode, (size_t)shader_size) != (size_t)shader_size) {
        SDL_free(shader_bytecode);
        SDL_CloseIO(shader_file);
        DEBUG_ERROR("Failed to read shader bytecode");
        goto cleanup;
    }
    SDL_CloseIO(shader_file);

    SDL_GPUShaderCreateInfo shader_create_info = {0};
    shader_create_info.code = shader_bytecode;
    shader_create_info.code_size = (uint32_t)shader_size;
    shader_create_info.entrypoint = "main"; // Entry point of your compute shader
    shader_create_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    shader_create_info.stage = SDL_CreateGPUComputePipeline;

    ppu->compute_shader = SDL_CreateGPUShader(device, &shader_create_info);
    SDL_free(shader_bytecode); // Bytecode is copied by SDL_CreateGPUShader
    if (!ppu->compute_shader) { DEBUG_ERROR("Failed to create compute shader"); goto cleanup; }

    // 3. Create Compute Pipeline
    SDL_GPUComputePipelineCreateInfo pipeline_info = {0};
    pipeline_info.shader = ppu->compute_shader;
    // pipeline_info.read_only_storage_texture_count = 0; // Adjust if using storage textures
    // pipeline_info.read_only_storage_buffer_count = 3;  // VRAM, OAM, Palette
    // pipeline_info.read_write_storage_texture_count = 0;
    // pipeline_info.read_write_storage_buffer_count = 1; // Framebuffer
    // pipeline_info.uniform_buffer_count = 1;            // Registers
    // pipeline_info.sampler_count = 0;
    // pipeline_info.thread_group_size_x = 8; // Example, match your shader
    // pipeline_info.thread_group_size_y = 8; // Example
    // pipeline_info.thread_group_size_z = 1; // Example

    ppu->compute_pipeline = SDL_CreateGPUComputePipeline(device, &pipeline_info);
    if (!ppu->compute_pipeline) { DEBUG_ERROR("Failed to create compute pipeline"); goto cleanup; }
    
    // 4. Setup Resource Bindings (example, indices must match shader)
    ppu->resource_bindings[0].type = SDL_GPU_SHADERRESOURCE_UNIFORM_BUFFER;
    ppu->resource_bindings[0].resource.uniform_buffer_binding.buffer = ppu->registers_buffer_gpu;
    ppu->resource_bindings[0].resource.uniform_buffer_binding.offset = 0;
    ppu->resource_bindings[0].resource.uniform_buffer_binding.size = sizeof(PPU_Registers_GPU);
    
    ppu->resource_bindings[1].type = SDL_GPU_SHADERRESOURCE_STORAGE_BUFFER;
    ppu->resource_bindings[1].resource.storage_buffer_binding.buffer = ppu->vram_buffer_gpu;
    
    ppu->resource_bindings[2].type = SDL_GPU_SHADERRESOURCE_STORAGE_BUFFER;
    ppu->resource_bindings[2].resource.storage_buffer_binding.buffer = ppu->oam_buffer_gpu;
    
    ppu->resource_bindings[3].type = SDL_GPU_SHADERRESOURCE_STORAGE_BUFFER;
    ppu->resource_bindings[3].resource.storage_buffer_binding.buffer = ppu->palette_buffer_gpu;
    
    ppu->resource_bindings[4].type = SDL_GPU_SHADERRESOURCE_STORAGE_BUFFER;
    ppu->resource_bindings[4].resource.storage_buffer_binding.buffer = ppu->framebuffer_gpu;

    DEBUG_INFO("PPU_SDLGPU_Init successful.");
    return true;

cleanup:
    DEBUG_ERROR("PPU_SDLGPU_Init failed. Cleaning up partially initialized resources.");
    PPU_SDLGPU_Shutdown(ppu); // Reuse shutdown to clean up
    return false;
}

void PPU_SDLGPU_Shutdown(PPU* ppu)
{
    if (!ppu || !ppu->gpu_device_ptr) return;

    SDL_GPUDevice* device = ppu->gpu_device_ptr;

    if (ppu->compute_pipeline) { SDL_ReleaseGPUComputePipeline(device, ppu->compute_pipeline); ppu->compute_pipeline = NULL; }
    if (ppu->compute_shader) { SDL_ReleaseGPUShader(device, ppu->compute_shader); ppu->compute_shader = NULL; }
    
    if (ppu->framebuffer_gpu) { SDL_ReleaseGPUBuffer(device, ppu->framebuffer_gpu); ppu->framebuffer_gpu = NULL; }
    if (ppu->registers_buffer_gpu) { SDL_ReleaseGPUBuffer(device, ppu->registers_buffer_gpu); ppu->registers_buffer_gpu = NULL; }
    if (ppu->palette_buffer_gpu) { SDL_ReleaseGPUBuffer(device, ppu->palette_buffer_gpu); ppu->palette_buffer_gpu = NULL; }
    if (ppu->oam_buffer_gpu) { SDL_ReleaseGPUBuffer(device, ppu->oam_buffer_gpu); ppu->oam_buffer_gpu = NULL; }
    if (ppu->vram_buffer_gpu) { SDL_ReleaseGPUBuffer(device, ppu->vram_buffer_gpu); ppu->vram_buffer_gpu = NULL; }
    
    ppu->gpu_device_ptr = NULL; // Don't release the device itself, it's owned elsewhere
    DEBUG_INFO("PPU_SDLGPU_Shutdown complete.");
}

void PPU_SDLGPU_UploadData(PPU* ppu) {
    if (!ppu || !ppu->gpu_device_ptr) return;
    SDL_GPUDevice* device = ppu->gpu_device_ptr;
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd) { DEBUG_ERROR("PPU_SDLGPU_UploadData: Failed to acquire command buffer."); return; }

    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);

    // VRAM
    SDL_UploadToGPUBuffer(copy_pass, ppu->vram, 0, ppu->vram_buffer_gpu, 0, PPU_VRAM_SIZE);
    // OAM
    SDL_UploadToGPUBuffer(copy_pass, ppu->oam, 0, ppu->oam_buffer_gpu, 0, PPU_OAM_SIZE);
    // Palette
    SDL_UploadToGPUBuffer(copy_pass, ppu->palette, 0, ppu->palette_buffer_gpu, 0, PPU_PALETTE_RAM_SIZE);
    
    // Registers
    PPU_Registers_GPU regs_gpu;
    regs_gpu.ctrl = ppu->ctrl;
    regs_gpu.mask = ppu->mask;
    regs_gpu.vram_addr_v = ppu->vram_addr;
    regs_gpu.temp_addr_t = ppu->temp_addr;
    regs_gpu.fine_x = ppu->fine_x;
    regs_gpu.scanline = ppu->scanline;
    regs_gpu.cycle = ppu->cycle;
    // Populate other fields of regs_gpu as needed

    SDL_UploadToGPUBuffer(copy_pass, &regs_gpu, 0, ppu->registers_buffer_gpu, 0, sizeof(PPU_Registers_GPU));

    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(cmd);
}

void PPU_SDLGPU_StepFrame(PPU* ppu)
{
    if (!ppu || !ppu->gpu_device_ptr || !ppu->compute_pipeline) {
        DEBUG_ERROR("PPU_SDLGPU_StepFrame: PPU not initialized for GPU or pipeline missing.");
        return;
    }
    SDL_GPUDevice* device = ppu->gpu_device_ptr;

    // 1. Upload any changed data (registers, OAM if frequently modified by CPU)
    PPU_SDLGPU_UploadData(ppu); // This can be optimized to only upload what changed

    // 2. Execute Compute Shader
    SDL_GPUCommandBuffer* cmd_buffer = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd_buffer) { DEBUG_ERROR("Failed to acquire command buffer for compute"); return; }

    SDL_GPUComputePass* compute_pass = SDL_BeginGPUComputePass(cmd_buffer, ppu->resource_bindings, SDL_arraysize(ppu->resource_bindings));
    if (compute_pass) {
        SDL_BindGPUComputePipeline(compute_pass, ppu->compute_pipeline);
        
        // Dispatch compute shader
        // Example: Process 256x240 pixels in 8x8 workgroups
        uint32_t group_count_x = (PPU_FRAMEBUFFER_WIDTH + 7) / 8;  // Assuming 8x8 workgroup size in shader
        uint32_t group_count_y = (PPU_FRAMEBUFFER_HEIGHT + 7) / 8; // Adjust to your shader's workgroup size
        uint32_t group_count_z = 1;
        SDL_DispatchCompute(compute_pass, group_count_x, group_count_y, group_count_z);
        
        SDL_EndGPUComputePass(compute_pass);
    } else {
        DEBUG_ERROR("Failed to begin compute pass");
    }
    
    SDL_SubmitGPUCommandBuffer(cmd_buffer);

    // 3. (Optional) Download framebuffer to CPU if needed immediately
    // PPU_SDLGPU_DownloadFramebuffer(ppu); 
    // Typically, you'd use the framebuffer_gpu directly for rendering or copy it to a texture.
}

void PPU_SDLGPU_DownloadFramebuffer(PPU* ppu) {
    if (!ppu || !ppu->gpu_device_ptr || !ppu->framebuffer_gpu || !ppu->framebuffer) {
         DEBUG_ERROR("PPU_SDLGPU_DownloadFramebuffer: Invalid PPU, GPU device, GPU buffer, or CPU framebuffer.");
        return;
    }
    SDL_GPUDevice* device = ppu->gpu_device_ptr;
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
     if (!cmd) { DEBUG_ERROR("PPU_SDLGPU_DownloadFramebuffer: Failed to acquire command buffer."); return; }

    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    SDL_DownloadFromGPUBuffer(copy_pass, ppu->framebuffer_gpu, 0, ppu->framebuffer, 0, PPU_FRAMEBUFFER_WIDTH * PPU_FRAMEBUFFER_HEIGHT * sizeof(uint32_t));
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(cmd);

    // Ensure the copy is complete before CPU tries to use it
    SDL_WaitForGPUIdle(device); 
}
    */