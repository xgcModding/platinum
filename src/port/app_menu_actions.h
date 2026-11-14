#ifndef LAKE_TRIO_APP_MENU_ACTIONS_H
#define LAKE_TRIO_APP_MENU_ACTIONS_H

/*
 * Shared native-menu action IDs.
 * libntr's DrawSimulatorMenuBar currently consumes these integer codes.
 * Keep the numeric values stable until that dispatch is migrated to this API.
 */
enum LakeTrioMenuAction {
    LAKE_TRIO_MENU_NONE = 0,
    LAKE_TRIO_MENU_IMPORT_ROM = 1,
    LAKE_TRIO_MENU_CONFIGURE_CONTROLS = 2
};

#ifdef __cplusplus
extern "C" {
#endif

/* Queue an existing File/Input command for the simulator's next UI update. */
void PlatformMenu_RequestAction(int action);

/* Called once per GUI frame by the simulator. Returns and clears one action. */
int PlatformMenu_Update(int canImport);

#ifdef __cplusplus
}
#endif

#endif /* LAKE_TRIO_APP_MENU_ACTIONS_H */
