#ifndef CNES_LOADER_H
#define CNES_LOADER_H

typedef enum Loader_Type {
    LOADER_TYPE_INES,  // iNES format
    LOADER_TYPE_UNIF,  // UNIF format
    LOADER_TYPE_FDS,   // Famicom Disk System format
    LOADER_TYPE_NSF,   // NES Sound Format
    LOADER_TYPE_NES,   // Generic NES format
    LOADER_TYPE_UNKNOWN // Unknown or unsupported format
} Loader_Type;

#endif // CNES_LOADER_H