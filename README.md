# cNES: A NES Emulator

cNES is a Nintendo Entertainment System (NES) emulator written in C. This project aims to provide an accurate and efficient NES emulation experience while serving as an educational resource for understanding the NES architecture.

## Table of Contents
- [Features](#features)
- [Building](#building)
- [Roadmap](#roadmap)
- [Resources](#resources)
- [Contributing](#contributing)
- [License](#license)

## Features

- **CPU Emulation**: 6502 processor emulation using a lookup table for efficient instruction dispatch.
- **PPU Emulation**: GPU-accelerated Picture Processing Unit (PPU) rendering via `SDL_gpu`.
- **UI Enhancements**:
  - Settings menu with save/load functionality.
  - Performance profiler with flame graph visualization.
  - FPS overlay with an optional performance graph.
  - UTF-8 support and icons in the UI.
- **ROM Loading**: Supports iNES ROM format.
- **Debugging Tools**:
  - Disassembler for analyzing ROMs.
  - Logging system with SDL-based error dialogs.

## Building

### Prerequisites
- A C compiler (e.g., GCC, Clang).
- SDL2 library for windowing, input, and audio.
- SDL_gpu library for GPU-accelerated 2D rendering.
- Vulkan SDK (if SDL_gpu is built with Vulkan backend).

### Windows (MinGW)
1. Set up a MinGW environment (e.g., MSYS2 with MinGW-w64).
2. Install SDL2, SDL_gpu, and Vulkan development libraries.
3. Compile the project using the provided Makefile or build scripts.

*(Build instructions for other platforms like Linux/macOS can be added here.)*

## Roadmap

- [ ] **Core Emulation**:
  - Add support for more [Mappers](https://www.nesdev.org/wiki/Mapper) to improve game compatibility.
  - Implement APU (Audio Processing Unit) emulation.
  - Improve PPU cycle accuracy and rendering details.
- [ ] **UI Improvements**:
  - Complete UI rework for better usability.
  - Add input configuration and remapping UI.
  - Implement plugin loading system.
  - Save states and screenshot functionality.
- [ ] **Development Tools**:
  - Develop a comprehensive testing framework.
  - Add cross-platform build system support (e.g., CMake).
  - Set up Continuous Integration (CI) for automated builds and tests.

## Resources

- **NESDev Wiki**: The go-to resource for NES development and emulation.
  - [Mappers](https://www.nesdev.org/wiki/Mapper)
  - [6502 Cycle Times](https://www.nesdev.org/wiki/6502_cycle_times)
- **Test ROMs**: [NES Test ROMs by christopherpow](https://github.com/christopherpow/nes-test-roms/tree/master)
- **SDL Shader Compiler**: [SDL_shadercross](https://github.com/libsdl-org/SDL_shadercross)

## Features

### Mappers
| Mapper ID | Name                | Supported | Notes                                      |
|-----------|---------------------|-----------|--------------------------------------------|
| 0         | NROM                | ✅         | Basic mapper, 16KB/32KB PRG, 8KB CHR.      |
| 1         | MMC1                | ❌         | Planned for future implementation.         |
| 2         | UxROM               | ❌         | Planned for future implementation.         |
| 3         | CNROM               | ❌         | Planned for future implementation.         |
| 4         | MMC3                | ❌         | Planned for future implementation.         |
| 7         | AOROM               | ❌         | Planned for future implementation.         |W

### APU (Audio Processing Unit)
| Feature                | Supported | Notes                                      |
|------------------------|-----------|--------------------------------------------|
| Pulse Channel 1        | ❌         | Not yet implemented.                       |
| Pulse Channel 2        | ❌         | Not yet implemented.                       |
| Triangle Channel       | ❌         | Not yet implemented.                       |
| Noise Channel          | ❌         | Not yet implemented.                       |
| DMC (Delta Modulation) | ❌         | Not yet implemented.                       |

### PPU (Picture Processing Unit)
| Feature                | Supported | Notes                                      |
|------------------------|-----------|--------------------------------------------|
| Background Rendering   | ✅         | Basic background rendering implemented.    |
| Sprite Rendering       | ✅         | Partial implementation, needs refinement. |
| Sprite 0 Hit           | ✅         | Implemented but needs testing.            |
| Vertical/Horizontal Mirroring | ✅   | Fully supported.                          |

## Contributing

Contributions are welcome! If you'd like to contribute, please fork the repository, create a feature branch, and submit a pull request. For major changes, open an issue first to discuss your ideas.

## Todo

- SDL3_NET
- SDL3_AUDIO
- https://github.com/zlib-ng/minizip-ng
- add file loader system
- llvm cpu
- luajit
- add different loaders and mappers
- add sdl3 software renderer support

## License

This project is licensed under the MIT License. See the `LICENSE` file for details.