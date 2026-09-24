#include "port/game_speed.h"
#include <stdlib.h>
#include "constants/battle/battle_controller.h"
#include "generated/movement_actions.h"
#include "map_object.h"
#include "constdata/const_020EA358.h"
#include <stdint.h>
#include "simulator/sim.h"
#include "simulator/config/sim_config.h"

typedef struct MovementTiming {
    void *mapObj;
    u32 remainder;
    struct MovementTiming *next;
} MovementTiming;

static BOOL sFixed30Hz;

void GameSpeed_SetFixed30Hz(BOOL enabled)
{
    sFixed30Hz = enabled;
}

BOOL GameSpeed_IsFixed30Hz(void)
{
    return sFixed30Hz;
}

static MovementTiming *sMovementTimings;
static ApplicationManager *sBattleApplicationManager;
static void *sBattleSystem;
static int sBattleActive;
static unsigned int sPublishedBattleUnits = 2;
static u32 sMovementSpeedUnits = 2;
static u32 sBattleSpeedUnits = 2;
static u32 sSoundAccumulator;
static int sDrawingMovingObject;
static int sBattlePhase;
static u32 sBattleMenuCount;
#define BATTLE_PHASE_LAST_ACCELERATED 12

static int IsTravelMovementAction(int movementAction)
{
    if (movementAction >= MOVEMENT_ACTION_WALK_SLOWER_NORTH
        && movementAction <= MOVEMENT_ACTION_WALK_FASTER_EAST) {
        return 1;
    }

    if (movementAction >= MOVEMENT_ACTION_WALK_SLIGHTLY_FAST_NORTH
        && movementAction <= MOVEMENT_ACTION_RUN_EAST) {
        return 1;
    }

    return movementAction >= MOVEMENT_ACTION_WALK_EVER_SO_SLIGHTLY_FAST_NORTH
        && movementAction <= MOVEMENT_ACTION_WALK_EVER_SO_SLIGHTLY_FAST_EAST;
}

static void ClearMovementTimings(void)
{
    while (sMovementTimings != NULL) {
        MovementTiming *timing = sMovementTimings;
        sMovementTimings = timing->next;
        free(timing);
    }
}

static MovementTiming *GetMovementTiming(void *mapObj)
{
    for (MovementTiming *timing = sMovementTimings; timing != NULL; timing = timing->next) {
        if (timing->mapObj == mapObj) {
            return timing;
        }
    }

    MovementTiming *timing = malloc(sizeof(*timing));
    if (timing != NULL) {
        timing->mapObj = mapObj;
        timing->remainder = 0;
        timing->next = sMovementTimings;
        sMovementTimings = timing;
    }
    return timing;
}

void GameSpeed_DeleteObject(void *mapObj)
{
    for (MovementTiming **entry = &sMovementTimings; *entry != NULL; entry = &(*entry)->next) {
        if ((*entry)->mapObj == mapObj) {
            MovementTiming *timing = *entry;
            *entry = timing->next;
            free(timing);
            break;
        }
    }

}

int GameSpeed_ScaleMovementDuration(int frames, u32 *remainder)
{
    u32 numerator = frames * 2 + (remainder != NULL ? *remainder : 0);
    int duration = numerator / sMovementSpeedUnits;
    if (remainder != NULL) {
        *remainder = numerator % sMovementSpeedUnits;
    }
    if (duration < 1) {
        duration = 1;
        if (remainder != NULL) {
            *remainder = 0;
        }
    }
    return duration;
}

void GameSpeed_ScaleWalk(void *mapObj, fx32 *distanceOut, s16 *durationOut)
{
    fx32 distance = *distanceOut;
    s16 duration = *durationOut;
    if (sMovementSpeedUnits > 2
        && duration > 0
        && IsTravelMovementAction(MapObject_GetMovementAction(mapObj))) {
        // Keep fractional step durations independent of other characters' updates.
        MovementTiming *timing = GetMovementTiming(mapObj);
        int64_t totalDistance = (int64_t)distance * duration;
        int scaledDuration = GameSpeed_ScaleMovementDuration(duration,
            timing != NULL ? &timing->remainder : NULL);

        distance = (int32_t)((totalDistance + scaledDuration / 2) / scaledDuration);
        duration = scaledDuration;
    }

    *distanceOut = distance;
    *durationOut = duration;
}

static void ResetBattleSoundAccumulator(void)
{
    sSoundAccumulator = sBattleSpeedUnits - 2;
}

static void UpdateBattleTimingMode(void)
{
    int shouldAccelerate = sBattleApplicationManager != NULL
        && sBattlePhase <= BATTLE_PHASE_LAST_ACCELERATED
        && sBattleMenuCount == 0
        && sBattleSpeedUnits > 2;

    if (sBattleActive != shouldAccelerate) {
        sBattleActive = shouldAccelerate;
        ResetBattleSoundAccumulator();
    }
    __atomic_store_n(&sPublishedBattleUnits, shouldAccelerate ? sBattleSpeedUnits : 2, __ATOMIC_RELEASE);
}

void GameSpeed_MenuOpened(void *battleSys)
{
    if (battleSys == NULL || battleSys != sBattleSystem) {
        return;
    }

    sBattleMenuCount++;
    UpdateBattleTimingMode();
}

static int IsInteractiveBattleCommand(int command)
{
    return (command >= BATTLE_COMMAND_SET_COMMAND_SELECTION
            && command <= BATTLE_COMMAND_SHOW_YES_NO_MENU)
        || command == BATTLE_COMMAND_FORGET_MOVE;
}


void GameSpeed_ApplySettings(void)
{
    sMovementSpeedUnits = SIM_GetConfigPtr()->movementSpeed + 2;
    sBattleSpeedUnits = SIM_GetConfigPtr()->battleSpeed + 2;
    ClearMovementTimings();
    UpdateBattleTimingMode();
}

int GameSpeed_BeginDraw(void *mapObj)
{
    int previous = sDrawingMovingObject;
    sDrawingMovingObject = IsTravelMovementAction(MapObject_GetMovementAction(mapObj));
    return previous;
}

void GameSpeed_EndDraw(int previous) { sDrawingMovingObject = previous; }

fx32 GameSpeed_AnimationFrames(fx32 frames)
{
    return sDrawingMovingObject && frames > 0
        ? (fx32)((int64_t)frames * sMovementSpeedUnits / 2) : frames;
}

unsigned int GameSpeed_BattleUnits(void)
{
    return __atomic_load_n(&sPublishedBattleUnits, __ATOMIC_ACQUIRE);
}

BOOL GameSpeed_TickSound(void)
{
    if (!sBattleActive) return TRUE;
    sSoundAccumulator += 2;
    if (sSoundAccumulator < sBattleSpeedUnits) return FALSE;
    sSoundAccumulator -= sBattleSpeedUnits;
    return TRUE;
}

void GameSpeed_CommandCleared(void *battleSys, int command)
{
    if (battleSys == sBattleSystem && IsInteractiveBattleCommand(command) && sBattleMenuCount > 0) {
        sBattleMenuCount--;
        UpdateBattleTimingMode();
    }
}

void GameSpeed_Application(ApplicationManager *appMan, BOOL finished)
{
    if (appMan->template.init != gBattleApplicationTemplate.init
        || appMan->template.main != gBattleApplicationTemplate.main
        || appMan->template.exit != gBattleApplicationTemplate.exit
        || appMan->template.overlayID != gBattleApplicationTemplate.overlayID) return;
    if (sBattleApplicationManager != appMan) sBattleMenuCount = 0;
    sBattleApplicationManager = finished ? NULL : appMan;
    sBattleSystem = finished ? NULL : appMan->data;
    sBattlePhase = appMan->procState;
    if (finished) sBattleMenuCount = 0;
    UpdateBattleTimingMode();
}
