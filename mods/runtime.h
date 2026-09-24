#ifndef POKEPLATINUM_MODS_RUNTIME_H
#define POKEPLATINUM_MODS_RUNTIME_H

#include "mods/api.h"
#include "string_gf.h"

void Mods_Init(void);
void Mods_Dispatch(PokePlatinumModEvent event, void *eventData);
void Mods_Shutdown(void);
uint32_t Mods_GetLoadedCount(void);
int Mods_GetLoadedInfo(uint32_t index, PokePlatinumModInfo *info);
String *Mods_StringFromUtf8(const char *text, u32 heapID);
uint32_t Mods_GetStartMenuItemCount(void);
const char *Mods_GetStartMenuItemLabel(uint32_t index);
PokePlatinumModMenuResult Mods_InvokeStartMenuItem(uint32_t index, void *gameContext);
uint32_t Mods_GetOptionCount(uint32_t modIndex);
const char *Mods_GetOptionLabel(uint32_t modIndex, uint32_t optionIndex);
uint32_t Mods_GetOptionValueCount(uint32_t modIndex, uint32_t optionIndex);
const char *Mods_GetOptionValueLabel(uint32_t modIndex, uint32_t optionIndex, uint32_t valueIndex);
uint32_t Mods_GetOptionSelectedValue(uint32_t modIndex, uint32_t optionIndex);
int Mods_IsOptionReadOnly(uint32_t modIndex, uint32_t optionIndex);
int Mods_IsOptionAction(uint32_t modIndex, uint32_t optionIndex);
int Mods_SetOptionSelectedValue(uint32_t modIndex, uint32_t optionIndex, uint32_t valueIndex);
void Mods_NotifySaveLoaded(void *saveData);
void Mods_NotifyKeyDown(int32_t keycode, uint8_t repeat);
uint32_t Mods_FilterExperienceGain(void *pokemon, uint32_t currentExperience, uint32_t requestedExperience);
int Mods_IsLevelUpItemBlocked(void *pokemon, uint16_t itemId);

#endif // POKEPLATINUM_MODS_RUNTIME_H
