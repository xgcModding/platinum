#ifndef POKEPLATINUM_GAME_SHORTCUTS_H
#define POKEPLATINUM_GAME_SHORTCUTS_H

#include <stdint.h>

void GameShortcuts_Init(void);
void GameShortcuts_Update(void);
void GameShortcuts_NotifyKeyDown(int32_t keycode, uint8_t repeat);
void PoketchKeyboard_QueuePage(int page);
void PoketchKeyboard_Update(void);
void GameShortcuts_SetMapOpen(int open);
int GameShortcuts_TakeMapCloseRequest(void);
void GameRestart_Request(void);
void GameRestart_Update(void);

#endif
