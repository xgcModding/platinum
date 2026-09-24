#ifndef POKEPLATINUM_LUA_MOD_H
#define POKEPLATINUM_LUA_MOD_H
#include "mods/mod_api.h"
typedef struct LuaMod LuaMod;
LuaMod *LuaMod_Open(const char *path);
const PokePlatinumMod *LuaMod_Descriptor(LuaMod *mod);
int LuaMod_Load(LuaMod *mod, const PokePlatinumModHost *host);
void LuaMod_Dispatch(LuaMod *mod, PokePlatinumModEvent event, void *data);
void LuaMod_Unload(LuaMod *mod);
void LuaMod_Close(LuaMod *mod);
#endif
