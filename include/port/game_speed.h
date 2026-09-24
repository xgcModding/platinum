#ifndef POKEPLATINUM_GAME_SPEED_H
#define POKEPLATINUM_GAME_SPEED_H
#include <nitro/types.h>
#include "overlay_manager.h"
void GameSpeed_ApplySettings(void);
void GameSpeed_SetFixed30Hz(BOOL enabled);
BOOL GameSpeed_IsFixed30Hz(void);
void GameSpeed_DeleteObject(void *mapObj);
int GameSpeed_ScaleMovementDuration(int frames, u32 *remainder);
void GameSpeed_ScaleWalk(void *mapObj, fx32 *distance, s16 *duration);
int GameSpeed_BeginDraw(void *mapObj);
void GameSpeed_EndDraw(int previous);
fx32 GameSpeed_AnimationFrames(fx32 frames);
unsigned int GameSpeed_BattleUnits(void);
BOOL GameSpeed_TickSound(void);
void GameSpeed_MenuOpened(void *battleSys);
void GameSpeed_CommandCleared(void *battleSys, int command);
void GameSpeed_Application(ApplicationManager *appMan, BOOL finished);
#endif
