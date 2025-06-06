bool Loader_iNES_Identify(const uint8_t *data, size_t size) {
    if (!data || size < 4) {
        return false;
    }
    // Check for "NES\x1A" magic number
    return (data[0] == 'N' && data[1] == 'E' && data[2] == 'S' && data[3] == 0x1A);
}