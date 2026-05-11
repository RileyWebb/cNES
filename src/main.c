#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#define CNES_PATH_SEPARATOR '\\'
#else
#include <sys/stat.h>
#include <sys/types.h>
#define CNES_PATH_SEPARATOR '/'
#endif

#include "debug.h"

#include "frontend/frontend.h"
#include "frontend/frontend_config.h"
#include "cNES/cpu.h"
#include "cNES/ppu.h"
#include "cNES/bus.h"
#include "cNES/nes.h"
#include "cNES/rom.h"
#include "cNES/version.h"
#include "cNES/scripting/lua_api.h"

static void ensure_directory_exists(const char *path)
{
    if (!path || path[0] == '\0') {
        return;
    }

#ifdef _WIN32
    (void)_mkdir(path);
#else
    (void)mkdir(path, 0755);
#endif
}

static void build_log_timestamp(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        return;
    }

    time_t now = time(NULL);
    if (now == (time_t)-1) {
        (void)snprintf(buffer, buffer_size, "unknown_time");
        return;
    }

    struct tm local_tm;
#ifdef _WIN32
    if (localtime_s(&local_tm, &now) != 0) {
        (void)snprintf(buffer, buffer_size, "unknown_time");
        return;
    }
#else
    if (localtime_r(&now, &local_tm) == NULL) {
        (void)snprintf(buffer, buffer_size, "unknown_time");
        return;
    }
#endif

    if (strftime(buffer, buffer_size, "%Y%m%d_%H%M%S", &local_tm) == 0) {
        (void)snprintf(buffer, buffer_size, "unknown_time");
    }
}

// Global ROM path and script path from command-line arguments
static const char* g_rom_path = NULL;
static const char* g_script_path = NULL;
static bool g_headless = false;

int process_args(int argc, char** argv) {
    g_rom_path = NULL;
    g_script_path = NULL;
    g_headless = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: cNES [options] [rom_path]\n");
            printf("Options:\n");
            printf("  -h, --help           Show this help message\n");
            printf("  --headless           Run without initializing the frontend window/GPU\n");
            printf("  --script <path>      Load and run Lua script at startup\n");
            return 0;
        } else if (strcmp(argv[i], "--headless") == 0) {
            g_headless = true;
        } else if (strcmp(argv[i], "--script") == 0) {
            if (i + 1 < argc) {
                g_script_path = argv[++i];
            } else {
                fprintf(stderr, "Error: --script requires a path argument\n");
                return 1;
            }
        } else if (argv[i][0] != '-') {
            // Positional argument (ROM path)
            if (!g_rom_path) {
                g_rom_path = argv[i];
            }
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }
    
    return 0;
}

int main(int argc, char** argv)
{
    const char *config_dir = FrontendConfig_GetDirectory();
    char logs_dir[512];
    char log_path[512];
    char log_timestamp[32];
    FILE* f_log = NULL;

    if (config_dir && config_dir[0] != '\0' &&
        snprintf(logs_dir, sizeof(logs_dir), "%slogs", config_dir) > 0) {
        ensure_directory_exists(logs_dir);
        build_log_timestamp(log_timestamp, sizeof(log_timestamp));
        if (snprintf(log_path, sizeof(log_path), "%s%clog_%s.txt", logs_dir, CNES_PATH_SEPARATOR, log_timestamp) > 0) {
            f_log = fopen(log_path, "w");
        }
    }
    if (!f_log) {
        build_log_timestamp(log_timestamp, sizeof(log_timestamp));
        if (snprintf(log_path, sizeof(log_path), "logs%clog_%s.txt", CNES_PATH_SEPARATOR, log_timestamp) > 0) {
            ensure_directory_exists("logs");
            f_log = fopen(log_path, "w");
        }
    }
    if (!f_log) {
        f_log = fopen("log.txt", "w");
    }

    DEBUG_RegisterBuffer(f_log);

    if (process_args(argc, argv) != 0) {
        exit(1);
    }

    DEBUG_INFO(CNES_VERSION_BUILD_STRING);

    if (!g_headless) {
        Frontend_Init();
    } else {
        DEBUG_INFO("Headless mode enabled");
    }

    NES* nes = NES_Create();

    ROM* rom = ROM_LoadFile(g_rom_path);
    if (!rom) {
        DEBUG_ERROR("Failed to load ROM: %s", g_rom_path ? g_rom_path : "No path provided");
        if (g_rom_path) {
            // If user provided a path and it failed, exit instead of silently falling back
            NES_Destroy(nes);
            return 1;
        }
        // If fallback also failed, continue anyway (will run with no ROM loaded)
    } else {
        if (NES_Load(nes, rom) != 0) {
            DEBUG_ERROR("Failed to load ROM into NES");
        }
    }

    // Initialize Lua scripting
    LuaScript* lua = LuaScript_Create(nes);
    if (lua && g_script_path) {
        if (LuaScript_LoadFile(lua, g_script_path) != 0) {
            DEBUG_ERROR("Failed to load Lua script: %s", LuaScript_GetError(lua));
        } else {
            LuaScript_OnStart(lua);
        }
    }

    if (g_headless) {
        for (;;)
        {
            NES_StepFrame(nes);

            // Call Lua onframe callback once per emulated frame.
            if (lua) {
                LuaScript_OnFrame(lua);

                // Check if Lua requested exit
                if (LuaScript_ShouldExit(lua)) {
                    break;
                }
            }
        }
    } else {
        frontend_paused = false;
        Frontend_StartEmulation(nes);

        for (;;)
        {
            Frontend_Update(nes);

            // Call Lua onframe callback if script is loaded
            if (lua) {
                LuaScript_OnFrame(lua);

                // Check if Lua requested exit
                if (LuaScript_ShouldExit(lua)) {
                    break;
                }
            }
        }

        Frontend_Shutdown();
    }

    int exit_code = 0;
    if (lua) {
        exit_code = LuaScript_GetExitCode(lua);
    }

    // Cleanup
    if (lua) {
        LuaScript_Destroy(lua);
    }

    NES_Destroy(nes);

    DEBUG_INFO("Closing cNES");
    fclose(f_log);

    return exit_code;
}
