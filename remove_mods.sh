#!/bin/bash
sed -i '/START_MENU_OPTION_MOD_FIRST = 128,/d' src/start_menu.c
sed -i '/if (menu->options\[i\] >= START_MENU_OPTION_MOD_FIRST) {/,/}/d' src/start_menu.c
sed -i '/for (u32 i = 0; i < Mods_GetStartMenuItemCount(); i++) {/,/}/d' src/start_menu.c
sed -i '/static charcode_t Mods_CodepointToChar(u32 value)/,/static void StartMenu_PrintBallCount(FieldTask \*fieldTask)/ {
    /static void StartMenu_PrintBallCount(FieldTask \*fieldTask)/!d
}' src/start_menu.c
sed -i '/if (menu->input >= START_MENU_OPTION_MOD_FIRST) {/,/^\s*} else if/ {
    /^\s*} else if/!d
    s/} else if (menu->input == START_MENU_OPTION_EXIT) {/if (menu->input == START_MENU_OPTION_EXIT) {/
}' src/start_menu.c
sed -i '/if (option >= START_MENU_OPTION_MOD_FIRST) {/,/return option;/ {
    /return option;/!d
}' src/start_menu.c
sed -i '/int resourceID = option == START_MENU_OPTION_MOD_FIRST/,/FONT_SYSTEM, 12, 0, 1, 15, 2, NULL);/ {
    /FONT_SYSTEM, 12, 0, 1, 15, 2, NULL);/!d
}' src/start_menu.c
