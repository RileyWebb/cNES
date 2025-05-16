add_subdirectory(sdl EXCLUDE_FROM_ALL)
add_subdirectory(glew EXCLUDE_FROM_ALL)
#add_subdirectory(libuv EXCLUDE_FROM_ALL)
#add_subdirectory(zip EXCLUDE_FROM_ALL)
#add_subdirectory(protobuf)
#add_subdirectory(protobuf-c/build-cmake EXCLUDE_FROM_ALL)

#STB
#file(GLOB STB_SOURCES stb/*.h)
#add_library(stb INTERFACE ${STB_SOURCES})
#target_include_directories(stb INTERFACE stb)

# CIMGUI
project(cimgui LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CIMGUI_LIBRARIES)

add_library(cimgui STATIC
        cimgui/cimgui.cpp
        cimgui/cimgui.h
        cimgui/imgui/imgui.cpp
        cimgui/imgui/imgui.h
        cimgui/imgui/imgui_widgets.cpp
        cimgui/imgui/imgui_draw.cpp
        cimgui/imgui/imgui_tables.cpp
        cimgui/imgui/imgui_demo.cpp
        cimgui/imgui/backends/imgui_impl_opengl3.cpp
        cimgui/imgui/backends/imgui_impl_sdl3.cpp)
target_include_directories(cimgui INTERFACE cimgui)
target_include_directories(cimgui PRIVATE cimgui/imgui)
target_include_directories(cimgui INTERFACE cimgui/generator/output/)
target_compile_definitions(cimgui PUBLIC -DCIMGUI_USE_OPENGL3 -DCIMGUI_USE_SDL3)

set(IMGUI_LIBRARIES )

if (WIN32)
    target_compile_definitions(cimgui PUBLIC IMGUI_IMPL_API=extern\t\"C\"\t__declspec\(dllexport\))
    list(APPEND CIMGUI_LIBRARIES imm32)
else (WIN32)
    target_compile_definitions(cimgui PUBLIC IMGUI_IMPL_API=extern\t\"C\"\t)
endif (WIN32)

target_link_libraries(cimgui PUBLIC SDL3-shared ${CIMGUI_LIBRARIES} stdc++ m)

#CIMPLOT
project(cimplot LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(cimplot STATIC
        cimplot/cimplot.cpp
        cimplot/implot/implot.cpp
        cimplot/implot/implot_demo.cpp
        cimplot/implot/implot_items.cpp)
target_include_directories(cimplot INTERFACE cimplot)
target_include_directories(cimplot PUBLIC cimgui)
target_include_directories(cimplot PRIVATE cimgui/imgui)

if (WIN32)
    target_compile_definitions(cimplot PUBLIC IMGUI_IMPL_API=extern\t\"C\"\t__declspec\(dllexport\))
    #list(APPEND CIMGUI_LIBRARIES imm32)
else (WIN32)
    target_compile_definitions(cimplot PUBLIC IMGUI_IMPL_API=extern\t\"C\"\t)
endif (WIN32)

target_link_libraries(cimplot PUBLIC cimgui)