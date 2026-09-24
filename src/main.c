#ifdef SDK_PORT
#include "port/camera_distance.h"
#endif
#include "main.h"

#include <dwc.h>

#include "constants/graphics.h"
#include "constants/heap.h"
#include "constants/versions.h"

#include "assert.h"
#include "brightness_controller.h"
#include "chatot_cry.h"
#include "comm_manager.h"
#include "communication_system.h"
#include "font.h"
#include "game_overlay.h"
#include "game_start.h"
#include "main.h"
#include "math_util.h"
#include "overlay_manager.h"
#include "play_time_manager.h"
#include "rtc.h"
#include "save_player.h"
#include "savedata.h"
#include "screen_fade.h"
#include "sound_system.h"
#include "sys_task_manager.h"
#include "system.h"
#include "timer.h"
#include "touch_pad.h"
#include "unk_0202419C.h"
#include "unk_02038FFC.h"
#include "unk_02039814.h"
#include "unk_02039A64.h"
#include "unk_0209A74C.h"

#ifdef SDK_PORT
#include <stdio.h>
#include <stdlib.h>

#include "mods/runtime.h"
#include "port/game_shortcuts.h"
#include "port/game_speed.h"
#include "simulator/sim.h"
#endif

#if defined(SDK_PORT) && (defined(__APPLE__) || defined(__linux__))
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

static void PrintCrashTrace(int signalNumber)
{
    void *frames[64];
    static const char message[] = "\n";
    write(STDERR_FILENO, message, sizeof(message) - 1);
    int count = backtrace(frames, sizeof(frames) / sizeof(frames[0]));
    backtrace_symbols_fd(frames, count, STDERR_FILENO);

    // Restore the default disposition before propagating the fatal signal.
    // Preserve the normal
    // signal termination and system crash report instead of resuming the game.
#ifdef __linux__
    signal(signalNumber, SIG_DFL);
    raise(signalNumber);
#else
    kill(getpid(), signalNumber);
#endif
}

static void InitCrashTrace(void)
{
    void *frame;
    backtrace(&frame, 1); // Initialize the unwinder before a failure occurs.
#ifdef __linux__
    signal(SIGSEGV, PrintCrashTrace);
    signal(SIGBUS, PrintCrashTrace);
    signal(SIGABRT, PrintCrashTrace);
    signal(SIGILL, PrintCrashTrace);
    signal(SIGFPE, PrintCrashTrace);
    signal(SIGTRAP, PrintCrashTrace);
#else
    struct sigaction action = { 0 };
    action.sa_handler = PrintCrashTrace;
    action.sa_flags = SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, NULL);
    sigaction(SIGBUS, &action, NULL);
    sigaction(SIGABRT, &action, NULL);
    sigaction(SIGILL, &action, NULL);
    sigaction(SIGFPE, &action, NULL);
    sigaction(SIGTRAP, &action, NULL);
#endif
}
#endif

#define RESET_COMBO (PAD_BUTTON_START | PAD_BUTTON_SELECT | PAD_BUTTON_L | PAD_BUTTON_R)

FS_EXTERN_OVERLAY(game_start);
FS_EXTERN_OVERLAY(game_opening);

typedef struct Application {
    FSOverlayID currOverlayID;
    ApplicationManager *currApplication;
    FSOverlayID nextOverlayID;
    const ApplicationManagerTemplate *nextApplication;
    ApplicationArgs args;
} Application;

static void InitApplication(void);
static void RunApplication(void);
static void WaitFrame(void);
static void TrySystemReset(enum OSResetParameter resetParam);
static void SoftReset(enum OSResetParameter resetParam);
static void HeapCanaryFailed(int resetParam, int param1);
static void CheckHeapCanary(void);

static Application sApplication;
// This variable doesn't really makes sense. If it's set to off, the game will
// repeatedly try to restore the backlight to its saved state.
static PMBackLightSwitch sSavedBacklightState;
BOOL gIgnoreCartridgeForWake;
extern const ApplicationManagerTemplate gOpeningCutsceneAppTemplate;
#ifdef SDK_PORT
extern const ApplicationManagerTemplate Unk_020F8AB4;

static u64 PerfNowNs(void)
{
    u64 counter = SDL_GetPerformanceCounter();
    u64 frequency = SDL_GetPerformanceFrequency();

    return counter / frequency * 1000000000ULL
        + counter % frequency * 1000000000ULL / frequency;
}
#endif

void NitroMain(void)
{
#if defined(SDK_PORT) && (defined(__APPLE__) || defined(__linux__))
    InitCrashTrace();
#endif
#ifdef SDK_PORT
    GameLanguage_Init();
    GameSpeed_ApplySettings();
    CameraDistance_ApplySettings();
    GameShortcuts_Init();
    Mods_Init();
#endif
    InitSystem();
    InitVRAM();
    InitKeypadAndTouchpad();

    SetGBACartridgeVersion(VERSION_NONE);
    PM_GetBackLight(&sSavedBacklightState, NULL);
    sub_0202419C();
    InitRTC();
    InitApplication();

    Fonts_Init();
    Font_InitManager(FONT_SYSTEM, HEAP_ID_APPLICATION);
    Font_InitManager(FONT_MESSAGE, HEAP_ID_APPLICATION);
    Font_InitManager(FONT_UNOWN, HEAP_ID_APPLICATION);

    sApplication.args.unk_00 = -1;
    sApplication.args.saveData = SaveData_Init();

    SoundSystem_Init(SaveData_GetChatotCry(sApplication.args.saveData), SaveData_GetOptions(sApplication.args.saveData));
    Timer_Start();

    if (sub_02038FFC(HEAP_ID_APPLICATION) == DWC_INIT_RESULT_DESTROY_OTHER_SETTING) {
        sub_02039A64(HEAP_ID_APPLICATION, 0);
    }

    if (SaveData_BackupExists(sApplication.args.saveData) == FALSE) {
        sub_0209A74C(HEAP_ID_SYSTEM);
    } else {
        switch (OS_GetResetParameter()) {
        case RESET_CLEAN:
            sApplication.args.error = FALSE;
#ifdef SDK_PORT
            switch (SIM_GetSkipIntroMode()) {
            case SIM_CONFIG_SKIP_INTRO_LOAD:
                EnqueueApplication(FS_OVERLAY_ID_NONE, &Unk_020F8AB4);
                break;
            case SIM_CONFIG_SKIP_INTRO_GAME:
                if (SaveData_LoadCheckStatus(sApplication.args.saveData) != 0) {
                    EnqueueApplication(FS_OVERLAY_ID_NONE, &Unk_020F8AB4);
                } else if (SaveData_DataExists(sApplication.args.saveData)) {
                    EnqueueApplication(FS_OVERLAY_ID(game_start), &gGameStartLoadSaveAppTemplate);
                } else {
                    EnqueueApplication(FS_OVERLAY_ID(game_start), &gGameStartRowanIntroAppTemplate);
                }
                break;
            default:
                EnqueueApplication(FS_OVERLAY_ID(game_opening), &gOpeningCutsceneAppTemplate);
                break;
            }
#else
            EnqueueApplication(FS_OVERLAY_ID(game_opening), &gOpeningCutsceneAppTemplate);
#endif
            break;

        case RESET_ERROR:
            SetScreenColorBrightness(DS_SCREEN_MAIN, COLOR_BLACK);
            SetScreenColorBrightness(DS_SCREEN_SUB, COLOR_BLACK);
            sApplication.args.error = TRUE;
            EnqueueApplication(FS_OVERLAY_ID(game_start), &gGameStartLoadSaveAppTemplate);
            break;

        default:
            GF_ASSERT(FALSE);
        }
    }

    gSystem.showTitleScreenIntro = TRUE;
    gSystem.frameCounter = 0;

    InitRNG();
    BrightnessController_ResetAllControllers();
    PlayTime_FlagNotStarted();

    gIgnoreCartridgeForWake = FALSE;

#ifdef SDK_PORT
    BOOL perfLogEnabled = getenv("LIBNTR_PERF_LOG") != NULL;
    u64 perfWindowStart = perfLogEnabled ? PerfNowNs() : 0;
    u64 perfLogicTime = 0;
    u64 perfVBlankTime = 0;
    u64 perfExtraVBlankTime = 0;
    u32 perfLoopCount = 0;
#endif

    while (TRUE) {
#ifdef SDK_PORT
        u64 perfLoopStart = perfLogEnabled ? PerfNowNs() : 0;
        u64 perfExtraWait = 0;
#endif
        CheckHeapCanary();
        HandleConsoleFold();
        ReadKeypadAndTouchpad();

#ifdef SDK_PORT
        GameShortcuts_Update();
        CameraDistance_Update();
        Mods_Dispatch(MOD_EVENT_FRAME, NULL);
#endif

        if ((gSystem.heldKeysRaw & RESET_COMBO) == RESET_COMBO && !gSystem.inhibitReset) {
            SoftReset(RESET_CLEAN);
        }

        if (CommSys_Update()) {
            CheckHeapCanary();
            RunApplication();
            SysTaskManager_ExecuteTasks(gSystem.mainTaskMgr);
            SysTaskManager_ExecuteTasks(gSystem.printTaskMgr);

            if (
#ifdef SDK_PORT
                GameSpeed_IsFixed30Hz() || (!gSystem.frameCounter && SIM_GetFrameRateMode() == SIM_CONFIG_FRAME_RATE_30)
#else
                !gSystem.frameCounter
#endif
            ) {
#ifdef SDK_PORT
                u64 perfWaitStart = perfLogEnabled ? PerfNowNs() : 0;
#endif
                OS_WaitIrq(TRUE, OS_IE_V_BLANK);
#ifdef SDK_PORT
                if (perfLogEnabled) {
                    perfExtraWait += PerfNowNs() - perfWaitStart;
                }
#endif
                gSystem.vblankCounter++;
            }
        }

        UpdateRTC();
        PlayTime_IncrementTimer();
        sub_020241CC();
        SysTaskManager_ExecuteTasks(gSystem.printTaskMgr);

#ifdef SDK_PORT
        u64 perfVBlankStart = perfLogEnabled ? PerfNowNs() : 0;
#endif
        OS_WaitIrq(TRUE, OS_IE_V_BLANK);
#ifdef SDK_PORT
        u64 perfVBlankEnd = perfLogEnabled ? PerfNowNs() : 0;
#endif

        gSystem.vblankCounter++;
        gSystem.frameCounter = 0;

        BrightnessController_Update();
        ExecScreenFade();

        if (gSystem.vblankCallback != NULL) {
            gSystem.vblankCallback(gSystem.vblankCallbackData);
        }

        SoundSystem_Tick();
        SysTaskManager_ExecuteTasks(gSystem.postVBlankTaskMgr);

#ifdef SDK_PORT
        if (perfLogEnabled) {
            u64 perfLoopEnd = PerfNowNs();
            u64 vblankTime = perfVBlankEnd - perfVBlankStart;
            perfVBlankTime += vblankTime;
            perfExtraVBlankTime += perfExtraWait;
            perfLogicTime += perfLoopEnd - perfLoopStart - vblankTime - perfExtraWait;
            perfLoopCount++;

            u64 perfWindowDuration = perfLoopEnd - perfWindowStart;
            if (perfWindowDuration >= 1000000000ULL) {
                fprintf(stderr,
                        "[game] %.1f loops/s, logic %.3f ms, vblank %.3f ms, extra-vblank %.3f ms\n",
                        (double)perfLoopCount * 1000000000.0 / (double)perfWindowDuration,
                        (double)perfLogicTime / (double)perfLoopCount / 1000000.0,
                        (double)perfVBlankTime / (double)perfLoopCount / 1000000.0,
                        (double)perfExtraVBlankTime / (double)perfLoopCount / 1000000.0);
                perfWindowStart = perfLoopEnd;
                perfLogicTime = 0;
                perfVBlankTime = 0;
                perfExtraVBlankTime = 0;
                perfLoopCount = 0;
            }
        }
#endif
    }
}

static void InitApplication()
{
    sApplication.currOverlayID = FS_OVERLAY_ID_NONE;
    sApplication.currApplication = NULL;
    sApplication.nextOverlayID = FS_OVERLAY_ID_NONE;
    sApplication.nextApplication = NULL;
}

static void RunApplication(void)
{
    if (sApplication.currApplication == NULL) {
        if (sApplication.nextApplication == NULL) {
            return;
        }

        if (sApplication.nextOverlayID != FS_OVERLAY_ID_NONE) {
            Overlay_LoadByID(sApplication.nextOverlayID, OVERLAY_LOAD_NORMAL);
        }

        sApplication.currOverlayID = sApplication.nextOverlayID;
        sApplication.currApplication = ApplicationManager_New(sApplication.nextApplication, &sApplication.args, HEAP_ID_SYSTEM);
        sApplication.nextOverlayID = FS_OVERLAY_ID_NONE;
        sApplication.nextApplication = NULL;
    }

    if (ApplicationManager_Exec(sApplication.currApplication)) {
        ApplicationManager_Free(sApplication.currApplication);
        sApplication.currApplication = NULL;

        if (sApplication.currOverlayID != FS_OVERLAY_ID_NONE) {
            Overlay_UnloadByID(sApplication.currOverlayID);
        }
    }
}

void EnqueueApplication(FSOverlayID overlayID, const ApplicationManagerTemplate *template)
{
    GF_ASSERT(sApplication.nextApplication == NULL);

    sApplication.nextOverlayID = overlayID;
    sApplication.nextApplication = template;
}

static void WaitFrame(void)
{
    CommSys_Update();

    OS_WaitIrq(TRUE, OS_IE_V_BLANK);

    gSystem.vblankCounter++;
    gSystem.frameCounter = 0;

    if (gSystem.vblankCallback != NULL) {
        gSystem.vblankCallback(gSystem.vblankCallbackData);
    }
}

static void TrySystemReset(enum OSResetParameter resetParam)
{
    if (CommManager_CheckResetFinished() && CARD_TryWaitBackupAsync() == TRUE) {
        OS_ResetSystem(resetParam);
    }

    WaitFrame();
}

static void CheckHeapCanary(void)
{
    int v0 = CommManager_GetResetType();

    switch (v0) {
    case 1:
        HeapCanaryFailed(1, v0);
        break;
    case 2:
        HeapCanaryFailed(0, v0);
        break;
    case 3:
        HeapCanaryFailed(1, v0);
        break;
    }
}

static void SoftReset(enum OSResetParameter resetParam)
{
    SetScreenColorBrightness(DS_SCREEN_MAIN, COLOR_WHITE);
    SetScreenColorBrightness(DS_SCREEN_SUB, COLOR_WHITE);

    if (CommManager_ExitOrReset()) {
        SaveData_SaveStateCancel(SaveData_Ptr());
    }

    while (TRUE) {
        HandleConsoleFold();
        TrySystemReset(resetParam);
    }
}

static void HeapCanaryFailed(int resetParam, int param1)
{
    int elapsed;

    if (param1 == 3) {
        NetworkError_DisplayNetworkError(HEAP_ID_SYSTEM, 3, 0);
    } else if (resetParam == RESET_CLEAN) {
        if (CommManager_IsConnectedToWifi() == TRUE) {
            NetworkError_DisplayNetworkError(HEAP_ID_SYSTEM, 6, 0);
        } else {
            NetworkError_DisplayNetworkError(HEAP_ID_SYSTEM, 2, 0);
        }
    } else {
        if (CommManager_IsConnectedToWifi() == TRUE) {
            NetworkError_DisplayNetworkError(HEAP_ID_SYSTEM, 5, 0);
        } else {
            NetworkError_DisplayNetworkError(HEAP_ID_SYSTEM, 0, 0);
        }
    }

    CommManager_ExitOrReset();
    WaitFrame();
    SoundSystem_Tick();

    elapsed = 0;

    while (TRUE) {
        HandleConsoleFold();
        ReadKeypadAndTouchpad();

        if (elapsed >= 30 && gSystem.pressedKeys & PAD_BUTTON_A) {
            break;
        }

        WaitFrame();

        if (elapsed < 30) {
            elapsed++;
        }
    }

    SoftReset(resetParam);
}

void InitRNG(void)
{
    RTCDate date;
    RTCTime time;
    GetCurrentDateTime(&date, &time);

    u32 seed = date.year + date.month * 0x100 * date.day * 0x10000 + time.hour * 0x10000 + (time.minute + time.second) * 0x1000000 + gSystem.vblankCounter;

    MTRNG_SetSeed(seed);
    LCRNG_SetSeed(seed);
}

void HandleConsoleFold(void)
{
    PMBackLightSwitch top, bottom;
    PMWakeUpTrigger trigger;

    if (PAD_DetectFold()) {
        if (gSystem.inhibitSleep == 0) {
            BeforeSleep();

            if (CTRDG_IsPulledOut() == TRUE) {
                gIgnoreCartridgeForWake = TRUE;
            }

sleep_again:
            trigger = PM_TRIGGER_COVER_OPEN | PM_TRIGGER_CARD;

            if (gSystem.gbaCartridgeVersion && !gIgnoreCartridgeForWake) {
                trigger |= PM_TRIGGER_CARTRIDGE;
            }

            PM_GoSleepMode(trigger, 0, 0);

            if (CARD_IsPulledOut()) {
                PM_ForceToPowerOff();
            } else if (PAD_DetectFold()) {
                // Woke up because the cartridge got pulled out
                gIgnoreCartridgeForWake = TRUE;
                goto sleep_again;
            }

            AfterSleep();
        } else {
            PM_GetBackLight(&top, &bottom);

            if (top == PM_BACKLIGHT_ON) {
                PM_SetBackLight(PM_LCD_ALL, PM_BACKLIGHT_OFF);
            }
        }
    } else {
        PM_GetBackLight(&top, &bottom);

        if (top == PM_BACKLIGHT_OFF) {
            PM_SetBackLight(PM_LCD_ALL, sSavedBacklightState);
        }
    }
}
