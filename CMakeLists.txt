cmake_minimum_required(VERSION 3.5)
project(cNES C)

enable_testing()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

add_subdirectory(ext)

set(CNES_CORE_SOURCES
    src/debug.c
    src/cNES/bus.c
    src/cNES/cpu.c
    src/cNES/debugging.c
    src/cNES/mapper.c
    src/cNES/apu.c
    src/cNES/nes.c
    src/cNES/ppu_accelerated.c
    src/cNES/ppu.c
    src/cNES/rom.c
    src/cNES/scripting/lua_api.c
)

set(CNES_FRONTEND_SOURCES
    src/main.c
    src/profiler.c
    src/frontend/frontend.c
    src/frontend/frontend_profiler.c
    src/frontend/frontend_log.c
    src/frontend/frontend_ppu_viewer.c
    src/frontend/frontend_debugger.c
    src/frontend/frontend_game_screen.c
    src/frontend/frontend_menus.c
    src/frontend/frontend_rominfo.c
    src/frontend/frontend_config.c
    src/frontend/frontend_style.c
    src/frontend/cimgui_markdown.c
    src/frontend/cimgui_hex.c
)

add_library(libcNES SHARED ${CNES_CORE_SOURCES})
target_include_directories(libcNES PUBLIC include)
target_include_directories(libcNES PRIVATE ext/cjson)
set_target_properties(libcNES PROPERTIES PREFIX "")

if (TARGET MINIZIP::minizip)
    set(CNES_MINIZIP_TARGET MINIZIP::minizip)
elseif(TARGET minizip)
    set(CNES_MINIZIP_TARGET minizip)
else()
    message(FATAL_ERROR "minizip target not found")
endif()

target_link_libraries(libcNES PUBLIC cjson ${CNES_MINIZIP_TARGET} stb)

add_executable(cNES ${CNES_FRONTEND_SOURCES})

if (WIN32)
    set(PLATFORM_LIBS mingw32)
    #add_definitions()
endif()

target_link_libraries(cNES PRIVATE ${PLATFORM_LIBS} cimgui cimplot SDL3-shared)
target_link_libraries(cNES PUBLIC libcNES cjson)

# Link Lua/LuaJIT if found
if (USE_LUAJIT)
    if (LUA_LIBS)
        target_link_libraries(libcNES PUBLIC ${LUA_LIBS})
    endif()
    if (LUA_INCLUDE)
        target_include_directories(libcNES PUBLIC ${LUA_INCLUDE})
    endif()
    target_compile_definitions(libcNES PUBLIC USE_LUAJIT=1)
    message(STATUS "Lua scripting ENABLED")
else()
    message(STATUS "Lua scripting DISABLED")
endif()

target_compile_options(libcNES PRIVATE
        $<$<CONFIG:Debug>:
        #-pedantic
        -Wall
        -Wextra
        -Wconversion
        -Wundef
        #-Werror
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-unused-but-set-variable>
)

target_compile_definitions(cNES PRIVATE CIMGUI_USE_SDL3 CIMGUI_USE_SDLGPU3)

add_custom_target(copy_data
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/data/)

add_custom_target(copy_info
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/LICENCE
        ${CMAKE_CURRENT_SOURCE_DIR}/CREDITS
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

add_dependencies(cNES copy_info copy_data)

add_subdirectory(tests)