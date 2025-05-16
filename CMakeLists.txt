cmake_minimum_required(VERSION 3.29.6)
project(cNES C)

set(CMAKE_C_STANDARD_REQUIRED 99)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_BINARY_DIR ${PROJECT_SOURCE_DIR}/bin)

include_directories(include)

add_subdirectory(ext)

#cNES
file(GLOB CNES_SOURCES src/cNES/*.c)
file(GLOB SOURCES src/*.c)
add_executable(cNES ${CNES_SOURCES} ${SOURCES})

if (WIN32)
    set(PLATFORM_LIBS mingw32)
    add_definitions()
endif()


target_link_libraries(cNES PRIVATE ${PLATFORM_LIBS} cimgui_with_backend cimplot) #SDL2main)# cglm stb)#vulkan glew gl glu)
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

target_compile_definitions(
	cNES
	PRIVATE
	CIMGUI_USE_SDL3=1
	CIMGUI_USE_SDLGPU3=1
)

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
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/tests
                ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/tests
)

add_dependencies(cNES copy_info copy_tests)