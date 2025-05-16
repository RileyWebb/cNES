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
include(cimgui/backend_test/cmake/GenerateCimguiBindings.cmake)

set(inclulist ${sdl3_SOURCE_DIR}/include)
GenerateCimguiBindings(cimgui_with_backend sdl3 sdlgpu3 inclulist)
target_link_libraries(cimgui_with_backend PRIVATE SDL3::SDL3)

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