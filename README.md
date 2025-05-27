# cNES

## Builiding

### Windows (MinGW)
https://github.com/ScriptTiger/LuaJIT-For-Windows
vulkan sdl

- use sdlgpu
- refactor cpu to use lookup table (not switch statement)
- plugin loading
- ui rework
- mappers https://www.nesdev.org/wiki/Mapper
- testing framework
- pipe logs to sdl (
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "GLEW Initialization Error", (const char*)glewGetErrorString(err), window);)

- GPU BASED PPU RENDERING
- SETTINGS MENU AND SAVE SETTINGS
- Performance profiler (flame graph)
- refactor immarkdown
- add utf8 support and icons
- Fix dissasembler
https://github.com/libsdl-org/SDL_shadercross
- profiler settings menu and fps overlay (with graph option)
https://github.com/christopherpow/nes-test-roms/tree/master


https://www.nesdev.org/wiki/6502_cycle_times

These are the tables the test uses. Since it passes on a NES, the values
are pretty much guaranteed correct.

No page crossing:

	0 1 2 3 4 5 6 7 8 9 A B C D E F
	--------------------------------
	7,6,0,8,3,3,5,5,3,2,2,2,4,4,6,6 | 0
	0,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7 | 1
	6,6,0,8,3,3,5,5,4,2,2,2,4,4,6,6 | 2
	0,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7 | 3
	6,6,0,8,3,3,5,5,3,2,2,2,3,4,6,6 | 4
	0,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7 | 5
	6,6,0,8,3,3,5,5,4,2,2,2,5,4,6,6 | 6
	0,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7 | 7
	2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4 | 8
	0,6,0,6,4,4,4,4,2,5,2,5,5,5,5,5 | 9
	2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4 | A
	0,5,0,5,4,4,4,4,2,4,2,4,4,4,4,4 | B
	2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6 | C
	0,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7 | D
	2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6 | E
	0,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7 | F
	
Page crossing:

	0 1 2 3 4 5 6 7 8 9 A B C D E F
	--------------------------------
	7,6,0,8,3,3,5,5,3,2,2,2,4,4,6,6 | 0
	0,6,0,8,4,4,6,6,2,5,2,7,5,5,7,7 | 1
	6,6,0,8,3,3,5,5,4,2,2,2,4,4,6,6 | 2
	0,6,0,8,4,4,6,6,2,5,2,7,5,5,7,7 | 3
	6,6,0,8,3,3,5,5,3,2,2,2,3,4,6,6 | 4
	0,6,0,8,4,4,6,6,2,5,2,7,5,5,7,7 | 5
	6,6,0,8,3,3,5,5,4,2,2,2,5,4,6,6 | 6
	0,6,0,8,4,4,6,6,2,5,2,7,5,5,7,7 | 7
	2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4 | 8
	0,6,0,6,4,4,4,4,2,5,2,5,5,5,5,5 | 9
	2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4 | A
	0,6,0,6,4,4,4,4,2,5,2,5,5,5,5,5 | B
	2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6 | C
	0,6,0,8,4,4,6,6,2,5,2,7,5,5,7,7 | D
	2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6 | E
	0,6,0,8,4,4,6,6,2,5,2,7,5,5,7,7 | F