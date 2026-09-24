#ifndef POKEPLATINUM_START_MENU_H
#define POKEPLATINUM_START_MENU_H

#include "field/field_system_decl.h"
#include "overlay005/sprite_resource_manager.h"

#include "bg_window.h"
#include "field_move_tasks.h"
#include "field_task.h"
#include "item_use_functions.h"
#include "list_menu.h"
#include "menu.h"
#include "string_list.h"

#define MAX_START_MENU_OPTIONS         25
#define MAX_VISIBLE_START_MENU_OPTIONS 7

enum StartMenuShortcut {
    START_MENU_SHORTCUT_NONE = 0,
    START_MENU_SHORTCUT_POKEMON,
    START_MENU_SHORTCUT_BAG,
    START_MENU_SHORTCUT_OPTIONS,
    START_MENU_SHORTCUT_MODS,
};

typedef struct StartMenu {
    Window primaryWindow;
    Window secondaryWindow;
    ListMenu *menu;
    StringList *menuOptions;
    Window modsWindow;
    ListMenu *modsMenu;
    StringList *modsMenuOptions;
    u32 selectedMod;
    u32 modOptionsCursor;
    u16 cursorPos;
    u16 listPos;
    u16 state;
    u32 input;
    u8 options[MAX_START_MENU_OPTIONS];
    SpriteResourceManager spriteManager;
    ManagedSprite *sprites[MAX_VISIBLE_START_MENU_OPTIONS + 1];
    u32 spriteCount;
    u32 hideOptionFlags;
    enum StartMenuShortcut shortcut;
    BOOL inUnionRoom;
    FieldTaskFunc callback;
    ItemUseContext itemUseCtx;
    FieldMoveContext fieldMoveContext;
    void *taskData;
    void *additionalTaskContext;
    u16 shortcutFieldMove;
    u16 shortcutMonSlot;
} StartMenu;

enum StartMenuState {
    START_MENU_STATE_INIT = 0,
    START_MENU_STATE_SELECT,
    START_MENU_STATE_APP_START,
    START_MENU_STATE_APP_RUN,
    START_MENU_STATE_SAVE,
    START_MENU_STATE_SAVE_WAIT,
    START_MENU_STATE_EVOLVE_INIT,
    START_MENU_STATE_EVOLVE,
    START_MENU_STATE_8,
    START_MENU_STATE_9,
    START_MENU_STATE_NEW_TASK,
    START_MENU_STATE_EXIT_WITH_NEW_TASK,
    START_MENU_STATE_REINIT,
    START_MENU_STATE_END,
    START_MENU_STATE_REINIT_WAIT_FOR_FADE,
    START_MENU_STATE_SAVED,
    START_MENU_STATE_MODS_INIT,
    START_MENU_STATE_MODS_INPUT,
    START_MENU_STATE_MOD_APP_START,
    START_MENU_STATE_MODS_REINIT,
    START_MENU_STATE_MODS_REINIT_WAIT_FOR_FADE,
};

enum MailUsageType {
    MAIL_GIVE_FROM_PARTY_MENU = 0,
    MAIL_GIVE_FROM_BAG,
    MAIL_READ_FROM_PARTY_MENU,
    MAIL_READ_FROM_BAG,
};

extern const u8 gAllSummaryScreenPages[];

BOOL FieldSystem_IsInValidLocation(FieldSystem *fieldSystem);
void StartMenu_Open(FieldSystem *fieldSystem);
void StartMenu_OpenShortcut(FieldSystem *fieldSystem, enum StartMenuShortcut shortcut);
BOOL StartMenu_UseFieldMoveShortcut(FieldSystem *fieldSystem, enum FieldMoveList move);
void StartMenu_OpenUnionRoom(FieldSystem *fieldSystem);
void StartMenu_OpenColosseum(FieldSystem *fieldSystem);
void StartMenu_OpenFromScript(FieldSystem *fieldSystem);
void StartMenu_SetCallback(StartMenu *menu, void *callback);
BOOL StartMenu_ExitPartyMenu(FieldTask *fieldTask);
BOOL StartMenu_ExitTownMap(FieldTask *fieldTask);
BOOL StartMenu_FlyDestinationSelected(FieldTask *fieldTask);
BOOL StartMenu_ExitJournal(FieldTask *fieldTask);
void *StartMenu_BuildMailData(u16 itemID, u8 usageType, u8 partySlot);
BOOL StartMenu_ExitMail(FieldTask *fieldTask);
BOOL StartMenu_ExitPoffinCase(FieldTask *fieldTask);
BOOL StartMenu_ExitPalPad(FieldTask *fieldTask);
BOOL StartMenu_ExitVsRecorder(FieldTask *fieldTask);

#endif // POKEPLATINUM_START_MENU_H
