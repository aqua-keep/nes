#ifndef NES_INTERFACE_H
#define NES_INTERFACE_H

#include "stdint.h"

size_t nes_get_size(const char* file);
int nes_read_rom(const char* file, uint8_t* romfile);

#endif //NES_INTERFACE_H
