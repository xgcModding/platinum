#ifndef POKEPLATINUM_ROM_ASSETS_H
#define POKEPLATINUM_ROM_ASSETS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
int RomAssets_Read(const char *path, void *destination, size_t size);
#ifdef __cplusplus
}
#endif

#endif
