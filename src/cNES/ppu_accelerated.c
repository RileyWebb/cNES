/*
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_iostream.h> // For SDL_RWFromFile
#include <stdio.h> // For printf, if debugging
#include "cNES/ppu.h"
#include "cNES/nes.h" // For NES struct if needed for settings
#include "debug.h"    // For UI_Log or similar

typedef struct PPU_Accel {
    SDL_GPUDevice* gpu_device_ptr; // Pointer to the SDL GPU device
    SDL_GPUBuffer* vram_buffer_gpu; // VRAM buffer for compute shader
    SDL_GPUBuffer* oam_buffer_gpu;  // OAM buffer for compute shader
    SDL_GPUBuffer* palette_buffer_gpu; // Palette RAM buffer for compute shader
    SDL_GPUBuffer* registers_buffer_gpu; // PPU registers buffer for compute shader
    SDL_GPUBuffer* framebuffer_gpu; // Framebuffer buffer for compute shader
    SDL_GPUShader* compute_shader; // Compute shader for PPU processing
    SDL_GPUComputePipeline* compute_pipeline; // Compute pipeline for PPU processing
    SDL_GPUResourceBinding resource_bindings[5]; // Resource bindings for the shader (0-4)
} PPU_Accel;

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


PPU_Accel* PPU_SDLGPU_Init()
{
    PPU_Accel *ppu_accel = malloc(sizeof(PPU_Accel));
    if (!ppu_accel) {
        DEBUG_ERROR("PPU_SDLGPU_Init: Failed to allocate memory for PPU_Accel.");
        return NULL;
    }
    memset(ppu_accel, 0, sizeof(PPU_Accel));

    const SDL_GPUShaderFormat required_format = SDL_GPU_SHADERFORMAT_SPIRV;
    if (!SDL_GPU_IsShaderFormatSupported(required_format)) {
        DEBUG_ERROR("PPU_SDLGPU_Init: Required shader format %d is not supported.", required_format);
        free(ppu_accel);
        return NULL;
    }

    SDL_GPUDevice* device = SDL_CreateGPUDevice(required_format, 
#ifdef DEBUG
        true,
#else
        false,
#endif
        NULL);

    if (!device) {
        DEBUG_ERROR("PPU_SDLGPU_Init: Failed to create SDL_GPUDevice.");
        free(ppu_accel);
        return NULL;
    }
    ppu_accel->gpu_device_ptr = device;

    // 1. Create GPU Buffers
    SDL_GPUBufferCreateInfo buffer_info = {0};
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_INDIRECT | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    buffer_info.size = PPU_VRAM_SIZE;
    ppu_accel->vram_buffer_gpu = SDL_CreateGPUBuffer(device, &buffer_info);
    if (!ppu_accel->vram_buffer_gpu) { DEBUG_ERROR("Failed to create VRAM GPU buffer"); goto cleanup; }

    buffer_info.size = PPU_OAM_SIZE;
    ppu_accel->oam_buffer_gpu = SDL_CreateGPUBuffer(device, &buffer_info);
    if (!ppu_accel->oam_buffer_gpu) { DEBUG_ERROR("Failed to create OAM GPU buffer"); goto cleanup; }

    buffer_info.size = PPU_PALETTE_RAM_SIZE;
    ppu_accel->palette_buffer_gpu = SDL_CreateGPUBuffer(device, &buffer_info);
    if (!ppu_accel->palette_buffer_gpu) { DEBUG_ERROR("Failed to create Palette RAM GPU buffer"); goto cleanup; }

    buffer_info.usage = SDL_GPU_BUFFERUSAGE_INDIRECT | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE; // Registers are read by GPU
    buffer_info.size = sizeof(PPU_Registers_GPU); // Buffer for PPU registers
    ppu_accel->registers_buffer_gpu = SDL_CreateGPUBuffer(device, &buffer_info);
    if (!ppu_accel->registers_buffer_gpu) { DEBUG_ERROR("Failed to create Registers GPU buffer"); goto cleanup; }

    buffer_info.usage = SDL_GPU_BUFFERUSAGE_INDIRECT | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE; // Framebuffer is written by GPU
    buffer_info.size = PPU_FRAMEBUFFER_WIDTH * PPU_FRAMEBUFFER_HEIGHT * sizeof(uint32_t);
    ppu_accel->framebuffer_gpu = SDL_CreateGPUBuffer(device, &buffer_info);
    if (!ppu_accel->framebuffer_gpu) { DEBUG_ERROR("Failed to create Framebuffer GPU buffer"); goto cleanup; }

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
    shader_create_info.stage = SDL_GPU_SHADERSTAGE_COMPUTE;

    ppu_accel->compute_shader = SDL_CreateGPUShader(device, &shader_create_info);
    SDL_free(shader_bytecode); // Bytecode is copied by SDL_CreateGPUShader
    if (!ppu_accel->compute_shader) { DEBUG_ERROR("Failed to create compute shader"); goto cleanup; }

    // 3. Create Compute Pipeline
    SDL_GPUComputePipelineCreateInfo pipeline_info = {0};
    pipeline_info.code = ppu_accel->compute_shader;
    pipeline_info.read_only_storage_texture_count = 0; 
    pipeline_info.read_only_storage_buffer_count = 3;  // VRAM, OAM, Palette (as read-only from shader perspective)
    pipeline_info.read_write_storage_texture_count = 0;
    pipeline_info.read_write_storage_buffer_count = 1; // Framebuffer (as read-write)
    pipeline_info.uniform_buffer_count = 1;            // Registers
    pipeline_info.sampler_count = 0;
    pipeline_info.thread_group_size_x = 8; // Example, match your shader
    pipeline_info.thread_group_size_y = 8; // Example
    pipeline_info.thread_group_size_z = 1; // Example

    ppu_accel->compute_pipeline = SDL_CreateGPUComputePipeline(device, &pipeline_info);
    if (!ppu_accel->compute_pipeline) { DEBUG_ERROR("Failed to create compute pipeline"); goto cleanup; }
    
    // 4. Setup Resource Bindings (example, indices must match shader)
    // Binding 0: Registers (Uniform Buffer)
    ppu_accel->resource_bindings[0].type = SDL_GPU_SHADERRESOURCE_UNIFORM_BUFFER;
    ppu_accel->resource_bindings[0].resource.uniform_buffer_binding.buffer = ppu_accel->registers_buffer_gpu;
    ppu_accel->resource_bindings[0].resource.uniform_buffer_binding.offset = 0;
    ppu_accel->resource_bindings[0].resource.uniform_buffer_binding.size = sizeof(PPU_Registers_GPU);
    
    // Binding 1: VRAM (Storage Buffer - ReadOnly)
    ppu_accel->resource_bindings[1].type = SDL_GPU_SHADERRESOURCE_STORAGE_BUFFER; // Or SDL_GPU_SHADERRESOURCE_READONLY_STORAGE_BUFFER if API distinguishes
    ppu_accel->resource_bindings[1].resource.storage_buffer_binding.buffer = ppu_accel->vram_buffer_gpu;
    
    // Binding 2: OAM (Storage Buffer - ReadOnly)
    ppu_accel->resource_bindings[2].type = SDL_GPU_SHADERRESOURCE_STORAGE_BUFFER;
    ppu_accel->resource_bindings[2].resource.storage_buffer_binding.buffer = ppu_accel->oam_buffer_gpu;
    
    // Binding 3: Palette RAM (Storage Buffer - ReadOnly)
    ppu_accel->resource_bindings[3].type = SDL_GPU_SHADERRESOURCE_STORAGE_BUFFER;
    ppu_accel->resource_bindings[3].resource.storage_buffer_binding.buffer = ppu_accel->palette_buffer_gpu;
    
    // Binding 4: Framebuffer (Storage Buffer - ReadWrite)
    ppu_accel->resource_bindings[4].type = SDL_GPU_SHADERRESOURCE_STORAGE_BUFFER;
    ppu_accel->resource_bindings[4].resource.storage_buffer_binding.buffer = ppu_accel->framebuffer_gpu;

    DEBUG_INFO("PPU_SDLGPU_Init successful.");
    return ppu_accel;

cleanup:
    DEBUG_ERROR("PPU_SDLGPU_Init failed. Cleaning up partially initialized resources.");
    PPU_SDLGPU_Shutdown(ppu_accel); // Reuse shutdown to clean up
    return NULL;
}

void PPU_SDLGPU_Shutdown(PPU_Accel* ppu_accel)
{
    if (!ppu_accel) return;

    // Device pointer might be null if cleanup was called early in Init
    SDL_GPUDevice* device = ppu_accel->gpu_device_ptr;
    if (!device) { // If device was never created or already released
        free(ppu_accel);
        return;
    }


    if (ppu_accel->compute_pipeline) { SDL_ReleaseGPUComputePipeline(device, ppu_accel->compute_pipeline); ppu_accel->compute_pipeline = NULL; }
    if (ppu_accel->compute_shader) { SDL_ReleaseGPUShader(device, ppu_accel->compute_shader); ppu_accel->compute_shader = NULL; }
    
    if (ppu_accel->framebuffer_gpu) { SDL_ReleaseGPUBuffer(device, ppu_accel->framebuffer_gpu); ppu_accel->framebuffer_gpu = NULL; }
    if (ppu_accel->registers_buffer_gpu) { SDL_ReleaseGPUBuffer(device, ppu_accel->registers_buffer_gpu); ppu_accel->registers_buffer_gpu = NULL; }
    if (ppu_accel->palette_buffer_gpu) { SDL_ReleaseGPUBuffer(device, ppu_accel->palette_buffer_gpu); ppu_accel->palette_buffer_gpu = NULL; }
    if (ppu_accel->oam_buffer_gpu) { SDL_ReleaseGPUBuffer(device, ppu_accel->oam_buffer_gpu); ppu_accel->oam_buffer_gpu = NULL; }
    if (ppu_accel->vram_buffer_gpu) { SDL_ReleaseGPUBuffer(device, ppu_accel->vram_buffer_gpu); ppu_accel->vram_buffer_gpu = NULL; }
    
    SDL_ReleaseGPUDevice(device); // Release the device created in Init
    ppu_accel->gpu_device_ptr = NULL;
    
    free(ppu_accel);
    DEBUG_INFO("PPU_SDLGPU_Shutdown complete.");
}

void PPU_SDLGPU_UploadData(PPU_Accel* ppu_accel, PPU* cpu_ppu) {
    if (!ppu_accel || !ppu_accel->gpu_device_ptr || !cpu_ppu) {
        DEBUG_ERROR("PPU_SDLGPU_UploadData: Invalid ppu_accel, GPU device, or cpu_ppu.");
        return;
    }
    SDL_GPUDevice* device = ppu_accel->gpu_device_ptr;
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd) { DEBUG_ERROR("PPU_SDLGPU_UploadData: Failed to acquire command buffer."); return; }

    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);

    // VRAM
    SDL_UploadToGPUBuffer(copy_pass, cpu_ppu->vram, 0, ppu_accel->vram_buffer_gpu, 0, PPU_VRAM_SIZE);
    // OAM
    SDL_UploadToGPUBuffer(copy_pass, cpu_ppu->oam, 0, ppu_accel->oam_buffer_gpu, 0, PPU_OAM_SIZE);
    // Palette
    SDL_UploadToGPUBuffer(copy_pass, cpu_ppu->palette, 0, ppu_accel->palette_buffer_gpu, 0, PPU_PALETTE_RAM_SIZE);
    
    // Registers
    PPU_Registers_GPU regs_gpu;
    regs_gpu.ctrl = cpu_ppu->ctrl;
    regs_gpu.mask = cpu_ppu->mask;
    regs_gpu.vram_addr_v = cpu_ppu->vram_addr;
    regs_gpu.temp_addr_t = cpu_ppu->temp_addr;
    regs_gpu.fine_x = cpu_ppu->fine_x;
    regs_gpu.scanline = cpu_ppu->scanline;
    regs_gpu.cycle = cpu_ppu->cycle;
    // Populate other fields of regs_gpu as needed

    SDL_UploadToGPUBuffer(copy_pass, &regs_gpu, 0, ppu_accel->registers_buffer_gpu, 0, sizeof(PPU_Registers_GPU));

    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(cmd);
}

void PPU_SDLGPU_StepFrame(PPU_Accel* ppu_accel, PPU* cpu_ppu)
{
    if (!ppu_accel || !ppu_accel->gpu_device_ptr || !ppu_accel->compute_pipeline || !cpu_ppu) {
        DEBUG_ERROR("PPU_SDLGPU_StepFrame: PPU not initialized for GPU, pipeline missing, or cpu_ppu missing.");
        return;
    }
    SDL_GPUDevice* device = ppu_accel->gpu_device_ptr;

    // 1. Upload any changed data (registers, OAM if frequently modified by CPU)
    PPU_SDLGPU_UploadData(ppu_accel, cpu_ppu); // This can be optimized to only upload what changed

    // 2. Execute Compute Shader
    SDL_GPUCommandBuffer* cmd_buffer = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd_buffer) { DEBUG_ERROR("Failed to acquire command buffer for compute"); return; }

    SDL_GPUComputePass* compute_pass = SDL_BeginGPUComputePass(cmd_buffer, ppu_accel->resource_bindings, SDL_arraysize(ppu_accel->resource_bindings));
    if (compute_pass) {
        SDL_BindGPUComputePipeline(compute_pass, ppu_accel->compute_pipeline);
        
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

void PPU_SDLGPU_DownloadFramebuffer(PPU_Accel* ppu_accel, PPU* cpu_ppu) {
    if (!ppu_accel || !ppu_accel->gpu_device_ptr || !ppu_accel->framebuffer_gpu || !cpu_ppu || !cpu_ppu->framebuffer) {
         DEBUG_ERROR("PPU_SDLGPU_DownloadFramebuffer: Invalid PPU_Accel, GPU device, GPU buffer, CPU PPU, or CPU framebuffer.");
        return;
    }
    SDL_GPUDevice* device = ppu_accel->gpu_device_ptr;
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
     if (!cmd) { DEBUG_ERROR("PPU_SDLGPU_DownloadFramebuffer: Failed to acquire command buffer."); return; }

    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    SDL_DownloadFromGPUBuffer(copy_pass, ppu_accel->framebuffer_gpu, 0, cpu_ppu->framebuffer, 0, PPU_FRAMEBUFFER_WIDTH * PPU_FRAMEBUFFER_HEIGHT * sizeof(uint32_t));
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(cmd);

    // Ensure the copy is complete before CPU tries to use it
    SDL_WaitForGPUIdle(device); 
}
    */