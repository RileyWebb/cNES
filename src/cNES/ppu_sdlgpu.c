#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_properties.h>

static SDL_GPUDevice *compute_device;

void PPU_SDLGPU_Init(SDL_GPUDevice* gpu_device) 
{
    compute_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV,
        #ifdef DEBUG
                                             true,
        #else
                                             false,
        #endif
                                             NULL);
}