#include <stdint.h>
#include <string.h>
#include <SDL2/SDL_keycode.h>

#include "constants/field_base_tiles.h"
#include "constants/field/map_load.h"
#include "underground/manager.h"
#include "constants/heap.h"
#include "generated/items.h"
#include "constants/narc.h"
#include "generated/text_banks.h"
#include "field/field_system.h"
#include "bg_window.h"
#include "field_message.h"
#include "message.h"
#include "message_util.h"
#include "overlay005/save_info_window.h"
#include "overlay005/fishing.h"
#include "render_window.h"
#include "save_player.h"
#include "sound_playback.h"
#include "string_gf.h"
#include "string_template.h"
#include "res/text/bank/common_strings.h"
#include "comm_manager.h"
#include "field_task.h"
#include "start_menu.h"
#include "item_use_functions.h"
#include "port/debug_field.h"
#include "port/game_shortcuts.h"
#include "port/camera_distance.h"
#include "simulator/sim.h"

#define RESULT_MESSAGE_FRAMES 30

typedef struct QuickSaveUi {
    SaveInfoWindow *saveInfo;
    Window messageWindow;
    String *message;
    void *waitDial;
    uint8_t printerId;
    int state;
    int delay;
} QuickSaveUi;

static QuickSaveUi sUi;
static int sSaveRequested;
static int sSaveActive;
static int sMapRequested;
static int sItemRequested;
static int sFieldMoveRequested;

enum { SHORTCUT_BICYCLE = 1, SHORTCUT_FISHING, SHORTCUT_EXPLORER_KIT };
static int sRadarRequested;
static int sMapOpen;
static int sMapCloseRequested;

void GameShortcuts_NotifyKeyDown(int32_t keycode, uint8_t repeat)
{
    if (repeat || keycode == SDLK_UNKNOWN) {
        return;
    }
    const SIM_config_type *config = SIM_GetConfigPtr();
    if (keycode == config->defogKey) { __atomic_store_n(&sFieldMoveRequested, FIELD_MOVE_DEFOG + 1, __ATOMIC_RELEASE); return; }
    if (keycode == config->flyKey) { __atomic_store_n(&sFieldMoveRequested, FIELD_MOVE_FLY + 1, __ATOMIC_RELEASE); return; }
    if (keycode == config->teleportKey) { __atomic_store_n(&sFieldMoveRequested, FIELD_MOVE_TELEPORT + 1, __ATOMIC_RELEASE); return; }
    if (keycode == config->surfKey) { __atomic_store_n(&sFieldMoveRequested, FIELD_MOVE_SURF + 1, __ATOMIC_RELEASE); return; }
    if (keycode == config->bicycleKey) { __atomic_store_n(&sItemRequested, SHORTCUT_BICYCLE, __ATOMIC_RELEASE); return; }
    if (keycode == config->fishingKey) { __atomic_store_n(&sItemRequested, SHORTCUT_FISHING, __ATOMIC_RELEASE); return; }
    if (keycode == config->explorerKitKey) { __atomic_store_n(&sItemRequested, SHORTCUT_EXPLORER_KIT, __ATOMIC_RELEASE); return; }
    if (keycode == config->undergroundRadarKey) {
        __atomic_store_n(&sRadarRequested, 1, __ATOMIC_RELEASE);
        return;
    }
    if (keycode == config->swapScreensKey) {
        SIM_SetScreensSwapped(!SIM_GetScreensSwapped());
        return;
    }
    if (keycode == config->saveKey) {
        __atomic_store_n(&sSaveRequested, 1, __ATOMIC_RELEASE);
        return;
    }
    if (keycode == config->resetKey) {
        GameRestart_Request();
        return;
    }
    // Preserve the main-keyboard/keypad aliases only for the default bindings.
    if (keycode == config->cameraDistanceIncreaseKey
        || (config->cameraDistanceIncreaseKey == SDLK_PLUS
            && (keycode == SDLK_EQUALS || keycode == SDLK_KP_PLUS))) {
        CameraDistance_RequestStep(1);
        return;
    }
    if (keycode == config->cameraDistanceDecreaseKey
        || (config->cameraDistanceDecreaseKey == SDLK_MINUS && keycode == SDLK_KP_MINUS)) {
        CameraDistance_RequestStep(-1);
        return;
    }
    if (keycode == config->mapKey) __atomic_store_n(&sMapRequested, 1, __ATOMIC_RELEASE);
    for (int i = 0; i < 25; i++) {
        int defaultKey = i == 9 ? SDLK_0 : SDLK_1 + i;
        int keypadKey = i == 9 ? SDLK_KP_0 : SDLK_KP_1 + i;
        if (keycode == config->poketchKeys[i]
            || (i < 10 && config->poketchKeys[i] == defaultKey && keycode == keypadKey)) {
            PoketchKeyboard_QueuePage(i);
            break;
        }
    }
}

void GameShortcuts_NotifyJoyDown(int key)
{
    if (key == SIM_CONFIG_JOY_INVALID_KEY) return;
    const SIM_config_type *config = SIM_GetConfigPtr();
    if (key == config->defogJoyKey) __atomic_store_n(&sFieldMoveRequested, FIELD_MOVE_DEFOG + 1, __ATOMIC_RELEASE);
    if (key == config->flyJoyKey) __atomic_store_n(&sFieldMoveRequested, FIELD_MOVE_FLY + 1, __ATOMIC_RELEASE);
    if (key == config->teleportJoyKey) __atomic_store_n(&sFieldMoveRequested, FIELD_MOVE_TELEPORT + 1, __ATOMIC_RELEASE);
    if (key == config->surfJoyKey) __atomic_store_n(&sFieldMoveRequested, FIELD_MOVE_SURF + 1, __ATOMIC_RELEASE);
    if (key == config->bicycleJoyKey) __atomic_store_n(&sItemRequested, SHORTCUT_BICYCLE, __ATOMIC_RELEASE);
    if (key == config->fishingJoyKey) __atomic_store_n(&sItemRequested, SHORTCUT_FISHING, __ATOMIC_RELEASE);
    if (key == config->explorerKitJoyKey) __atomic_store_n(&sItemRequested, SHORTCUT_EXPLORER_KIT, __ATOMIC_RELEASE);
    if (key == config->undergroundRadarJoyKey) __atomic_store_n(&sRadarRequested, 1, __ATOMIC_RELEASE);
    if (key == config->swapScreensJoyKey) SIM_SetScreensSwapped(!SIM_GetScreensSwapped());
    if (key == config->mapJoyKey) __atomic_store_n(&sMapRequested, 1, __ATOMIC_RELEASE);
    for (int i = 0; i < 25; i++) {
        if (key == config->poketchJoyKeys[i]) { PoketchKeyboard_QueuePage(i); break; }
    }
    if (key == config->saveJoyKey) __atomic_store_n(&sSaveRequested, 1, __ATOMIC_RELEASE);
    if (key == config->resetJoyKey) GameRestart_Request();
    if (key == config->cameraDistanceIncreaseJoyKey) CameraDistance_RequestStep(1);
    if (key == config->cameraDistanceDecreaseJoyKey) CameraDistance_RequestStep(-1);
}

static void FreeMessage(void)
{
    if (sUi.message != NULL) {
        String_Free(sUi.message);
        sUi.message = NULL;
    }
}

static String *CreateResultMessage(FieldSystem *fieldSystem, int success)
{
    MessageLoader *loader = MessageLoader_Init(MSG_LOADER_LOAD_ON_DEMAND,
        NARC_INDEX_MSGDATA__PL_MSG, TEXT_BANK_COMMON_STRINGS, HEAP_ID_FIELD1);
    String *message;

    if (success) {
        StringTemplate *template = StringTemplate_Default(HEAP_ID_FIELD1);
        StringTemplate_SetPlayerName(template, 0, SaveData_GetTrainerInfo(fieldSystem->saveData));
        message = MessageUtil_ExpandedString(template, loader, CommonStrings_Text_PlayerSavedTheGame, HEAP_ID_FIELD1);
        StringTemplate_Free(template);
    } else {
        message = MessageLoader_GetNewString(loader, CommonStrings_Text_SaveError);
    }

    MessageLoader_Free(loader);
    return message;
}

static BOOL RunSaveTask(FieldTask *task)
{
    FieldSystem *fieldSystem = FieldTask_GetFieldSystem(task);
    const Options *options = SaveData_GetOptions(fieldSystem->saveData);

    switch (sUi.state) {
    case 0:
        Window_Init(&sUi.messageWindow);
        sUi.saveInfo = SaveInfoWindow_New(fieldSystem, HEAP_ID_FIELD1, BG_LAYER_MAIN_3);
        SaveInfoWindow_Draw(sUi.saveInfo);
        sUi.message = MessageBank_GetNewStringFromNARC(NARC_INDEX_MSGDATA__PL_MSG, TEXT_BANK_COMMON_STRINGS,
            CommonStrings_Text_SavingDontTurnOffThePower, HEAP_ID_FIELD1);
        FieldMessage_AddWindow(fieldSystem->bgConfig, &sUi.messageWindow, BG_LAYER_MAIN_3);
        FieldMessage_DrawWindow(&sUi.messageWindow, options);
        sUi.printerId = FieldMessage_Print(&sUi.messageWindow, sUi.message, options, 1);
        sUi.state = 1;
        break;
    case 1:
        if (!FieldMessage_FinishedPrinting(sUi.printerId)) {
            break;
        }
        sUi.waitDial = Window_AddWaitDial(&sUi.messageWindow, BASE_TILE_SCROLLING_MESSAGE_BOX);
        sUi.state = 2;
        break;
    case 2: {
        int success = FieldSystem_Save(fieldSystem) != 0;
        DestroyWaitDial(sUi.waitDial);
        sUi.waitDial = NULL;
        FreeMessage();
        FieldMessage_ClearWindow(&sUi.messageWindow);
        sUi.message = CreateResultMessage(fieldSystem, success);
        sUi.printerId = FieldMessage_Print(&sUi.messageWindow, sUi.message, options, 1);
        if (success) {
            Sound_PlayEffect(SEQ_SE_DP_SAVE_sseq);
        }
        sUi.state = 3;
        break;
    }
    case 3:
        if (!FieldMessage_FinishedPrinting(sUi.printerId)) {
            break;
        }
        sUi.delay = RESULT_MESSAGE_FRAMES;
        sUi.state = 4;
        break;
    case 4:
        if (sUi.delay-- > 0) {
            break;
        }
        FreeMessage();
        Window_EraseMessageBox(&sUi.messageWindow, 0);
        Window_Remove(&sUi.messageWindow);
        SaveInfoWindow_Erase(sUi.saveInfo);
        SaveInfoWindow_Free(sUi.saveInfo);
        memset(&sUi, 0, sizeof(sUi));
        sSaveActive = 0;
        return 1;
    }

    return 0;
}

void GameShortcuts_SetMapOpen(int open)
{
    sMapOpen = open;
    sMapCloseRequested = 0;
}

int GameShortcuts_TakeMapCloseRequest(void)
{
    int requested = sMapCloseRequested;
    sMapCloseRequested = 0;
    return requested;
}

void GameShortcuts_Update(void)
{
    FieldSystem *fieldSystem;

    if (__atomic_exchange_n(&sRadarRequested, 0, __ATOMIC_ACQ_REL)) {
        fieldSystem = DEBUG_GetFieldSystem();
        if (fieldSystem != NULL && fieldSystem->mapLoadType == MAP_LOAD_TYPE_UNDERGROUND
            && FieldSystem_IsRunningFieldMap(fieldSystem) && !FieldSystem_IsRunningTask(fieldSystem)) {
            UndergroundMan_UseRadarShortcut();
        }
    }

    PoketchKeyboard_Update();

    if (!sSaveActive) {
        GameRestart_Update();
    }

    int fieldMoveRequested = __atomic_exchange_n(&sFieldMoveRequested, 0, __ATOMIC_ACQ_REL);
    int itemRequested = __atomic_exchange_n(&sItemRequested, 0, __ATOMIC_ACQ_REL);
    int mapRequested = __atomic_exchange_n(&sMapRequested, 0, __ATOMIC_ACQ_REL);
    int saveRequested = __atomic_exchange_n(&sSaveRequested, 0, __ATOMIC_ACQ_REL);
    if (mapRequested && sMapOpen) {
        sMapCloseRequested = 1;
        return;
    }
    if ((!mapRequested && !saveRequested && !itemRequested && !fieldMoveRequested) || sSaveActive) {
        return;
    }

    fieldSystem = DEBUG_GetFieldSystem();
    if (itemRequested == SHORTCUT_FISHING && Fishing_TryHandleShortcut(fieldSystem)) {
        return;
    }
    if (fieldSystem == NULL || FieldSystem_IsRunningTask(fieldSystem)
        || !FieldSystem_IsRunningFieldMap(fieldSystem) || CommManager_IsInitializedNotPoketch()) {
        return;
    }

    if (fieldMoveRequested) {
        StartMenu_UseFieldMoveShortcut(fieldSystem, fieldMoveRequested - 1);
        return;
    }

    if (mapRequested) {
        FieldSystem_UseTownMapShortcut(fieldSystem);
        return;
    }

    if (itemRequested) {
        if (itemRequested == SHORTCUT_FISHING) FieldSystem_UseFishingShortcut(fieldSystem);
        else FieldSystem_UseItemShortcut(fieldSystem,
            itemRequested == SHORTCUT_BICYCLE ? ITEM_BICYCLE : ITEM_EXPLORER_KIT);
        return;
    }

    memset(&sUi, 0, sizeof(sUi));
    sSaveActive = 1;
    FieldSystem_CreateTask(fieldSystem, RunSaveTask, NULL);
}
