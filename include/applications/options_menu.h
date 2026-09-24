#ifndef POKEPLATINUM_APPLICATIONS_OPTIONS_MENU_H
#define POKEPLATINUM_APPLICATIONS_OPTIONS_MENU_H

#include "game_options.h"
#include "overlay_manager.h"

typedef struct OptionsMenu {
    Options *options;
    u32 modIndex;
    u32 cursor;
    BOOL isModMenu;
} OptionsMenu;

BOOL OptionsMenu_Init(ApplicationManager *appMan, int *state);
BOOL OptionsMenu_Exit(ApplicationManager *appMan, int *state);
BOOL OptionsMenu_Main(ApplicationManager *appMan, int *state);

#endif // POKEPLATINUM_APPLICATIONS_OPTIONS_MENU_H
