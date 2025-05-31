cmake_minimum_required(VERSION 3.5)
project(cNES C)

enable_testing()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_BINARY_DIR ${PROJECT_SOURCE_DIR}/bin)

include_directories(include)

add_subdirectory(ext)

#cNES
add_executable(cNES 
        src/main.c 
        src/debug.c 
        src/profiler.c 
        src/ui/ui.c 
        src/ui/ui_menus.c
        src/ui/ui_style.c
        src/ui/cimgui_markdown.c 
        src/cNES/bus.c 
        src/cNES/cpu.c 
        src/cNES/debugging.c 
        src/cNES/nes.c 
        src/cNES/ppu_accelerated.c 
        src/cNES/ppu.c
        src/cNES/rom.c
)

if (WIN32)
    set(PLATFORM_LIBS mingw32)
    #add_definitions()
endif()


target_link_libraries(cNES PRIVATE ${PLATFORM_LIBS} cimgui cimplot cjson-static minizip-ng) #SDL2main)# cglm stb)#vulkan glew gl glu)
target_link_libraries(cNES PUBLIC SDL3-shared) #SDL2)

target_compile_options(cNES PRIVATE
        $<$<CONFIG:Debug>:
        #-pedantic
        -Wall
        -Wextra
        -Wconversion
        -Wundef
        #-Werror
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        >
)

target_compile_definitions(cNES PUBLIC -DCIMGUI_USE_SDL3 -DCIMGUI_USE_SDLGPU3)

add_custom_target(copy_data
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/data/)

add_custom_target(copy_info
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/LICENCE
        ${CMAKE_CURRENT_SOURCE_DIR}/CREDITS
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
        
add_custom_target(copy_tests
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/tests
                ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/tests
)

add_dependencies(cNES copy_info copy_tests copy_data)

add_subdirectory(tests)