#include "applications/options_menu.h"

#include <nitro.h>
#include <stdio.h>
#include <string.h>

#include "constants/heap.h"
#include "constants/narc.h"

#include "bg_window.h"
#include "font.h"
#include "game_options.h"
#include "graphics.h"
#include "gx_layers.h"
#include "heap.h"
#include "menu.h"
#include "message.h"
#include "narc.h"
#include "network_icon.h"
#include "overlay_manager.h"
#include "palette.h"
#include "render_text.h"
#include "render_window.h"
#include "screen_fade.h"
#include "sound.h"
#include "sound_playback.h"
#include "sprite_system.h"
#include "string_gf.h"
#include "system.h"
#include "text.h"
#include "vram_transfer.h"

#ifdef SDK_PORT
#include "mods/runtime.h"
#include "port/autosave.h"
#include "port/game_speed.h"
#include "port/camera_distance.h"
#include "simulator/config/sim_config.h"
#include "simulator/sim.h"
#endif

#include "res/graphics/options_menu/config_gra.naix"
#include "res/text/bank/options_menu.h"

#define MENU_TITLE_BASE_TILE      10
#define MENU_TITLE_WIDTH          30
#define MENU_TITLE_HEIGHT         2
#define ENTRIES_BASE_TILE         (MENU_TITLE_BASE_TILE + MENU_TITLE_WIDTH * MENU_TITLE_HEIGHT)
#define ENTRIES_WIDTH             30
#define ENTRIES_HEIGHT            14
#define DESCRIPTION_BASE_TILE     (ENTRIES_BASE_TILE + ENTRIES_WIDTH * ENTRIES_HEIGHT)
#define DESCRIPTION_WIDTH         27
#define DESCRIPTION_HEIGHT        4
#define STANDARD_WINDOW_BASE_TILE (DESCRIPTION_BASE_TILE + DESCRIPTION_WIDTH * DESCRIPTION_HEIGHT)
#define MESSAGE_BOX_BASE_TILE     (STANDARD_WINDOW_BASE_TILE + 9)
#define CONFIRMATION_BASE_TILE    (MESSAGE_BOX_BASE_TILE + 30)

#define CHOICES_X     (12 * 8 + 4)
#define CHOICES_WIDTH (48 * 8)

#define SINGLE_ENTRY_HEIGHT 16
#define FIRST_ENTRY_OFFSET  24

#define NUM_WINDOWS      3
#define NUM_BG_TEMPLATES 5

#define HEAP_ALLOCATION_SIZE 0x10000

enum OptionsMenuEntryID {
    ENTRY_TEXT_SPEED = 0,
    ENTRY_SOUND_MODE,
    ENTRY_BATTLE_SCENE,
    ENTRY_BATTLE_STYLE,
    ENTRY_BUTTON_MODE,
    ENTRY_MESSAGE_BOX_FRAME,
#ifdef SDK_PORT
    ENTRY_RESOLUTION_SCALE,
    ENTRY_WINDOW_SIZE,
    ENTRY_SKIP_INTRO,
    ENTRY_VSYNC,
    ENTRY_FPS,
    ENTRY_MONITOR,
    ENTRY_FPS_COUNTER,
    ENTRY_CAPTURE_EXP,
    ENTRY_EXP_SHARE,
    ENTRY_MOVEMENT_SPEED,
    ENTRY_BATTLE_SPEED,
    ENTRY_CAMERA_DISTANCE,
    ENTRY_AUTOSAVE,
    ENTRY_HIDE_POKEMON,
    ENTRY_SCREEN_LAYOUT,
    ENTRY_SWAP_SCREENS,
#endif
    ENTRY_CLOSE,

    MAX_ENTRIES,
};

#ifdef SDK_PORT
typedef struct OptionsMenuPage {
    const char *title;
    u8 count;
    u8 entries[7];
} OptionsMenuPage;

static const OptionsMenuPage sMainPages[] = {
    { "Gameplay", 6, { ENTRY_TEXT_SPEED, ENTRY_BATTLE_SCENE, ENTRY_BATTLE_STYLE, ENTRY_BUTTON_MODE, ENTRY_MESSAGE_BOX_FRAME, ENTRY_CAPTURE_EXP } },
    { "Gameplay 2", 4, { ENTRY_EXP_SHARE, ENTRY_MOVEMENT_SPEED, ENTRY_BATTLE_SPEED, ENTRY_HIDE_POKEMON } },
    { "Sound", 1, { ENTRY_SOUND_MODE } },
    { "Display", 7, { ENTRY_MONITOR, ENTRY_WINDOW_SIZE, ENTRY_RESOLUTION_SCALE, ENTRY_FPS, ENTRY_VSYNC, ENTRY_SCREEN_LAYOUT, ENTRY_SWAP_SCREENS } },
    { "Other", 4, { ENTRY_SKIP_INTRO, ENTRY_FPS_COUNTER, ENTRY_CAMERA_DISTANCE, ENTRY_AUTOSAVE } },
};

#define MAIN_PAGE_COUNT (sizeof(sMainPages) / sizeof(sMainPages[0]))
#endif

typedef struct OptionsMenuEntry {
    u16 numChoices;
    u16 selected;
    String *choices[20];
} OptionsMenuEntry;

typedef struct OptionsMenuSelections {
    u16 textSpeed : 4;
    u16 soundMode : 2;
    u16 battleScene : 1;
    u16 battleStyle : 1;
    u16 buttonMode : 2;
    u16 messageBoxStyle : 5;
    u16 : 1;
} OptionsMenuSelections;

typedef struct OptionsMenuData {
    enum HeapID heapID;
    int state;
    int subState;
    int dummy0C;
    u32 saveSelections : 2;
#ifdef SDK_PORT
    u32 cursor : 5;
    u32 dummy10_7 : 14;
#else
    u32 cursor : 3;
    u32 dummy10_5 : 16;
#endif
    u32 redrawMessageBox : 1;
    u32 dummy10_22 : 10;
    BgConfig *bgConfig;
    OptionsMenuSelections options;
    Options *saveOptions;
    MessageLoader *msgLoader;
    void *nscrBuffer;
    NNSG2dScreenData *tilemapData;

    union {
        Window asArray[NUM_WINDOWS];
        struct {
            Window title;
            Window entries;
            Window description;
        };
    } windows;

    union {
        OptionsMenuEntry asArray[MAX_ENTRIES];
        struct {
            OptionsMenuEntry textSpeed;
            OptionsMenuEntry soundMode;
            OptionsMenuEntry battleScene;
            OptionsMenuEntry battleStyle;
            OptionsMenuEntry buttonMode;
            OptionsMenuEntry messageBoxStyle;
#ifdef SDK_PORT
            OptionsMenuEntry resolutionScale;
            OptionsMenuEntry windowSize;
            OptionsMenuEntry skipIntro;
            OptionsMenuEntry vsync;
            OptionsMenuEntry fps;
            OptionsMenuEntry monitor;
            OptionsMenuEntry fpsCounter;
            OptionsMenuEntry captureExp;
            OptionsMenuEntry expShare;
            OptionsMenuEntry movementSpeed;
            OptionsMenuEntry battleSpeed;
            OptionsMenuEntry cameraDistance;
            OptionsMenuEntry autosave;
            OptionsMenuEntry hidePokemon;
            OptionsMenuEntry screenLayout;
            OptionsMenuEntry swapScreens;
#endif
            OptionsMenuEntry close;
        };
    } entries;

    Menu *yesNoChoice;
    u32 textPrinter;
    void *dummy2B0;
    void *dummy2B4;
    void *dummy2B8;
    BOOL isModMenu;
#ifdef SDK_PORT
    u32 mainPage;
#endif
    u32 modIndex;
    u32 modOptionCount;
    u32 modCursor;
    u32 modTop;
    u32 initialModSelections[POKEPLATINUM_MOD_MAX_OPTIONS];
    u32 pendingModSelections[POKEPLATINUM_MOD_MAX_OPTIONS];
} OptionsMenuData;

static void OptionsMenuVBlank(void *data);

static int SetupMenuVisuals(OptionsMenuData *menuData);
static void SetVRAMBanks(void);
static void SetupBgs(OptionsMenuData *menuData);
static void LoadBgTiles(OptionsMenuData *menuData);
static void SetupWindows(OptionsMenuData *menuData);

static void PrintTitleAndEntries(OptionsMenuData *param0);
static void PrintEntryChoices(OptionsMenuData *menuData, u16 entry);
#ifdef SDK_PORT
static void PrintMainEntries(OptionsMenuData *menuData);
static u16 MainEntryRow(const OptionsMenuData *menuData, u16 entry);
static BOOL ProcessMainPageInput(OptionsMenuData *menuData);
#endif
static void PrintEntryDescription(OptionsMenuData *menuData, u16 entry, BOOL scheduleVRAMCopy);
static void PrintBankEntryAsDescription(OptionsMenuData *menuData, u16 entry, BOOL scheduleVRAMCopy);
static BOOL IsTextPrinterDone(const OptionsMenuData *menuData);
static void LoadAllEntryChoices(OptionsMenuData *menuData);

static BOOL ChangesWereMade(OptionsMenuData *menuData);
static void DrawConfirmationPrompt(OptionsMenuData *menuData);

static void ProcessMainInput(OptionsMenuData *menuData);
static BOOL ProcessModInput(OptionsMenuData *menuData);
static void ApplyModSelections(OptionsMenuData *menuData);
static void PrintModTitleAndEntries(OptionsMenuData *menuData);
static void PrintModEntries(OptionsMenuData *menuData);
static void PrintModDescription(OptionsMenuData *menuData);
#ifdef SDK_BUILD_ARM
static u32 ProcessConfirmationInput(OptionsMenuData *menuData);
#else
static u64 ProcessConfirmationInput(OptionsMenuData *menuData);
#endif

static int TeardownMenuData(OptionsMenuData *menuData);
static void TeardownBgs(OptionsMenuData *menuData);
static void TeardownTilemaps(OptionsMenuData *menuData);
static void TeardownWindows(OptionsMenuData *menuData);

BOOL OptionsMenu_Init(ApplicationManager *appMan, int *state)
{
    OptionsMenuData *menuData = NULL;
    OptionsMenu *args = ApplicationManager_Args(appMan);
    Options *options = args->options;

    Heap_Create(HEAP_ID_APPLICATION, HEAP_ID_OPTIONS_MENU, HEAP_ALLOCATION_SIZE);

    menuData = ApplicationManager_NewData(appMan, sizeof(OptionsMenuData), HEAP_ID_OPTIONS_MENU);
    memset(menuData, 0, sizeof(OptionsMenuData));

    menuData->options.textSpeed = Options_TextSpeed(options);
    menuData->options.battleScene = Options_BattleScene(options);
    menuData->options.battleStyle = Options_BattleStyle(options);
    menuData->options.soundMode = Options_SoundMode(options);
    menuData->options.buttonMode = Options_ButtonMode(options);
    menuData->options.messageBoxStyle = Options_Frame(options);
#ifdef SDK_PORT
    menuData->entries.resolutionScale.selected = SIM_GetInternalResolutionScale() - 1;
    menuData->entries.windowSize.selected = SIM_GetWindowSizeMode() - 1;
    menuData->entries.skipIntro.selected = SIM_GetSkipIntroMode();
    menuData->entries.screenLayout.selected = SIM_GetConfigPtr()->screenLayout;
    menuData->entries.swapScreens.selected = SIM_GetConfigPtr()->swapScreens ? 0 : 1;
    menuData->entries.vsync.selected = SIM_GetVsyncEnabled() ? 0 : 1;
    menuData->entries.fps.selected = SIM_GetFrameRateMode();
    menuData->entries.monitor.selected = SIM_GetMonitor() - 1;
    menuData->entries.fpsCounter.selected = SIM_GetFpsCounterEnabled() ? 0 : 1;
    menuData->entries.captureExp.selected = Options_CaptureExp(options) ? 0 : 1;
    menuData->entries.expShare.selected = Options_ExpShare(options) ? 0 : 1;
    menuData->entries.hidePokemon.selected = Options_HidePokemon(options) ? 0 : 1;
    menuData->entries.movementSpeed.selected = SIM_GetConfigPtr()->movementSpeed;
    menuData->entries.battleSpeed.selected = SIM_GetConfigPtr()->battleSpeed;
    menuData->entries.cameraDistance.selected = SIM_GetConfigPtr()->cameraDistance - 1;
    menuData->entries.autosave.selected = FieldAutosave_IsEnabled() ? 0 : 1;
#endif
    menuData->heapID = HEAP_ID_OPTIONS_MENU;
    menuData->saveOptions = options;
    menuData->isModMenu = args->isModMenu;
    menuData->modIndex = args->modIndex;
#ifdef SDK_PORT
    menuData->modOptionCount = args->isModMenu ? Mods_GetOptionCount(args->modIndex) : 0;
    for (u32 i = 0; i < menuData->modOptionCount; i++) {
        menuData->initialModSelections[i] = Mods_GetOptionSelectedValue(args->modIndex, i);
        menuData->pendingModSelections[i] = menuData->initialModSelections[i];
    }
#else
    menuData->modOptionCount = 0;
#endif
    menuData->modCursor = args->cursor <= menuData->modOptionCount ? args->cursor : 0;

    RenderControlFlags_SetCanABSpeedUpPrint(FALSE);

    return TRUE;
}

BOOL OptionsMenu_Exit(ApplicationManager *appMan, int *state)
{
    OptionsMenuData *menuData = ApplicationManager_Data(appMan);
    OptionsMenu *args = ApplicationManager_Args(appMan);

    if (!menuData->isModMenu && menuData->saveSelections == 1) {
        menuData->options.textSpeed = menuData->entries.textSpeed.selected;
        menuData->options.battleScene = menuData->entries.battleScene.selected;
        menuData->options.battleStyle = menuData->entries.battleStyle.selected;
        menuData->options.soundMode = menuData->entries.soundMode.selected;
        menuData->options.buttonMode = menuData->entries.buttonMode.selected;
        menuData->options.messageBoxStyle = menuData->entries.messageBoxStyle.selected;
#ifdef SDK_PORT
        SIM_RequestInternalResolutionScale(menuData->entries.resolutionScale.selected + 1);
        SIM_RequestWindowSizeMode(menuData->entries.windowSize.selected + 1);
        SIM_RequestSkipIntroMode(menuData->entries.skipIntro.selected);
        SIM_RequestVsyncEnabled(menuData->entries.vsync.selected == 0);
        SIM_RequestFrameRateMode(menuData->entries.fps.selected);
        SIM_RequestMonitor(menuData->entries.monitor.selected + 1);
        SIM_RequestFpsCounterEnabled(menuData->entries.fpsCounter.selected == 0);
        Options_SetCaptureExp(menuData->saveOptions, menuData->entries.captureExp.selected == 0);
        Options_SetExpShare(menuData->saveOptions, menuData->entries.expShare.selected == 0);
        Options_SetHidePokemon(menuData->saveOptions, menuData->entries.hidePokemon.selected == 0);
        SIM_GetConfigPtr()->movementSpeed = menuData->entries.movementSpeed.selected;
        SIM_GetConfigPtr()->battleSpeed = menuData->entries.battleSpeed.selected;
        SIM_GetConfigPtr()->cameraDistance = menuData->entries.cameraDistance.selected + 1;
        SIM_GetConfigPtr()->screenLayout = menuData->entries.screenLayout.selected;
        SIM_GetConfigPtr()->swapScreens = menuData->entries.swapScreens.selected == 0;
        SIM_SetScreensSwapped(SIM_GetConfigPtr()->swapScreens);
        FieldAutosave_SetEnabled(menuData->entries.autosave.selected == 0);
        CameraDistance_ApplySettings();
        GameSpeed_ApplySettings();
        SIM_Config_SaveGameConfigFile(SIM_GetConfigPtr());
#endif
    }

    if (!menuData->isModMenu) {
        Options_SetTextSpeed(menuData->saveOptions, menuData->options.textSpeed);
        Options_SetBattleScene(menuData->saveOptions, menuData->options.battleScene);
        Options_SetBattleStyle(menuData->saveOptions, menuData->options.battleStyle);
        Options_SetSoundMode(menuData->saveOptions, menuData->options.soundMode);
        Options_SetButtonMode(menuData->saveOptions, menuData->options.buttonMode);
        Options_SetFrame(menuData->saveOptions, menuData->options.messageBoxStyle);
        Sound_SetPlaybackMode(menuData->options.soundMode);
        Options_SetSystemButtonMode(NULL, menuData->options.buttonMode);
    } else {
        args->cursor = menuData->modCursor;
    }

    RenderControlFlags_SetCanABSpeedUpPrint(TRUE);

    ApplicationManager_FreeData(appMan);
    Heap_Destroy(menuData->heapID);

    return TRUE;
}

enum OptonsMenuState {
    STATE_SETUP_MENU_VISUALS = 0,
    STATE_WAIT_FOR_FADE_IN,
    STATE_HANDLE_INPUT,
    STATE_CONFIRM_NEW_SETTINGS,
    STATE_WAIT_CONFIRM_NEW_SETTINGS,
    STATE_HANDLE_YES_NO_INPUT,
    STATE_START_VISUAL_TEARDOWN,
    STATE_WAIT_FOR_FADE_OUT,
    STATE_TEARDOWN,
};

BOOL OptionsMenu_Main(ApplicationManager *appMan, int *state)
{
    OptionsMenuData *menuData = ApplicationManager_Data(appMan);
    #ifdef SDK_BUILD_ARM
    u32 choiceYesNo;
    #else
    u64 choiceYesNo;
    #endif

    switch (menuData->state) {
    case STATE_SETUP_MENU_VISUALS:
        if (!SetupMenuVisuals(menuData)) {
            return FALSE;
        }

        StartScreenFade(FADE_MAIN_ONLY, FADE_TYPE_BRIGHTNESS_IN, FADE_TYPE_BRIGHTNESS_IN, COLOR_BLACK, 6, 1, menuData->heapID);
        break;

    case STATE_WAIT_FOR_FADE_IN:
        if (!IsScreenFadeDone()) {
            return FALSE;
        }
        break;

    case STATE_HANDLE_INPUT:
        if (menuData->isModMenu) {
            if (ProcessModInput(menuData)) {
                if (ChangesWereMade(menuData) == TRUE) {
                    menuData->state = STATE_CONFIRM_NEW_SETTINGS;
                } else {
                    Sound_PlayEffect(SE_CONFIRM_sseq_3);
                    menuData->saveSelections = 2;
                    menuData->state = STATE_START_VISUAL_TEARDOWN;
                }
            }
            return FALSE;
        }

#ifdef SDK_PORT
        // Handle shoulders before A: Button Mode can map L to A and remove R.
        if (ProcessMainPageInput(menuData)) {
            return FALSE;
        }
#endif
        if ((JOY_NEW(PAD_BUTTON_A) && menuData->cursor == ENTRY_CLOSE) || JOY_NEW(PAD_BUTTON_B)) {
            if (ChangesWereMade(menuData) == TRUE) {
                menuData->state = STATE_CONFIRM_NEW_SETTINGS;
            } else {
                Sound_PlayEffect(SE_CONFIRM_sseq_3);
                menuData->saveSelections = 2;
                menuData->state = STATE_START_VISUAL_TEARDOWN;
            }

            return FALSE;
        }

        ProcessMainInput(menuData);
        return FALSE;

    case STATE_CONFIRM_NEW_SETTINGS:
        RenderControlFlags_SetCanABSpeedUpPrint(TRUE);
        PrintBankEntryAsDescription(menuData, OptionsMenu_Text_ConfirmDialog, FALSE);
        break;

    case STATE_WAIT_CONFIRM_NEW_SETTINGS:
        if (IsTextPrinterDone(menuData)) {
            RenderControlFlags_SetCanABSpeedUpPrint(FALSE);
            DrawConfirmationPrompt(menuData);
            menuData->state = STATE_HANDLE_YES_NO_INPUT;
        }

        return FALSE;

    case STATE_HANDLE_YES_NO_INPUT:
        choiceYesNo = ProcessConfirmationInput(menuData);

        if (choiceYesNo != MENU_NOTHING_CHOSEN) {
            if (choiceYesNo == 0) {
                Sound_StopEffect(SE_CONFIRM_sseq_3, 0);
                Sound_PlayEffect(SEQ_SE_DP_SAVE_sseq);
                if (menuData->isModMenu) {
                    ApplyModSelections(menuData);
                }
                menuData->saveSelections = 1;
            } else {
                menuData->saveSelections = 2;
            }

            menuData->state = STATE_START_VISUAL_TEARDOWN;
        }

        return FALSE;

    case STATE_START_VISUAL_TEARDOWN:
        if (!menuData->isModMenu && Text_IsPrinterActive(menuData->textPrinter)) {
            Text_RemovePrinter(menuData->textPrinter);
        }

        StartScreenFade(FADE_MAIN_ONLY, FADE_TYPE_BRIGHTNESS_OUT, FADE_TYPE_BRIGHTNESS_OUT, COLOR_BLACK, 6, 1, menuData->heapID);
        break;

    case STATE_WAIT_FOR_FADE_OUT:
        if (!IsScreenFadeDone()) {
            return FALSE;
        }
        break;

    case STATE_TEARDOWN:
        if (!TeardownMenuData(menuData)) {
            return FALSE;
        }

        return TRUE;
    }

    menuData->state++;
    return FALSE;
}

static void SetVRAMBanks()
{
    GXBanks banks = {
        GX_VRAM_BG_128_A,
        GX_VRAM_BGEXTPLTT_NONE,
        GX_VRAM_SUB_BG_128_C,
        GX_VRAM_SUB_BGEXTPLTT_NONE,
        GX_VRAM_OBJ_16_G,
        GX_VRAM_OBJEXTPLTT_NONE,
        GX_VRAM_SUB_OBJ_16_I,
        GX_VRAM_SUB_OBJEXTPLTT_NONE,
        GX_VRAM_TEX_NONE,
        GX_VRAM_TEXPLTT_NONE,
    };

    GXLayers_SetBanks(&banks);
}

static void OptionsMenuVBlank(void *data)
{
    OptionsMenuData *menuData = data;

    if (!menuData->isModMenu && menuData->redrawMessageBox) {
        LoadMessageBoxGraphics(menuData->bgConfig,
            BG_LAYER_MAIN_1,
            MESSAGE_BOX_BASE_TILE,
            15,
            menuData->entries.messageBoxStyle.selected,
            menuData->heapID);
        menuData->redrawMessageBox = FALSE;
    }

    SpriteSystem_TransferOam();
    NNS_GfdDoVramTransfer();
    Bg_RunScheduledUpdates(menuData->bgConfig);
    OS_SetIrqCheckFlag(OS_IE_V_BLANK);
}

static int SetupMenuVisuals(OptionsMenuData *menuData)
{
    switch (menuData->subState) {
    case 0:
        SetVBlankCallback(NULL, NULL);
        DisableHBlank();
        GXLayers_DisableEngineALayers();
        GXLayers_DisableEngineBLayers();

        GX_SetVisiblePlane(0);
        GXS_SetVisiblePlane(0);

        SetVRAMBanks();
        ResetVisibleHardwareWindows(DS_SCREEN_MAIN);
        ResetVisibleHardwareWindows(DS_SCREEN_SUB);
        SetupBgs(menuData);
        break;

    case 1:
        LoadBgTiles(menuData);
        menuData->msgLoader = MessageLoader_Init(MSG_LOADER_LOAD_ON_DEMAND,
            NARC_INDEX_MSGDATA__PL_MSG,
            TEXT_BANK_OPTIONS_MENU,
            menuData->heapID);
        if (!menuData->isModMenu) {
            LoadAllEntryChoices(menuData);
        }
        break;

    case 2:
        SetupWindows(menuData);
        if (menuData->isModMenu) {
            PrintModTitleAndEntries(menuData);
        } else {
            PrintTitleAndEntries(menuData);
        }
        VramTransfer_New(32, menuData->heapID);
        GXLayers_EngineAToggleLayers(GX_PLANEMASK_OBJ, TRUE);
        NetworkIcon_InitIfConnected();
        SetVBlankCallback(OptionsMenuVBlank, menuData);
        menuData->subState = 0;
        return TRUE;
    }

    menuData->subState++;
    return FALSE;
}

static int TeardownMenuData(OptionsMenuData *menuData)
{
    int v0 = 0, v1 = 0;

    switch (menuData->subState) {
    case 0:
        VramTransfer_Free();
        TeardownWindows(menuData);

        if (!menuData->isModMenu) {
            for (v0 = 0; v0 < MAX_ENTRIES; v0++) {
                for (v1 = 0; v1 < menuData->entries.asArray[v0].numChoices; v1++) {
                    String_Free(menuData->entries.asArray[v0].choices[v1]);
                }
            }
        }
        MessageLoader_Free(menuData->msgLoader);
        TeardownTilemaps(menuData);
        TeardownBgs(menuData);
        break;

    case 1:
        SetVBlankCallback(NULL, NULL);
        DisableHBlank();
        GXLayers_DisableEngineALayers();
        GXLayers_DisableEngineBLayers();

        GX_SetVisiblePlane(0);
        GXS_SetVisiblePlane(0);
        menuData->subState = 0;
        return TRUE;
    }

    menuData->subState++;
    return FALSE;
}

static void SetupBgs(OptionsMenuData *menuData)
{
    menuData->bgConfig = BgConfig_New(menuData->heapID);

    GraphicsModes graphicsModes = {
        GX_DISPMODE_GRAPHICS,
        GX_BGMODE_0,
        GX_BGMODE_0,
        GX_BG0_AS_2D
    };

    SetAllGraphicsModes(&graphicsModes);

    BgTemplate bgTemplates[NUM_BG_TEMPLATES] = {
        {
            .x = 0,
            .y = 0,
            .bufferSize = 0x800,
            .baseTile = 0,
            .screenSize = BG_SCREEN_SIZE_256x256,
            .colorMode = GX_BG_COLORMODE_16,
            .screenBase = GX_BG_SCRBASE_0xf800,
            .charBase = GX_BG_CHARBASE_0x00000,
            .bgExtPltt = GX_BG_EXTPLTT_01,
            .priority = 1,
            .areaOver = 0,
            .mosaic = FALSE,
        },
        {
            .x = 0,
            .y = 0,
            .bufferSize = 0x800,
            .baseTile = 0,
            .screenSize = BG_SCREEN_SIZE_256x256,
            .colorMode = GX_BG_COLORMODE_16,
            .screenBase = GX_BG_SCRBASE_0xf000,
            .charBase = GX_BG_CHARBASE_0x00000,
            .bgExtPltt = GX_BG_EXTPLTT_01,
            .priority = 2,
            .areaOver = 0,
            .mosaic = FALSE,
        },
        {
            .x = 0,
            .y = 0,
            .bufferSize = 0x800,
            .baseTile = 0,
            .screenSize = BG_SCREEN_SIZE_256x256,
            .colorMode = GX_BG_COLORMODE_16,
            .screenBase = GX_BG_SCRBASE_0xe800,
            .charBase = GX_BG_CHARBASE_0x00000,
            .bgExtPltt = GX_BG_EXTPLTT_01,
            .priority = 3,
            .areaOver = 0,
            .mosaic = FALSE,
        },
        {
            .x = 0,
            .y = 0,
            .bufferSize = 0x800,
            .baseTile = 0,
            .screenSize = BG_SCREEN_SIZE_256x256,
            .colorMode = GX_BG_COLORMODE_16,
            .screenBase = GX_BG_SCRBASE_0xe000,
            .charBase = GX_BG_CHARBASE_0x00000,
            .bgExtPltt = GX_BG_EXTPLTT_01,
            .priority = 0,
            .areaOver = 0,
            .mosaic = FALSE,
        },
        {
            .x = 0,
            .y = 0,
            .bufferSize = 0x800,
            .baseTile = 0,
            .screenSize = BG_SCREEN_SIZE_256x256,
            .colorMode = GX_BG_COLORMODE_16,
            .screenBase = GX_BG_SCRBASE_0xf800,
            .charBase = GX_BG_CHARBASE_0x00000,
            .bgExtPltt = GX_BG_EXTPLTT_01,
            .priority = 0,
            .areaOver = 0,
            .mosaic = FALSE,
        },
    };

    for (int i = 0; i < NUM_BG_TEMPLATES; i++) {
        static const int sBgLayers[NUM_BG_TEMPLATES] = {
            // Must declared inline to match.
            BG_LAYER_MAIN_0,
            BG_LAYER_MAIN_1,
            BG_LAYER_MAIN_2,
            BG_LAYER_MAIN_3,
            BG_LAYER_SUB_0,
        };

        Bg_InitFromTemplate(menuData->bgConfig, sBgLayers[i], &(bgTemplates[i]), BG_TYPE_STATIC);
        Bg_ClearTilemap(menuData->bgConfig, sBgLayers[i]);
    }

    Bg_ClearTilesRange(BG_LAYER_MAIN_0, 32, 0, menuData->heapID);
    Bg_ClearTilesRange(BG_LAYER_SUB_0, 32, 0, menuData->heapID);
}

static void TeardownBgs(OptionsMenuData *menuData)
{
    Bg_FreeTilemapBuffer(menuData->bgConfig, BG_LAYER_SUB_0);
    Bg_FreeTilemapBuffer(menuData->bgConfig, BG_LAYER_MAIN_3);
    Bg_FreeTilemapBuffer(menuData->bgConfig, BG_LAYER_MAIN_2);
    Bg_FreeTilemapBuffer(menuData->bgConfig, BG_LAYER_MAIN_1);
    Bg_FreeTilemapBuffer(menuData->bgConfig, BG_LAYER_MAIN_0);
    Heap_Free(menuData->bgConfig);
}

static void LoadBgTiles(OptionsMenuData *menuData)
{
    NNSG2dCharacterData *cursorTiles;
    NNSG2dPaletteData *cursorPalette;

    NARC *narc = NARC_ctor(NARC_INDEX_GRAPHIC__CONFIG_GRA, menuData->heapID);

    u32 memberSize = NARC_GetMemberSize(narc, tiles_NCGR);
    void *memberBuffer = Heap_AllocAtEnd(menuData->heapID, memberSize);
    NARC_ReadWholeMember(narc, tiles_NCGR, memberBuffer);
    NNS_G2dGetUnpackedCharacterData(memberBuffer, &cursorTiles);
    Bg_LoadTiles(menuData->bgConfig, BG_LAYER_MAIN_0, cursorTiles->pRawData, cursorTiles->szByte, 0);
    Bg_LoadTiles(menuData->bgConfig, BG_LAYER_SUB_0, cursorTiles->pRawData, cursorTiles->szByte, 0);
    Heap_Free(memberBuffer);

    memberSize = NARC_GetMemberSize(narc, tiles_NCLR);
    memberBuffer = Heap_AllocAtEnd(menuData->heapID, memberSize);
    NARC_ReadWholeMember(narc, tiles_NCLR, memberBuffer);
    NNS_G2dGetUnpackedPaletteData(memberBuffer, &cursorPalette);
    Bg_LoadPalette(BG_LAYER_MAIN_0, cursorPalette->pRawData, PALETTE_SIZE_BYTES, 0);
    Bg_LoadPalette(BG_LAYER_SUB_0, cursorPalette->pRawData, PALETTE_SIZE_BYTES, 0);
    Heap_Free(memberBuffer);

    memberSize = NARC_GetMemberSize(narc, tilemap_bin);
    menuData->nscrBuffer = Heap_Alloc(menuData->heapID, memberSize);
    NARC_ReadWholeMember(narc, tilemap_bin, menuData->nscrBuffer);
    NNS_G2dGetUnpackedScreenData(menuData->nscrBuffer, &(menuData->tilemapData));

    NARC_dtor(narc);

    Bg_FillTilemapRect(menuData->bgConfig,
        BG_LAYER_MAIN_2,
        1,
        0,
        0,
        32,
        32,
        TILEMAP_FILL_VAL_INCLUDES_PALETTE);
    Bg_FillTilemapRect(menuData->bgConfig,
        BG_LAYER_SUB_0,
        1,
        0,
        0,
        32,
        32,
        TILEMAP_FILL_VAL_INCLUDES_PALETTE);

    Bg_CopyRectToTilemapRect(menuData->bgConfig,
        BG_LAYER_MAIN_0,
        0,
        0,
        32,
        2,
        menuData->tilemapData->rawData,
        0,
        0,
        menuData->tilemapData->screenWidth / 8,
        menuData->tilemapData->screenHeight / 8);

    Bg_SetOffset(menuData->bgConfig, BG_LAYER_MAIN_0, BG_OFFSET_UPDATE_SET_Y, -FIRST_ENTRY_OFFSET);
    Bg_ScheduleTilemapTransfer(menuData->bgConfig, BG_LAYER_MAIN_2);
    Bg_ScheduleTilemapTransfer(menuData->bgConfig, BG_LAYER_MAIN_0);
    Bg_ScheduleTilemapTransfer(menuData->bgConfig, BG_LAYER_SUB_0);
}

static void TeardownTilemaps(OptionsMenuData *menuData)
{
    Heap_Free(menuData->nscrBuffer);
}

static void SetupWindows(OptionsMenuData *menuData)
{
    Window_Add(menuData->bgConfig,
        &menuData->windows.title,
        BG_LAYER_MAIN_1,
        1,
        0,
        MENU_TITLE_WIDTH,
        MENU_TITLE_HEIGHT,
        13,
        MENU_TITLE_BASE_TILE);
    Window_Add(menuData->bgConfig,
        &menuData->windows.entries,
        BG_LAYER_MAIN_1,
        1,
        3,
        ENTRIES_WIDTH,
        ENTRIES_HEIGHT,
        13,
        ENTRIES_BASE_TILE);
    Window_Add(menuData->bgConfig,
        &menuData->windows.description,
        BG_LAYER_MAIN_1,
        2,
        19,
        DESCRIPTION_WIDTH,
        DESCRIPTION_HEIGHT,
        12,
        DESCRIPTION_BASE_TILE);
    LoadStandardWindowGraphics(menuData->bgConfig,
        BG_LAYER_MAIN_1,
        STANDARD_WINDOW_BASE_TILE,
        14,
        STANDARD_WINDOW_SYSTEM,
        menuData->heapID);
    LoadMessageBoxGraphics(menuData->bgConfig,
        BG_LAYER_MAIN_1,
        MESSAGE_BOX_BASE_TILE,
        15,
        menuData->options.messageBoxStyle,
        menuData->heapID);

    Font_LoadTextPalette(PAL_LOAD_MAIN_BG, PLTT_OFFSET(13), menuData->heapID);
    Font_LoadTextPalette(PAL_LOAD_SUB_BG, PLTT_OFFSET(13), menuData->heapID);
    Font_LoadScreenIndicatorsPalette(PAL_LOAD_MAIN_BG, PLTT_OFFSET(12), menuData->heapID);
    Font_LoadScreenIndicatorsPalette(PAL_LOAD_SUB_BG, PLTT_OFFSET(12), menuData->heapID);

    Window_FillTilemap(&menuData->windows.title, PIXEL_FILL(0));
    Window_FillTilemap(&menuData->windows.entries, PIXEL_FILL(15));
    Window_FillTilemap(&menuData->windows.description, PIXEL_FILL(15));
    Window_ClearTilemap(&menuData->windows.description);
    Window_ClearTilemap(&menuData->windows.entries);
    Window_ClearTilemap(&menuData->windows.title);

    Window_DrawStandardFrame(&menuData->windows.entries, TRUE, STANDARD_WINDOW_BASE_TILE, 14);
    Window_DrawMessageBoxWithScrollCursor(&menuData->windows.description, TRUE, MESSAGE_BOX_BASE_TILE, 15);
}

static void TeardownWindows(OptionsMenuData *menuData)
{
    Window_EraseStandardFrame(&menuData->windows.entries, FALSE);
    Window_EraseMessageBox(&menuData->windows.description, FALSE);

    for (u16 i = 0; i < 3; i++) {
        Window_ClearAndCopyToVRAM(&menuData->windows.asArray[i]);
        Window_FillTilemap(&menuData->windows.asArray[i], 0);
        Window_ClearTilemap(&menuData->windows.asArray[i]);
        Window_Remove(&menuData->windows.asArray[i]);
    }
}

static const u8 sEntryLabels[MAX_ENTRIES] = {
    OptionsMenu_Text_TextSpeedLabel,
    OptionsMenu_Text_SoundModeLabel,
    OptionsMenu_Text_BattleSceneLabel,
    OptionsMenu_Text_BattleStyleLabel,
    OptionsMenu_Text_ButtonModeLabel,
    OptionsMenu_Text_MessageBoxStyleLabel,
#ifdef SDK_PORT
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
#endif
    [ENTRY_CLOSE] = OptionsMenu_Text_CloseLabel,
};

static void PrintTitleAndEntries(OptionsMenuData *menuData)
{
#ifndef SDK_PORT
    u16 i; // Must forward-declare to match
#endif
    TextColor transparentBg = TEXT_COLOR(1, 2, 0);

    Window_FillTilemap(&menuData->windows.title, PIXEL_FILL(0));
#ifdef SDK_PORT
    String *string = Mods_StringFromUtf8(sMainPages[menuData->mainPage].title, menuData->heapID);
#else
    TextColor whiteBg = TEXT_COLOR(1, 2, 15);
    String *string = String_Init(256, menuData->heapID);
    MessageLoader_GetString(menuData->msgLoader, OptionsMenu_Text_Title, string);
#endif

    Text_AddPrinterWithParamsAndColor(&menuData->windows.title,
        FONT_SYSTEM,
        string,
        2,
        2,
        TEXT_SPEED_INSTANT,
        transparentBg,
        NULL);

#ifdef SDK_PORT
    char pageHint[32];
    snprintf(pageHint, sizeof(pageHint), "L/R: Page %u/%u",
        (unsigned int)menuData->mainPage + 1, (unsigned int)MAIN_PAGE_COUNT);
    String *hint = Mods_StringFromUtf8(pageHint, menuData->heapID);
    Text_AddPrinterWithParamsAndColor(&menuData->windows.title, FONT_SYSTEM, hint,
        MENU_TITLE_WIDTH * 8 - Font_CalcStringWidth(FONT_SYSTEM, hint, 0) - 2, 2,
        TEXT_SPEED_NO_TRANSFER, transparentBg, NULL);
    String_Free(hint);
    PrintMainEntries(menuData);
#else
    for (i = 0; i < MAX_ENTRIES; i++) {
        String_Clear(string);
        MessageLoader_GetString(menuData->msgLoader, sEntryLabels[i], string);
        Text_AddPrinterWithParamsAndColor(&menuData->windows.entries,
            FONT_SYSTEM,
            string,
            4,
            16 * i,
            TEXT_SPEED_NO_TRANSFER,
            whiteBg,
            NULL);
    }

    for (i = 0; i < MAX_ENTRIES; i++) {
        PrintEntryChoices(menuData, i);
    }
#endif

    PrintEntryDescription(menuData, menuData->cursor, TRUE);
    Window_CopyToVRAM(&menuData->windows.title);
    Window_CopyToVRAM(&menuData->windows.entries);
    String_Free(string);
}

#ifdef SDK_PORT
static u16 MainEntryRow(const OptionsMenuData *menuData, u16 entry)
{
    const OptionsMenuPage *page = &sMainPages[menuData->mainPage];
    for (u16 row = 0; row < page->count; row++) {
        if (page->entries[row] == entry) {
            return row;
        }
    }
    return 0; // Close follows the page's settings.
}

static BOOL ProcessMainPageInput(OptionsMenuData *menuData)
{
    u32 shoulders = gSystem.pressedKeysRaw & (PAD_BUTTON_L | PAD_BUTTON_R);
    if (shoulders == 0) {
        return FALSE;
    }
    if (shoulders == (PAD_BUTTON_L | PAD_BUTTON_R)) {
        return TRUE;
    }

    menuData->mainPage = (menuData->mainPage + MAIN_PAGE_COUNT
        + (shoulders == PAD_BUTTON_R ? 1 : -1)) % MAIN_PAGE_COUNT;
    menuData->cursor = sMainPages[menuData->mainPage].entries[0];
    PrintTitleAndEntries(menuData);
    Bg_ScheduleScroll(menuData->bgConfig, BG_LAYER_MAIN_0,
        BG_OFFSET_UPDATE_SET_Y, -FIRST_ENTRY_OFFSET);
    Sound_PlayEffect(SE_CONFIRM_sseq_3);
    return TRUE;
}

static void PrintMainEntries(OptionsMenuData *menuData)
{
    const OptionsMenuPage *page = &sMainPages[menuData->mainPage];
    TextColor whiteBg = TEXT_COLOR(1, 2, 15);
    Window_FillTilemap(&menuData->windows.entries, PIXEL_FILL(15));

    for (u32 row = 0; row < page->count; row++) {
        u32 entry = page->entries[row];

        String *label;
        if (entry == ENTRY_RESOLUTION_SCALE) {
            label = Mods_StringFromUtf8("RESOLUTION", menuData->heapID);
        } else if (entry == ENTRY_WINDOW_SIZE) {
            label = Mods_StringFromUtf8("WINDOW SIZE", menuData->heapID);
        } else if (entry == ENTRY_CAMERA_DISTANCE) {
            label = Mods_StringFromUtf8("CAMERA DISTANCE", menuData->heapID);
        } else if (entry == ENTRY_SKIP_INTRO) {
            label = Mods_StringFromUtf8("SKIP INTRO", menuData->heapID);
        } else if (entry == ENTRY_SCREEN_LAYOUT) {
            label = Mods_StringFromUtf8("SCREEN LAYOUT", menuData->heapID);
        } else if (entry == ENTRY_SWAP_SCREENS) {
            label = Mods_StringFromUtf8("SWAP SCREENS", menuData->heapID);
        } else if (entry == ENTRY_VSYNC) {
            label = Mods_StringFromUtf8("VSYNC", menuData->heapID);
        } else if (entry == ENTRY_FPS) {
            label = Mods_StringFromUtf8("FPS", menuData->heapID);
        } else if (entry == ENTRY_MONITOR) {
            label = Mods_StringFromUtf8("MONITOR", menuData->heapID);
        } else if (entry == ENTRY_MOVEMENT_SPEED) {
            label = Mods_StringFromUtf8("MOVE SPEED", menuData->heapID);
        } else if (entry == ENTRY_BATTLE_SPEED) {
            label = Mods_StringFromUtf8("BATTLE SPEED", menuData->heapID);
        } else if (entry == ENTRY_EXP_SHARE) {
            label = Mods_StringFromUtf8("EXP SHARE", menuData->heapID);
        } else if (entry == ENTRY_HIDE_POKEMON) {
            label = Mods_StringFromUtf8("HIDE PKMN", menuData->heapID);
        } else if (entry == ENTRY_CAPTURE_EXP) {
            label = Mods_StringFromUtf8("CAPTURE EXP", menuData->heapID);
        } else if (entry == ENTRY_FPS_COUNTER) {
            label = Mods_StringFromUtf8("FPS COUNTER", menuData->heapID);
        } else if (entry == ENTRY_AUTOSAVE) {
            label = Mods_StringFromUtf8("AUTOSAVE", menuData->heapID);
        } else {
            label = MessageLoader_GetNewString(menuData->msgLoader, sEntryLabels[entry]);
        }
        Text_AddPrinterWithParamsAndColor(&menuData->windows.entries,
            FONT_SYSTEM,
            label,
            4,
            row * SINGLE_ENTRY_HEIGHT,
            TEXT_SPEED_NO_TRANSFER,
            whiteBg,
            NULL);
        String_Free(label);
        PrintEntryChoices(menuData, entry);
    }
    Window_CopyToVRAM(&menuData->windows.entries);
}
#endif

#ifdef SDK_PORT
#define MOD_MENU_VISIBLE_ENTRIES 7

static void PrintModEntries(OptionsMenuData *menuData)
{
    TextColor darkGray = TEXT_COLOR(1, 2, 15);
    TextColor red = TEXT_COLOR(3, 4, 15);

    Window_FillTilemap(&menuData->windows.entries, PIXEL_FILL(15));

    for (u32 row = 0; row < MOD_MENU_VISIBLE_ENTRIES; row++) {
        u32 entry = menuData->modTop + row;
        if (entry >= menuData->modOptionCount) {
            break;
        }

        const char *label;
        const char *value = NULL;
        label = Mods_GetOptionLabel(menuData->modIndex, entry);
        u32 selected = menuData->pendingModSelections[entry];
        value = Mods_GetOptionValueLabel(menuData->modIndex, entry, selected);

        String *labelString = Mods_StringFromUtf8(label, menuData->heapID);
        Text_AddPrinterWithParamsAndColor(&menuData->windows.entries,
            FONT_SYSTEM,
            labelString,
            4,
            row * SINGLE_ENTRY_HEIGHT,
            TEXT_SPEED_NO_TRANSFER,
            darkGray,
            NULL);
        String_Free(labelString);

        if (value != NULL) {
            String *valueString = Mods_StringFromUtf8(value, menuData->heapID);
            Text_AddPrinterWithParamsAndColor(&menuData->windows.entries,
                FONT_SYSTEM,
                valueString,
                CHOICES_X,
                row * SINGLE_ENTRY_HEIGHT,
                TEXT_SPEED_NO_TRANSFER,
                red,
                NULL);
            String_Free(valueString);
        }
    }

    Window_CopyToVRAM(&menuData->windows.entries);
}

static void PrintModDescription(OptionsMenuData *menuData)
{
    const char *description = "";

    Window_FillTilemap(&menuData->windows.description, PIXEL_FILL(15));
    String *string = Mods_StringFromUtf8(description, menuData->heapID);
    Text_AddPrinterWithParamsAndColor(&menuData->windows.description,
        FONT_MESSAGE,
        string,
        4,
        0,
        TEXT_SPEED_NO_TRANSFER,
        TEXT_COLOR(1, 2, 15),
        NULL);
    String_Free(string);
    Window_ScheduleCopyToVRAM(&menuData->windows.description);
}

static void PrintModTitleAndEntries(OptionsMenuData *menuData)
{
    PokePlatinumModInfo info = { .structSize = sizeof(PokePlatinumModInfo) };
    const char *title = Mods_GetLoadedInfo(menuData->modIndex, &info) ? info.name : "MOD";
    String *string = Mods_StringFromUtf8(title, menuData->heapID);

    Text_AddPrinterWithParamsAndColor(&menuData->windows.title,
        FONT_SYSTEM,
        string,
        2,
        2,
        TEXT_SPEED_INSTANT,
        TEXT_COLOR(1, 2, 0),
        NULL);
    String_Free(string);

    PrintModEntries(menuData);
    PrintModDescription(menuData);
    Window_CopyToVRAM(&menuData->windows.title);
}

static BOOL ProcessModInput(OptionsMenuData *menuData)
{
    u32 totalEntries = menuData->modOptionCount + 1;

    if (JOY_NEW(PAD_BUTTON_B)) {
        return TRUE;
    }

    if (JOY_NEW(PAD_BUTTON_A) && menuData->modCursor == menuData->modOptionCount) {
        return TRUE;
    }

    if (JOY_NEW(PAD_BUTTON_A)
        && menuData->modCursor < menuData->modOptionCount
        && Mods_IsOptionAction(menuData->modIndex, menuData->modCursor)) {
        Mods_SetOptionSelectedValue(menuData->modIndex,
            menuData->modCursor,
            menuData->pendingModSelections[menuData->modCursor]);
        Sound_PlayEffect(SE_CONFIRM_sseq_3);
        return FALSE;
    }

    if (menuData->modCursor < menuData->modOptionCount
        && !Mods_IsOptionReadOnly(menuData->modIndex, menuData->modCursor)
        && !Mods_IsOptionAction(menuData->modIndex, menuData->modCursor)) {
        u32 valueCount = Mods_GetOptionValueCount(menuData->modIndex, menuData->modCursor);
        u32 selected = menuData->pendingModSelections[menuData->modCursor];

        if (valueCount > 0 && JOY_NEW(PAD_KEY_RIGHT)) {
            menuData->pendingModSelections[menuData->modCursor] = (selected + 1) % valueCount;
            PrintModEntries(menuData);
            Sound_PlayEffect(SE_CONFIRM_sseq_3);
        } else if (valueCount > 0 && JOY_NEW(PAD_KEY_LEFT)) {
            menuData->pendingModSelections[menuData->modCursor] = (selected + valueCount - 1) % valueCount;
            PrintModEntries(menuData);
            Sound_PlayEffect(SE_CONFIRM_sseq_3);
        }
    }

    u32 previousTop = menuData->modTop;
    if (JOY_NEW(PAD_KEY_UP)) {
        menuData->modCursor = (menuData->modCursor + totalEntries - 1) % totalEntries;
    } else if (JOY_NEW(PAD_KEY_DOWN)) {
        menuData->modCursor = (menuData->modCursor + 1) % totalEntries;
    } else {
        return FALSE;
    }

    if (menuData->modCursor < menuData->modTop) {
        menuData->modTop = menuData->modCursor;
    } else if (menuData->modCursor >= menuData->modTop + MOD_MENU_VISIBLE_ENTRIES) {
        menuData->modTop = menuData->modCursor - MOD_MENU_VISIBLE_ENTRIES + 1;
    }

    if (previousTop != menuData->modTop) {
        PrintModEntries(menuData);
    }
    Bg_ScheduleScroll(menuData->bgConfig,
        BG_LAYER_MAIN_0,
        BG_OFFSET_UPDATE_SET_Y,
        -((menuData->modCursor - menuData->modTop) * SINGLE_ENTRY_HEIGHT + FIRST_ENTRY_OFFSET));
    PrintModDescription(menuData);
    Sound_PlayEffect(SE_CONFIRM_sseq_3);
    return FALSE;
}

static void ApplyModSelections(OptionsMenuData *menuData)
{
    for (u32 i = 0; i < menuData->modOptionCount; i++) {
        if (!Mods_IsOptionReadOnly(menuData->modIndex, i)
            && !Mods_IsOptionAction(menuData->modIndex, i)
            && menuData->pendingModSelections[i] != menuData->initialModSelections[i]) {
            Mods_SetOptionSelectedValue(menuData->modIndex, i,
                menuData->pendingModSelections[i]);
        }
    }
}
#else
static BOOL ProcessModInput(OptionsMenuData *menuData)
{
    (void)menuData;
    return TRUE;
}

static void PrintModTitleAndEntries(OptionsMenuData *menuData)
{
    (void)menuData;
}

static void PrintModEntries(OptionsMenuData *menuData)
{
    (void)menuData;
}

static void PrintModDescription(OptionsMenuData *menuData)
{
    (void)menuData;
}

static void ApplyModSelections(OptionsMenuData *menuData)
{
    (void)menuData;
}
#endif

static const int sNumChoicesPerEntry[MAX_ENTRIES] = {
    3,
    2,
    2,
    2,
    3,
    20,
#ifdef SDK_PORT
    11,
    5,
    3,
    2,
    4,
    0,
    2,
    2,
    2,
    7,
    7,
    SIM_CONFIG_CAMERA_DISTANCE_MAX,
    2,
    2,
    3,
    2,
#endif
    0,
};

static const u8 sFirstChoicePerEntry[MAX_ENTRIES] = {
    OptionsMenu_Text_TextSpeedSlow,
    OptionsMenu_Text_SoundModeStereo,
    OptionsMenu_Text_BattleSceneOn,
    OptionsMenu_Text_BattleStyleShift,
    OptionsMenu_Text_ButtonModeNormal,
    OptionsMenu_Text_MessageBoxStyle_01,
#ifdef SDK_PORT
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
#endif
    0,
};

static void LoadAllEntryChoices(OptionsMenuData *menuData)
{
    u16 i, j;
    for (i = 0; i < MAX_ENTRIES; i++) {
        menuData->entries.asArray[i].numChoices = sNumChoicesPerEntry[i];
#ifdef SDK_PORT
        if (i == ENTRY_MONITOR) {
            menuData->entries.asArray[i].numChoices = SIM_GetMonitorCount();
            if (menuData->entries.asArray[i].numChoices > NELEMS(menuData->entries.asArray[i].choices)) {
                menuData->entries.asArray[i].numChoices = NELEMS(menuData->entries.asArray[i].choices);
            }

        }
#endif
        for (j = 0; j < menuData->entries.asArray[i].numChoices; j++) {
#ifdef SDK_PORT
            if (i == ENTRY_RESOLUTION_SCALE) {
                char resolutionValue[24];
                u32 scale = j + 1;
                // Show each screen's render resolution as width x height.
                snprintf(resolutionValue, sizeof(resolutionValue), "%ux%u",
                    SIM_NDS_SCREEN_WIDTH * scale, SIM_NDS_SCREEN_HEIGHT * scale);
                menuData->entries.asArray[i].choices[j] = Mods_StringFromUtf8(resolutionValue, menuData->heapID);
            } else if (i == ENTRY_CAMERA_DISTANCE) {
                char value[8];
                snprintf(value, sizeof(value), "x%u", j + 1);
                menuData->entries.asArray[i].choices[j] = Mods_StringFromUtf8(value, menuData->heapID);
            } else if (i == ENTRY_WINDOW_SIZE) {
                static const char *const windowSizeValues[] = {
                    "x1", "x2", "x3", "x4", "FS",
                };
                menuData->entries.asArray[i].choices[j] = Mods_StringFromUtf8(windowSizeValues[j], menuData->heapID);
            } else if (i == ENTRY_SKIP_INTRO) {
                static const char *const skipIntroValues[] = {
                    "NONE", "LOAD", "GAME",
                };
                menuData->entries.asArray[i].choices[j] = Mods_StringFromUtf8(skipIntroValues[j], menuData->heapID);
            } else if (i == ENTRY_SCREEN_LAYOUT) {
                static const char *const layouts[] = { "Vertical", "Horizontal", "Large Screen" };
                menuData->entries.asArray[i].choices[j] = Mods_StringFromUtf8(layouts[j], menuData->heapID);
            } else if (i == ENTRY_VSYNC || i == ENTRY_HIDE_POKEMON || i == ENTRY_SWAP_SCREENS) {
                static const char *const vsyncValues[] = {
                    "TRUE", "FALSE",
                };
                menuData->entries.asArray[i].choices[j] = Mods_StringFromUtf8(vsyncValues[j], menuData->heapID);
            } else if (i == ENTRY_FPS) {
                static const char *const fpsValues[] = {
                    "30", "60", "AUTO", "INF",
                };
                menuData->entries.asArray[i].choices[j] = Mods_StringFromUtf8(fpsValues[j], menuData->heapID);
            } else if (i == ENTRY_MONITOR) {
                char monitorValue[4];
                snprintf(monitorValue, sizeof(monitorValue), "%d", j + 1);
                menuData->entries.asArray[i].choices[j] = Mods_StringFromUtf8(monitorValue, menuData->heapID);
            } else if (i == ENTRY_MOVEMENT_SPEED || i == ENTRY_BATTLE_SPEED) {
                static const char *const speedValues[] = { "x1", "x1.5", "x2", "x2.5", "x3", "x3.5", "x4" };
                menuData->entries.asArray[i].choices[j] = Mods_StringFromUtf8(speedValues[j], menuData->heapID);
            } else if (i == ENTRY_AUTOSAVE) {
                static const char *const autosaveValues[] = { "ON", "OFF" };
                menuData->entries.asArray[i].choices[j] = Mods_StringFromUtf8(autosaveValues[j], menuData->heapID);
            } else if (i == ENTRY_CAPTURE_EXP || i == ENTRY_EXP_SHARE) {
                static const char *const yesNoValues[] = { "YES", "NO" };
                menuData->entries.asArray[i].choices[j] = Mods_StringFromUtf8(yesNoValues[j], menuData->heapID);
            } else if (i == ENTRY_FPS_COUNTER) {
                static const char *const fpsCounterValues[] = {
                    "TRUE", "FALSE",
                };
                menuData->entries.asArray[i].choices[j] = Mods_StringFromUtf8(fpsCounterValues[j], menuData->heapID);
            } else
#endif
            {
                menuData->entries.asArray[i].choices[j] = MessageLoader_GetNewString(menuData->msgLoader, sFirstChoicePerEntry[i] + j);
            }
        }
    }

    menuData->entries.textSpeed.selected = menuData->options.textSpeed;
    menuData->entries.battleScene.selected = menuData->options.battleScene;
    menuData->entries.battleStyle.selected = menuData->options.battleStyle;
    menuData->entries.soundMode.selected = menuData->options.soundMode;
    menuData->entries.buttonMode.selected = menuData->options.buttonMode;
    menuData->entries.messageBoxStyle.selected = menuData->options.messageBoxStyle;
}

static const s8 sEntryXOffsets[MAX_ENTRIES] = { 0 };

static void PrintEntryChoices(OptionsMenuData *menuData, u16 entry)
{
    TextColor darkGray, red, color;
    u16 i;
    u8 textSpeed;
    u8 choiceSpacing = 48;
    s8 xOffset = 0;
    u16 row = entry;

#ifdef SDK_PORT
    row = MainEntryRow(menuData, entry);
    if (entry == ENTRY_FPS) {
        choiceSpacing = 36;
    }
#endif

    darkGray = TEXT_COLOR(1, 2, 15);
    red = TEXT_COLOR(3, 4, 15);

    Window_FillRectWithColor(&menuData->windows.entries, PIXEL_FILL(15), CHOICES_X + sEntryXOffsets[entry], row * SINGLE_ENTRY_HEIGHT, CHOICES_WIDTH, SINGLE_ENTRY_HEIGHT);
    if (entry == ENTRY_MESSAGE_BOX_FRAME
#ifdef SDK_PORT
        || entry == ENTRY_RESOLUTION_SCALE
        || entry == ENTRY_WINDOW_SIZE
        || entry == ENTRY_MONITOR
        || entry == ENTRY_MOVEMENT_SPEED
        || entry == ENTRY_BATTLE_SPEED
        || entry == ENTRY_CAMERA_DISTANCE
        || entry == ENTRY_SCREEN_LAYOUT
#endif
    ) {
        Text_AddPrinterWithParamsAndColor(&menuData->windows.entries,
            FONT_SYSTEM,
            menuData->entries.asArray[entry].choices[menuData->entries.asArray[entry].selected],
#ifdef SDK_PORT
            entry == ENTRY_SCREEN_LAYOUT ? CHOICES_X : 48 + CHOICES_X,
#else
            48 + CHOICES_X,
#endif
            row * SINGLE_ENTRY_HEIGHT,
            TEXT_SPEED_NO_TRANSFER,
            red,
            NULL);
        Window_CopyToVRAM(&menuData->windows.entries);
        if (entry == ENTRY_MESSAGE_BOX_FRAME) {
            menuData->redrawMessageBox = TRUE;
        }
        return;
    }

    if (entry == ENTRY_SOUND_MODE) {
        Sound_SetPlaybackMode(menuData->entries.asArray[entry].selected);
    } else if (entry == ENTRY_BUTTON_MODE) {
        Options_SetSystemButtonMode(NULL, menuData->entries.asArray[entry].selected);
    } else if (entry == ENTRY_TEXT_SPEED) {
        Options_SetTextSpeed(menuData->saveOptions, menuData->entries.asArray[entry].selected);
        PrintEntryDescription(menuData, entry, FALSE);
    }

    xOffset = 0;
    for (i = 0; i < menuData->entries.asArray[entry].numChoices; i++) {
        if (i == menuData->entries.asArray[entry].selected) {
            color = red;
        } else {
            color = darkGray;
        }

        if (i == menuData->entries.asArray[entry].numChoices - 1) {
            textSpeed = TEXT_SPEED_NO_TRANSFER;
        } else {
            textSpeed = TEXT_SPEED_NO_TRANSFER;
        }

        if (entry == ENTRY_BUTTON_MODE) {
            Text_AddPrinterWithParamsAndColor(&menuData->windows.entries,
                FONT_SYSTEM,
                menuData->entries.asArray[entry].choices[i],
                xOffset + CHOICES_X,
                row * SINGLE_ENTRY_HEIGHT,
                textSpeed,
                color,
                NULL);
            xOffset += Font_CalcStringWidth(FONT_SYSTEM, menuData->entries.asArray[entry].choices[i], 0) + 12;
        } else {
            Text_AddPrinterWithParamsAndColor(&menuData->windows.entries,
                FONT_SYSTEM,
                menuData->entries.asArray[entry].choices[i],
                i * choiceSpacing + CHOICES_X + sEntryXOffsets[entry],
                row * SINGLE_ENTRY_HEIGHT,
                textSpeed,
                color,
                NULL);
        }
    }

    Window_CopyToVRAM(&menuData->windows.asArray[1]);
}

static void PrintBankEntryAsDescription(OptionsMenuData *menuData, u16 entry, BOOL scheduleVRAMCopy)
{
    // Kill any active printer
    if (IsTextPrinterDone(menuData) == FALSE) {
        Text_RemovePrinter(menuData->textPrinter);
    }

    u8 renderDelay = Options_TextFrameDelay(menuData->saveOptions);

    Window_FillTilemap(&menuData->windows.description, 15);

    TextColor color = TEXT_COLOR(1, 2, 15);
    String *string = String_Init(256, menuData->heapID);
    MessageLoader_GetString(menuData->msgLoader, entry, string);

    if (scheduleVRAMCopy == FALSE) {
        menuData->textPrinter = Text_AddPrinterWithParamsAndColor(&menuData->windows.description,
            FONT_MESSAGE,
            string,
            4,
            0,
            renderDelay,
            color,
            NULL);
    } else {
        Text_AddPrinterWithParamsAndColor(&menuData->windows.description,
            FONT_MESSAGE,
            string,
            4,
            0,
            TEXT_SPEED_NO_TRANSFER,
            color,
            NULL);
        Window_ScheduleCopyToVRAM(&menuData->windows.description);
    }

    String_Free(string);
}

static BOOL IsTextPrinterDone(const OptionsMenuData *menuData)
{
    return Text_IsPrinterActive(menuData->textPrinter) == FALSE;
}

static void ProcessMainInput(OptionsMenuData *menuData)
{
    OptionsMenuEntry *entry = &menuData->entries.asArray[menuData->cursor];

    if (menuData->cursor != ENTRY_CLOSE) {
        if (JOY_NEW(PAD_KEY_RIGHT)) {
            entry->selected = (entry->selected + 1) % entry->numChoices;
            PrintEntryChoices(menuData, menuData->cursor);
            Sound_PlayEffect(SE_CONFIRM_sseq_3);
        } else if (JOY_NEW(PAD_KEY_LEFT)) {
            entry->selected = (entry->selected + entry->numChoices - 1) % entry->numChoices;
            PrintEntryChoices(menuData, menuData->cursor);
            Sound_PlayEffect(SE_CONFIRM_sseq_3);
        }
    }

    if (JOY_NEW(PAD_KEY_UP) || JOY_NEW(PAD_KEY_DOWN)) {
        u16 row;
#ifdef SDK_PORT
        const OptionsMenuPage *page = &sMainPages[menuData->mainPage];
        u16 count = page->count;
        row = MainEntryRow(menuData, menuData->cursor);
        row = (row + count + (JOY_NEW(PAD_KEY_UP) ? -1 : 1)) % count;
        menuData->cursor = page->entries[row];
#else
        menuData->cursor = (menuData->cursor + MAX_ENTRIES
            + (JOY_NEW(PAD_KEY_UP) ? -1 : 1)) % MAX_ENTRIES;
        row = menuData->cursor;
#endif
        Bg_ScheduleScroll(menuData->bgConfig, BG_LAYER_MAIN_0,
            BG_OFFSET_UPDATE_SET_Y, -(row * SINGLE_ENTRY_HEIGHT + FIRST_ENTRY_OFFSET));
        PrintEntryDescription(menuData, menuData->cursor, TRUE);
        Sound_PlayEffect(SE_CONFIRM_sseq_3);
    }
}

static BOOL ChangesWereMade(OptionsMenuData *menuData)
{
#ifdef SDK_PORT
    if (menuData->isModMenu) {
        for (u32 i = 0; i < menuData->modOptionCount; i++) {
            if (!Mods_IsOptionReadOnly(menuData->modIndex, i)
                && !Mods_IsOptionAction(menuData->modIndex, i)
                && menuData->pendingModSelections[i] != menuData->initialModSelections[i]) {
                return TRUE;
            }
        }
        return FALSE;
    }
#endif

    return menuData->options.textSpeed != menuData->entries.textSpeed.selected
        || menuData->options.battleScene != menuData->entries.battleScene.selected
        || menuData->options.battleStyle != menuData->entries.battleStyle.selected
        || menuData->options.soundMode != menuData->entries.soundMode.selected
        || menuData->options.buttonMode != menuData->entries.buttonMode.selected
        || menuData->options.messageBoxStyle != menuData->entries.messageBoxStyle.selected
#ifdef SDK_PORT
        || SIM_GetInternalResolutionScale() - 1 != menuData->entries.resolutionScale.selected
        || SIM_GetConfigPtr()->screenLayout != menuData->entries.screenLayout.selected
        || SIM_GetConfigPtr()->swapScreens != (menuData->entries.swapScreens.selected == 0)
        || SIM_GetWindowSizeMode() - 1 != menuData->entries.windowSize.selected
        || SIM_GetSkipIntroMode() != menuData->entries.skipIntro.selected
        || SIM_GetVsyncEnabled() != (menuData->entries.vsync.selected == 0)
        || SIM_GetFrameRateMode() != menuData->entries.fps.selected
        || SIM_GetMonitor() - 1 != menuData->entries.monitor.selected
        || SIM_GetFpsCounterEnabled() != (menuData->entries.fpsCounter.selected == 0)
        || Options_CaptureExp(menuData->saveOptions) != (menuData->entries.captureExp.selected == 0)
        || Options_ExpShare(menuData->saveOptions) != (menuData->entries.expShare.selected == 0)
        || Options_HidePokemon(menuData->saveOptions) != (menuData->entries.hidePokemon.selected == 0)
        || SIM_GetConfigPtr()->movementSpeed != menuData->entries.movementSpeed.selected
        || SIM_GetConfigPtr()->battleSpeed != menuData->entries.battleSpeed.selected
        || SIM_GetConfigPtr()->cameraDistance - 1 != menuData->entries.cameraDistance.selected
        || FieldAutosave_IsEnabled() != (menuData->entries.autosave.selected == 0)
#endif
        ;
}

static const WindowTemplate sConfirmationWindowTemplate = {
    .bgLayer = BG_LAYER_MAIN_3,
    .tilemapLeft = 25,
    .tilemapTop = 13,
    .width = 6,
    .height = 4,
    .palette = 13,
    .baseTile = CONFIRMATION_BASE_TILE,
};

static void DrawConfirmationPrompt(OptionsMenuData *menuData)
{
    menuData->yesNoChoice = Menu_MakeYesNoChoice(menuData->bgConfig,
        &sConfirmationWindowTemplate,
        STANDARD_WINDOW_BASE_TILE,
        14,
        menuData->heapID);
}

#ifdef SDK_BUILD_ARM
static u32 ProcessConfirmationInput(OptionsMenuData *menuData)
#else
static u64 ProcessConfirmationInput(OptionsMenuData *menuData)
#endif
{
    return Menu_ProcessInputAndHandleExit(menuData->yesNoChoice, menuData->heapID);
}

static const u8 sEntryDescriptions[MAX_ENTRIES] = {
    OptionsMenu_Text_TextSpeedDescription,
    OptionsMenu_Text_SoundModeDescription,
    OptionsMenu_Text_BattleSceneDescription,
    OptionsMenu_Text_BattleStyleDescription,
    OptionsMenu_Text_ButtonModeDescription,
    OptionsMenu_Text_MessageBoxStyleDescription,
#ifdef SDK_PORT
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
#endif
    [ENTRY_CLOSE] = OptionsMenu_Text_CloseDescription,
};

static void PrintEntryDescription(OptionsMenuData *menuData, u16 entry, BOOL scheduleVRAMCopy)
{
    // Descriptions disabled
    if (IsTextPrinterDone(menuData) == FALSE) {
        Text_RemovePrinter(menuData->textPrinter);
    }
    Window_FillTilemap(&menuData->windows.description, 15);
    if (scheduleVRAMCopy) {
        Window_ScheduleCopyToVRAM(&menuData->windows.description);
    }
}
