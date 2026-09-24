#include <nitro.h>
#include <stdio.h>

#include "constants/field/map_load.h"
#include "constants/player_avatar.h"
#include "field/field_system.h"
#include "overlay005/save_info_window.h"

#include "comm_manager.h"
#include "field_system.h"
#include "field_task.h"
#include "player_avatar.h"
#include "port/autosave.h"
#include <simulator/sim.h>
#include <simulator/config/sim_config.h>
#include "savedata.h"
#include "system_flags.h"
#include "unk_0206B9D8.h"
#include "vars_flags.h"

#define AUTOSAVE_RETRY_FRAMES 300

static FieldSystem *sFieldSystem;
static u32 sPendingReasons;
static int sMapHeaderID;
static BOOL sMapKnown;
static int sRetryFrames;

BOOL FieldAutosave_IsEnabled(void)
{
    return SIM_GetConfigPtr()->autosaveEnabled;
}

void FieldAutosave_SetEnabled(BOOL enabled)
{
    SIM_config_type *config = SIM_GetConfigPtr();
    if (config->autosaveEnabled == enabled) {
        return;
    }
    config->autosaveEnabled = enabled;
    sPendingReasons = 0;
    sRetryFrames = 0;
    // Persist independently of the save game, including when disabling saves.
    SIM_Config_SaveGameConfigFile(config);
}

void FieldAutosave_Init(FieldSystem *fieldSystem)
{
    sFieldSystem = fieldSystem;
    sPendingReasons = 0;
    sMapHeaderID = 0;
    sMapKnown = FALSE;
    sRetryFrames = 0;
}

void FieldAutosave_Request(FieldSystem *fieldSystem, enum FieldAutosaveReason reason)
{
    if (fieldSystem != NULL && fieldSystem == sFieldSystem && FieldAutosave_IsEnabled()) {
        // Coalesce an encounter, recovery and warp into one complete save.
        sPendingReasons |= reason;
    }
}

void FieldAutosave_Update(FieldSystem *fieldSystem)
{
    if (fieldSystem == NULL || fieldSystem != sFieldSystem
        || fieldSystem->location == NULL || fieldSystem->playerAvatar == NULL
        || fieldSystem->mapObjMan == NULL
        || fieldSystem->processManager->pause || fieldSystem->processManager->kill
        || FieldSystem_IsRunningTask(fieldSystem)
        || !FieldSystem_IsRunningFieldMap(fieldSystem)) {
        return;
    }

    int mapHeaderID = fieldSystem->location->mapHeaderID;
    if (sMapKnown && mapHeaderID != sMapHeaderID) {
        sPendingReasons |= FIELD_AUTOSAVE_ZONE;
    }
    // Loading a save is not itself a zone change.
    sMapKnown = TRUE;
    sMapHeaderID = mapHeaderID;

    if (!FieldAutosave_IsEnabled()) {
        sPendingReasons = 0;
        sRetryFrames = 0;
        return;
    }

    if (sPendingReasons == 0) {
        return;
    }
    if (sRetryFrames > 0) {
        sRetryFrames--;
        return;
    }

    int movement = PlayerAvatar_GetPlayerMoveState(fieldSystem->playerAvatar);
    if (movement != PLAYER_MOVE_STATE_NONE && movement != PLAYER_MOVE_STATE_END) {
        return;
    }

    VarsFlags *varsFlags = SaveData_GetVarsFlags(fieldSystem->saveData);
    if (fieldSystem->mapLoadType != MAP_LOAD_TYPE_OVERWORLD
        || CommManager_IsInitializedNotPoketch()
        || SystemFlag_CheckSafariGameActive(varsFlags)
        || SystemFlag_CheckInPalPark(varsFlags)
        || FieldSystem_IsInBattleTowerSalon(fieldSystem)
        || SaveData_OverwriteCheck(fieldSystem->saveData)) {
        // Keep the request until ordinary saving is available. Never silently
        // replace another playthrough after selecting New Game.
        return;
    }

    // This prepares the current objects and player location and performs the
    // normal save, without constructing any of the manual-save UI.
    if (FieldSystem_Save(fieldSystem)) {
        sPendingReasons = 0;
    } else {
        sRetryFrames = AUTOSAVE_RETRY_FRAMES;
    }
}
