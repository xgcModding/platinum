#ifndef POKEPLATINUM_PM_VERSION_H
#define POKEPLATINUM_PM_VERSION_H

#include "constants/versions.h"

extern const u8 gGameVersion;
#ifdef SDK_PORT
extern u8 gGameLanguage;
void GameLanguage_Init(void);
#undef GAME_LANGUAGE
#define GAME_LANGUAGE gGameLanguage
#else
extern const u8 gGameLanguage;
#endif

#endif // POKEPLATINUM_PM_VERSION_H
