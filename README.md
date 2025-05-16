# cNES

- use sdlgpu
- refactor cpu to use lookup table (not switch statement)
- plugin loading
- ui rework
- mappers
- testing framework
- pipe logs to sdl (
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "GLEW Initialization Error", (const char*)glewGetErrorString(err), window);)