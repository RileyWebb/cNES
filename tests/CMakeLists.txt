include_directories(${PROJECT_SOURCE_DIR}/include) # Add include directory for headers like cNES/nes.h

add_executable(cNES_cpu_tests 
        cpu_tests.c
        ../src/cNES/cpu.c
        ../src/cNES/debugging.c
        ../src/cNES/rom.c
        ../src/cNES/bus.c
        ../src/cNES/nes.c
        ../src/cNES/ppu.c
        ../src/debug.c
)
#add_executable(cNES_ppu_tests ppu_tests.c)
#add_executable(cNES_bus_tests bus_tests.c)

# Use $<TARGET_FILE:...> generator expression for robust path to the test executable
add_test(NAME cNES_cpu_tests COMMAND cNES_cpu_tests)
set_tests_properties(cNES_cpu_tests PROPERTIES WILL_FAIL FALSE)