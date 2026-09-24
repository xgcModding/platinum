#ifndef POKEPLATINUM_PORT_AUTOSAVE_H
#define POKEPLATINUM_PORT_AUTOSAVE_H

#include <nitro/types.h>

#include "field/field_system_decl.h"

enum FieldAutosaveReason {
    FIELD_AUTOSAVE_BATTLE = 1 << 0,
    FIELD_AUTOSAVE_POKECENTER = 1 << 1,
    FIELD_AUTOSAVE_ZONE = 1 << 2,
    FIELD_AUTOSAVE_PC = 1 << 3,
};

void FieldAutosave_Init(FieldSystem *fieldSystem);
BOOL FieldAutosave_IsEnabled(void);
void FieldAutosave_SetEnabled(BOOL enabled);
void FieldAutosave_Request(FieldSystem *fieldSystem, enum FieldAutosaveReason reason);
void FieldAutosave_Update(FieldSystem *fieldSystem);

#endif
