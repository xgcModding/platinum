#ifndef POKEPLATINUM_PORT_CAMERA_DISTANCE_H
#define POKEPLATINUM_PORT_CAMERA_DISTANCE_H

#include <simulator/config/sim_config.h>
#include "constants/field/map_matrix.h"

#define CAMERA_DISTANCE_MAX_RADIUS (MAP_MATRIX_MAX_WIDTH - 1)
#define CAMERA_DISTANCE_VIEW_DIAMETER (2 * CAMERA_DISTANCE_MAX_RADIUS + 1)
#define CAMERA_DISTANCE_MAX_CHUNKS MAP_MATRIX_MAX_SIZE

#include "field/field_system_decl.h"
#include "overlay005/land_data.h"
#include "overlay005/model_attributes.h"

BOOL CameraDistance_GetObjectHeight(const FieldSystem *fieldSystem, int mapMatrixIndex,
    fx32 currentHeight, fx32 localX, fx32 localZ, fx32 *height);
BOOL CameraDistance_GetTileAttributes(const FieldSystem *fieldSystem, int tileX, int tileZ, u16 *attributes);
float CameraDistance_AdjustFogDepth(float boundary);
void CameraDistance_ApplySettings(void);
void CameraDistance_RequestStep(int step);
void CameraDistance_Update(void);
void CameraDistance_OnCreate(FieldSystem *fieldSystem);
void CameraDistance_OnDelete(FieldSystem *fieldSystem);
BOOL CameraDistance_FilterObjectEvents(FieldSystem *fieldSystem, BOOL changed);
BOOL CameraDistance_ShouldRenderMap(u8 index, const LandDataManager *landDataMan, ModelAttributes *modelAttrs);
void CameraDistance_PrepareFrame(const LandDataManager *landDataMan);
void CameraDistance_RenderExtra(const LandDataManager *landDataMan, ModelAttributes *modelAttrs);
void CameraDistance_BeforeLandEnd(LandDataManager *landDataMan);

#endif
