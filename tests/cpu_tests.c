#include "cNES/cpu.h"
#include "cNES/nes.h"
#include "cNES/bus.h"
#include "cNES/rom.h" // Added for ROM loading

#include "nestest.h" // Assumed to provide nestest_rom and nestest_rom_len

#include <stdio.h> // Added for printf/fprintf

int main() 
{
    // Create NES instance
    NES *nes = NES_Create();
    if (!nes) {
        fprintf(stderr, "Failed to create NES\n");
        return 1;
    }

    // Create CPU instance
    CPU *cpu = CPU_Create(nes);
    if (!cpu) 
    {
        fprintf(stderr, "Failed to create CPU\n");
        NES_Destroy(nes);
        return 1;
    }

    // Load nestest ROM
    // Assumes nestest.h provides:
    // extern unsigned char nestest_rom[];
    // extern unsigned int nestest_rom_len;
    ROM* rom_data = ROM_LoadMemory(rom_nestest, nestest_nes_len);
    if (!rom_data) {
        fprintf(stderr, "Failed to load nestest ROM data\n");
        CPU_Destroy(cpu);
        NES_Destroy(nes);
        return 1;
    }
    
    if (NES_Load(nes, rom_data)) { 
        fprintf(stderr, "Failed to load ROM into NES\n");
        // if ROM_LoadFromMemory allocates rom_data, it should be freed here, e.g., ROM_Destroy(rom_data);
        CPU_Destroy(cpu);
        NES_Destroy(nes);
        return 1;
    }

    // Set initial CPU state for nestest
    cpu->a = 0x00;
    cpu->x = 0x00;
    cpu->y = 0x00;
    cpu->pc = 0xC000;
    cpu->sp = 0xFD;
    // Set status register P to $24 (IRQ disabled, Unused flag set)
    // This might be `cpu->P = 0x24;` if P is a byte, or individual flags:
    // cpu->P.I = 1; cpu->P.U = 1; (other flags 0)
    // Or using a helper function if available:
    CPU_SetFlag(cpu, 0x24, 1);


    // Main loop for nestest execution
    int step_result;
    // Run for a number of instructions sufficient for nestest to complete.
    // Nestest writes results to $02/$03.
    const int max_instructions = 30000; 
    for (int i = 0; i < max_instructions; ++i) {
        step_result = CPU_Step(cpu);
        if (step_result < 0) { // Assuming CPU_Step returns < 0 on error
            fprintf(stderr, "CPU_Step failed at instruction %d (PC: %04X), error code: %d\n", i, cpu->pc, step_result);
            break;
        }
        // Optional: check for specific PC if a known end-of-test instruction address exists
        // and you want to stop early, e.g. if (cpu->PC == 0xC6C2) break;
    }

    // Check nestest result from NES RAM ($0000-$07FF)
    // Values at $02 and $03 indicate success (00, 00) or failure codes.
    unsigned char res_02 = BUS_Read(nes, 0x02);
    unsigned char res_03 = BUS_Read(nes, 0x03);

    int final_test_outcome = 1; // Default to failure

    if (res_02 == 0x00 && res_03 == 0x00) {
        printf("Nestest Succeeded.\n");
        final_test_outcome = 0; // Success
    } else {
        printf("Nestest Failed: $0002 = %02X, $0003 = %02X\n", res_02, res_03);
    }
    
    // Cleanup
    CPU_Destroy(cpu);
    // if ROM_LoadFromMemory allocated rom_data, it should be freed here, e.g., ROM_Destroy(rom_data);
    NES_Destroy(nes);

    return final_test_outcome;
}