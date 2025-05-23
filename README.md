# cNES

## Builiding

### Windows (MinGW)
https://github.com/ScriptTiger/LuaJIT-For-Windows

- use sdlgpu
- refactor cpu to use lookup table (not switch statement)
- plugin loading
- ui rework
- mappers
- testing framework
- pipe logs to sdl (
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "GLEW Initialization Error", (const char*)glewGetErrorString(err), window);)

- GPU BASED PPU RENDERING
- SETTINGS MENU AND SAVE SETTINGS
- Performance profiler (flame graph)
- refactor immarkdown
- add utf8 support and icons
- Fix dissasembler
- profiler settings menu and fps overlay (with graph option)