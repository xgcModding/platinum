#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <nitro.h>
#include <nnsys.h>

#include "constants/field/map.h"
#include "constants/heap.h"
#include "constants/narc.h"
#include "field/field_system.h"
#include "generated/map_headers.h"
#include "overlay005/area_data.h"
#include "overlay005/bdhc.h"
#include "overlay005/land_data.h"
#include "overlay005/map_prop.h"
#include "overlay005/map_prop_animation.h"
#include "overlay005/model_attributes.h"
#include "easy3d.h"
#include "fx_util.h"
#include "map_header_data.h"
#include "map_matrix.h"
#include "map_object.h"
#include "narc.h"
#include "port/camera_distance.h"
#include "simulator/config/sim_config.h"
#include "simulator/sim.h"
#include "simulator/g3_draw.h"
#include "camera.h"
#include "heap.h"
#include "field_task.h"
#include "map_header.h"
#include "player_avatar.h"
#include "overlay005/area_light.h"
#include <stdio.h>

#define DEFAULT_ZOOM_MULTIPLIER 2
#define BASE_LOADED_CHUNK_COUNT 4
#define MAX_EXTRA_CHUNK_RADIUS CAMERA_DISTANCE_MAX_RADIUS
#define MAX_EXTRA_CHUNKS CAMERA_DISTANCE_MAX_CHUNKS
#define MAX_EXTRA_AREA_MANAGERS MAX_EXTRA_CHUNKS
#define INVALID_MAP_MATRIX_INDEX -1
#define INVALID_LAND_DATA_ID 0xFFFF

typedef struct ExtraChunk {
    int mapMatrixIndex;
    int landDataID;
    NNSG3dRenderObj renderObj;
    NNSG3dResFileHeader *modelFile;
    MapPropManager *mapPropManager;
    NNSG3dResTex *mapTexture;
    AreaDataManager *areaDataManager;
    int usesCurrentArea;
    BDHC bdhc;
    u8 *bdhcBuffer;
    u16 *terrainAttributes;
} ExtraChunk;

typedef struct ExtraAreaManager {
    int areaDataArchiveID;
    NNSG3dResFileHeader *mapTextureFile;
    NNSG3dResTex *mapTexture;
    NNSG3dResFileHeader *mapPropTextureFile;
    AreaDataManager *areaDataManager;
} ExtraAreaManager;

typedef struct LandDataFileHeader {
    int terrainAttributesSize;
    int mapPropsSize;
    int mapModelSize;
    int bdhcSize;
} LandDataFileHeader;

static void ApplyZoom(void);
static void ApplyClipping(uint32_t radius);

static Camera *sFieldCamera;
static FieldSystem *sFieldSystem;
static NARC *sExtraLandDataNarc;
static ExtraChunk sExtraChunks[MAX_EXTRA_CHUNKS];
static uint32_t sExtraChunkCount;
static ExtraAreaManager sExtraAreaManagers[MAX_EXTRA_AREA_MANAGERS];
static uint32_t sExtraAreaManagerCount;
static LandDataManager *sCachedLandDataMan;
static int sCachedCenterX = -1;
static int sCachedCenterZ = -1;
static uint32_t sCachedRadius;
static int sCachedLoadedIndexes[BASE_LOADED_CHUNK_COUNT] = { -1, -1, -1, -1 };
static uint16_t sBaseFovY;
static fx32 sBaseNearClip;
static fx32 sBaseFarClip;
static uint32_t sZoomMultiplier = DEFAULT_ZOOM_MULTIPLIER;
static NNSG3dResFileHeader *sCurrentPropTextureBackupFile;
static NNSG3dResTex *sCurrentPropTextureBackup;
static NNSG3dResTex *sOriginalCurrentPropTexture;
static NNSG3dTexKey sTextureSwapKey;
static NNSG3dTexKey sTextureSwap4x4Key;
static NNSG3dPlttKey sPaletteSwapKey;
static NNSG3dResTex *sActiveSwappedTexture;
static BOOL sReportedDuplicateRemoval;
static int sRequestedSteps;

void CameraDistance_RequestStep(int step)
{
    if (step == -1 || step == 1) {
        __atomic_fetch_add(&sRequestedSteps, step, __ATOMIC_RELEASE);
    }
}

static BOOL ObjectEventsHaveSameSource(const ObjectEvent *left, const ObjectEvent *right)
{
    return left->graphicsID == right->graphicsID
        && left->x == right->x
        && left->z == right->z;
}

BOOL CameraDistance_FilterObjectEvents(FieldSystem *fieldSystem, BOOL changed)
{
    uint32_t removed = 0;

    // Refresh runs for every active field frame and is the authoritative owner
    // of the object manager used by the alias cleanup.
    sFieldSystem = fieldSystem;

    if (!changed || sZoomMultiplier == 1 || fieldSystem == NULL || fieldSystem->mapHeaderData == NULL) {
        return changed;
    }

    MapHeaderData *data = fieldSystem->mapHeaderData;
    if (data->objectEvents != data->visibleObjectEvents) {
        return changed;
    }

    uint32_t index = data->numLocalObjectEvents;
    while (index < data->numObjectEvents) {
        BOOL duplicate = FALSE;
        for (uint32_t previous = 0; previous < index; previous++) {
            if (ObjectEventsHaveSameSource(
                    &data->visibleObjectEvents[index],
                    &data->visibleObjectEvents[previous])) {
                duplicate = TRUE;
                break;
            }
        }

        if (!duplicate) {
            index++;
            continue;
        }

        data->numObjectEvents--;
        memmove(
            &data->visibleObjectEvents[index],
            &data->visibleObjectEvents[index + 1],
            (data->numObjectEvents - index) * sizeof(ObjectEvent));
        removed++;
    }

    if (removed > 0 && !sReportedDuplicateRemoval) {
        fprintf(stderr, "[camera] Alias duplicados de NPC eliminados\n");
        sReportedDuplicateRemoval = TRUE;
    }

    return changed;
}

void CameraDistance_Update(void)
{
    int steps = __atomic_exchange_n(&sRequestedSteps, 0, __ATOMIC_ACQ_REL);
    if (steps != 0 && sFieldSystem != NULL && sFieldCamera != NULL
        && FieldSystem_IsRunningFieldMap(sFieldSystem) && !FieldSystem_IsRunningTask(sFieldSystem)) {
        int distance = (int)SIM_GetConfigPtr()->cameraDistance + steps;
        distance = distance < 1 ? 1 : (distance > SIM_CONFIG_CAMERA_DISTANCE_MAX ? SIM_CONFIG_CAMERA_DISTANCE_MAX : distance);
        if (distance != SIM_GetConfigPtr()->cameraDistance) {
            SIM_GetConfigPtr()->cameraDistance = distance;
            CameraDistance_ApplySettings();
            SIM_Config_SaveGameConfigFile(SIM_GetConfigPtr());
        }
    }

    MapObject *objects[64];
    MapObject *duplicates[64];
    int objectCount = 0;
    int duplicateCount = 0;
    int startIndex = 0;

    if (sZoomMultiplier == 1 || sFieldSystem == NULL || sFieldSystem->mapObjMan == NULL) {
        return;
    }

    while (objectCount < 64
        && MapObjectMan_FindObjectWithStatus(sFieldSystem->mapObjMan, &objects[objectCount], &startIndex, MAP_OBJ_STATUS_0)) {
        objectCount++;
    }

    for (int index = 0; index < objectCount; index++) {
        MapObject *alias = objects[index];
        if (!MapObject_HasNoScript(alias)) {
            continue;
        }

        for (int previous = 0; previous < objectCount; previous++) {
            MapObject *other = objects[previous];
            if (other == alias
                || MapObject_GetGraphicsID(other) != MapObject_GetGraphicsID(alias)
                || MapObject_GetXInitial(other) != MapObject_GetXInitial(alias)
                || MapObject_GetZInitial(other) != MapObject_GetZInitial(alias)) {
                continue;
            }

            if (!MapObject_HasNoScript(other) || previous < index) {
                duplicates[duplicateCount++] = alias;
                break;
            }
        }
    }

    for (int index = 0; index < duplicateCount; index++) {
        MapObject_Delete(duplicates[index]);
    }

    if (duplicateCount > 0 && !sReportedDuplicateRemoval) {
        fprintf(stderr, "[camera] Alias duplicados de NPC eliminados\n");
        sReportedDuplicateRemoval = TRUE;
    }
}

static Camera *FieldSystemCamera(FieldSystem *fieldSystem)
{
    if (fieldSystem == NULL) {
        return NULL;
    }

    return fieldSystem->camera;
}

// Extra scenery is owned by the PC cache, not the DS field heap. Loading
// several neighboring areas into HEAP_ID_FIELD1 can exhaust it before the
// native map finishes allocating its own resources.
static void *ReadExtraResource(enum NarcID narcID, int memberIndex)
{
    u32 size = NARC_GetMemberSizeByIndexPair(narcID, memberIndex);
    void *resource = size != 0 ? malloc(size) : NULL;
    if (resource != NULL) {
        NARC_ReadWholeMemberByIndexPair(resource, narcID, memberIndex);
    }
    return resource;
}

static void ClearExtraAreaManagers(void)
{
    for (uint32_t i = 0; i < sExtraAreaManagerCount; i++) {
        AreaDataManager *area = sExtraAreaManagers[i].areaDataManager;
        if (area != NULL) {
            for (uint32_t modelID = 0; modelID < MAX_MAP_PROP_MODEL_FILES; modelID++) {
                if (area->mapPropModelFiles[modelID] != NULL) {
                    free(area->mapPropModelFiles[modelID]);
                }
            }
            if (area->mapPropModelIDs != NULL) {
                free(area->mapPropModelIDs);
            }
            free(area);
        }
        if (sExtraAreaManagers[i].mapTextureFile != NULL) {
            free(sExtraAreaManagers[i].mapTextureFile);
        }
        if (sExtraAreaManagers[i].mapPropTextureFile != NULL) {
            free(sExtraAreaManagers[i].mapPropTextureFile);
        }
        sExtraAreaManagers[i].areaDataArchiveID = -1;
        sExtraAreaManagers[i].mapTextureFile = NULL;
        sExtraAreaManagers[i].mapTexture = NULL;
        sExtraAreaManagers[i].mapPropTextureFile = NULL;
        sExtraAreaManagers[i].areaDataManager = NULL;
    }

    sExtraAreaManagerCount = 0;
}

static void SetSwappedTextureKeys(NNSG3dResTex *texture)
{
    texture->texInfo.flag &= ~NNS_G3D_RESTEX_LOADED;
    texture->tex4x4Info.flag &= ~NNS_G3D_RESTEX4x4_LOADED;
    texture->plttInfo.flag &= ~NNS_G3D_RESPLTT_LOADED;
    NNS_G3dTexSetTexKey(texture, sTextureSwapKey, sTextureSwap4x4Key);
    NNS_G3dPlttSetPlttKey(texture, sPaletteSwapKey);
}

static int ActivateSwappedTexture(NNSG3dResTex *texture)
{
    if (texture == NULL || sTextureSwapKey == 0 || sPaletteSwapKey == 0
        || NNS_G3dTexGetRequiredSize(texture) > NNS_GfdGetTexKeySize(sTextureSwapKey)
        || NNS_G3dTex4x4GetRequiredSize(texture) > NNS_GfdGetTexKeySize(sTextureSwap4x4Key)
        || NNS_G3dPlttGetRequiredSize(texture) > NNS_GfdGetPlttKeySize(sPaletteSwapKey)) {
        return 0;
    }

    if (texture == sActiveSwappedTexture) {
        return 1;
    }
    NNS_G3dGeFlushBuffer();
    // Sending Nintendo DS commands does not drain the host's vertex batch.
    // That batch must sample the old VRAM contents before we replace them.
    G3SIM_FlushArray();
    SetSwappedTextureKeys(texture);
    NNS_G3dTexLoad(texture, TRUE);
    NNS_G3dPlttLoad(texture, TRUE);
    sActiveSwappedTexture = texture;
    return 1;
}

static void RebindSwappedModel(NNSG3dResFileHeader *file, NNSG3dResTex *texture)
{
    NNS_G3dReleaseMdlSet(NNS_G3dGetMdlSet(file));
    Easy3D_BindTextureToResource(file, texture);
}

static void RebindAreaPropModels(AreaDataManager *area, NNSG3dResTex *texture)
{
    // AreaDataManager_Load binds every model in this list to the area's
    // shared texture, even when the model also contains a TEX0 block.
    // The separately loaded dummy model owns its texture and is not listed.
    for (uint32_t i = 0; i < area->mapPropModelIDs[0]; i++) {
        uint16_t modelID = area->mapPropModelIDs[i + 1];
        if (modelID < MAX_MAP_PROP_MODEL_FILES && area->mapPropModelFiles[modelID] != NULL) {
            RebindSwappedModel(area->mapPropModelFiles[modelID], texture);
        }
    }
}

static void RebindSwappedModels(void)
{
    // Material bindings contain VRAM addresses. Refresh every user of the
    // shared slot after it moves, including areas already in the chunk cache.
    // Existing texture animations still reference the original resource.
    SetSwappedTextureKeys(sOriginalCurrentPropTexture);
    for (uint32_t i = 0; i <= sExtraAreaManagerCount; i++) {
        AreaDataManager *area = i == 0 ? sFieldSystem->areaDataManager
            : sExtraAreaManagers[i - 1].areaDataManager;
        NNSG3dResTex *propTexture = area->mapPropTexture;
        SetSwappedTextureKeys(propTexture);
        if (i != 0) {
            NNSG3dResTex *mapTexture = sExtraAreaManagers[i - 1].mapTexture;
            SetSwappedTextureKeys(mapTexture);
        }
        RebindAreaPropModels(area, propTexture);
    }
    for (uint32_t i = 0; i < sExtraChunkCount; i++) {
        if (!sExtraChunks[i].usesCurrentArea) {
            RebindSwappedModel(sExtraChunks[i].modelFile, sExtraChunks[i].mapTexture);
        }
    }
}

static void GetMatrixTextureCapacity(uint32_t *textureSize, uint32_t *paletteSize)
{
    int visited[MAP_MATRIX_MAX_SIZE];
    int count = 0;
    const MapMatrix *matrix = sFieldSystem->mapMatrix;
    for (int i = 0; i < matrix->width * matrix->height; i++) {
        if (matrix->data.landDataIDs[i] == INVALID_LAND_DATA_ID) {
            continue;
        }
        int areaID = MapHeader_GetAreaDataArchiveID(MapMatrix_GetMapHeaderIDAtIndex(matrix, i));
        int j;
        for (j = 0; j < count && visited[j] != areaID; j++) {
        }
        if (j != count) {
            continue;
        }
        visited[count++] = areaID;
        AreaDataFile area;
        NARC_ReadWholeMemberByIndexPair(&area, NARC_INDEX_FIELDDATA__AREADATA__AREA_DATA, areaID);
        int archives[] = {
            NARC_INDEX_FIELDDATA__AREADATA__AREA_MAP_TEX__MAP_TEX_SET,
            NARC_INDEX_FIELDDATA__AREADATA__AREA_BUILD_MODEL__AREABM_TEXSET,
        };
        int members[] = { area.mapTextureArchiveID, area.mapPropArchivesID };
        for (j = 0; j < 2; j++) {
            void *file = ReadExtraResource(archives[j], members[j]);
            NNSG3dResTex *texture = file != NULL ? NNS_G3dGetTex(file) : NULL;
            if (texture != NULL) {
                uint32_t size = NNS_G3dTexGetRequiredSize(texture);
                if (size > *textureSize) *textureSize = size;
                size = NNS_G3dPlttGetRequiredSize(texture);
                if (size > *paletteSize) *paletteSize = size;
            }
            free(file);
        }
    }
}

static int PrepareTextureSwap(NNSG3dResTex *foreignMapTexture, NNSG3dResTex *foreignPropTexture)
{
    NNSG3dResTex *currentPropTexture = sFieldSystem->areaDataManager->mapPropTexture;
    if (currentPropTexture == NULL || NNS_G3dTex4x4GetRequiredSize(currentPropTexture) != 0
        || NNS_G3dTex4x4GetRequiredSize(foreignMapTexture) != 0
        || NNS_G3dTex4x4GetRequiredSize(foreignPropTexture) != 0) {
        return 0;
    }

    NNSG3dTexKey oldTextureKey = sCurrentPropTextureBackup != NULL
        ? sTextureSwapKey : currentPropTexture->texInfo.vramKey;
    NNSG3dPlttKey oldPaletteKey = sCurrentPropTextureBackup != NULL
        ? sPaletteSwapKey : currentPropTexture->plttInfo.vramKey;
    if (oldTextureKey == 0 || oldPaletteKey == 0) {
        return 0;
    }

    uint32_t requiredTextureSize = NNS_GfdGetTexKeySize(oldTextureKey);
    uint32_t requiredPaletteSize = NNS_GfdGetPlttKeySize(oldPaletteKey);
    NNSG3dResTex *textures[] = { foreignMapTexture, foreignPropTexture };
    for (uint32_t i = 0; i < sizeof(textures) / sizeof(textures[0]); i++) {
        if (NNS_G3dTexGetRequiredSize(textures[i]) > requiredTextureSize) {
            requiredTextureSize = NNS_G3dTexGetRequiredSize(textures[i]);
        }
        if (NNS_G3dPlttGetRequiredSize(textures[i]) > requiredPaletteSize) {
            requiredPaletteSize = NNS_G3dPlttGetRequiredSize(textures[i]);
        }
    }
    if (sCurrentPropTextureBackup != NULL
        && requiredTextureSize == NNS_GfdGetTexKeySize(oldTextureKey)
        && requiredPaletteSize == NNS_GfdGetPlttKeySize(oldPaletteKey)) {
        return 1;
    }

    // Reserve the largest texture pair in this matrix before loading any
    // extra areas. Growing incrementally can exhaust VRAM while the old
    // slot is still allocated, leaving distant chunks permanently absent.
    if (sCurrentPropTextureBackup == NULL) {
        GetMatrixTextureCapacity(&requiredTextureSize, &requiredPaletteSize);
    }

    // Allocate replacements before releasing the old keys so a failed growth
    // leaves the current scene and all cached areas intact.
    NNSG3dTexKey textureKey = oldTextureKey;
    NNSG3dPlttKey paletteKey = oldPaletteKey;
    if (requiredTextureSize > NNS_GfdGetTexKeySize(oldTextureKey)) {
        textureKey = NNS_GfdAllocTexVram(requiredTextureSize, FALSE, 0);
        if (textureKey == 0) {
            return 0;
        }
    }
    if (requiredPaletteSize > NNS_GfdGetPlttKeySize(oldPaletteKey)) {
        paletteKey = NNS_GfdAllocPlttVram(requiredPaletteSize, TRUE, 0);
        if (paletteKey == 0) {
            if (textureKey != oldTextureKey) {
                NNS_GfdFreeTexVram(textureKey);
            }
            return 0;
        }
    }

    if (sCurrentPropTextureBackup == NULL) {
        int propArchiveID = sFieldSystem->areaDataManager->areaData.mapPropArchivesID;
        NNSG3dResFileHeader *backupFile = ReadExtraResource(
            NARC_INDEX_FIELDDATA__AREADATA__AREA_BUILD_MODEL__AREABM_TEXSET,
            propArchiveID);
        NNSG3dResTex *backup = backupFile != NULL ? NNS_G3dGetTex(backupFile) : NULL;
        if (backup == NULL) {
            if (backupFile != NULL) {
                free(backupFile);
            }
            if (textureKey != oldTextureKey) {
                NNS_GfdFreeTexVram(textureKey);
            }
            if (paletteKey != oldPaletteKey) {
                NNS_GfdFreePlttVram(paletteKey);
            }
            return 0;
        }
        sCurrentPropTextureBackupFile = backupFile;
        sCurrentPropTextureBackup = backup;
        sOriginalCurrentPropTexture = currentPropTexture;
    }

    NNS_G3dGeFlushBuffer();
    G3SIM_FlushArray();
    NNSG3dTexKey releasedTextureKey, released4x4Key;
    NNS_G3dTexReleaseTexKey(currentPropTexture, &releasedTextureKey, &released4x4Key);
    NNS_G3dPlttReleasePlttKey(currentPropTexture);
    sTextureSwapKey = textureKey;
    sTextureSwap4x4Key = 0;
    sPaletteSwapKey = paletteKey;
    if (textureKey != oldTextureKey) {
        NNS_GfdFreeTexVram(oldTextureKey);
    }
    if (paletteKey != oldPaletteKey) {
        NNS_GfdFreePlttVram(oldPaletteKey);
    }
    sActiveSwappedTexture = NULL;
    sFieldSystem->areaDataManager->mapPropTexture = sCurrentPropTextureBackup;
    RebindSwappedModels();
    ActivateSwappedTexture(sCurrentPropTextureBackup);
    return 1;
}

static void RestoreCurrentPropTexture(void)
{
    if (sCurrentPropTextureBackup == NULL || sOriginalCurrentPropTexture == NULL) {
        return;
    }

    ActivateSwappedTexture(sCurrentPropTextureBackup);
    NNS_G3dTexSetTexKey(
        sOriginalCurrentPropTexture,
        sTextureSwapKey,
        sTextureSwap4x4Key);
    NNS_G3dPlttSetPlttKey(sOriginalCurrentPropTexture, sPaletteSwapKey);
    sOriginalCurrentPropTexture->texInfo.flag |= NNS_G3D_RESTEX_LOADED;
    sOriginalCurrentPropTexture->tex4x4Info.flag |= sTextureSwap4x4Key != 0
        ? NNS_G3D_RESTEX4x4_LOADED
        : 0;
    sOriginalCurrentPropTexture->plttInfo.flag |= NNS_G3D_RESPLTT_LOADED;
    if (sFieldSystem != NULL && sFieldSystem->areaDataManager != NULL) {
        sFieldSystem->areaDataManager->mapPropTexture = sOriginalCurrentPropTexture;
        RebindAreaPropModels(sFieldSystem->areaDataManager, sOriginalCurrentPropTexture);
    }
    free(sCurrentPropTextureBackupFile);
    sCurrentPropTextureBackupFile = NULL;
    sCurrentPropTextureBackup = NULL;
    sOriginalCurrentPropTexture = NULL;
    sTextureSwapKey = 0;
    sTextureSwap4x4Key = 0;
    sPaletteSwapKey = 0;
    sActiveSwappedTexture = NULL;
}

static NNSG3dResTex *GetMapTextureForHeader(
    uint16_t mapHeaderID,
    int *usesCurrentArea,
    AreaDataManager **areaDataManager)
{
    AreaDataFile areaData;
    int areaDataArchiveID;

    if (sFieldSystem == NULL || sFieldSystem->location == NULL) {
        return NULL;
    }

    areaDataArchiveID = MapHeader_GetAreaDataArchiveID(mapHeaderID);
    if (mapHeaderID == MAP_HEADER_EVERYWHERE
        || areaDataArchiveID == sFieldSystem->areaDataManager->areaDataArchiveID) {
        *usesCurrentArea = 1;
        *areaDataManager = sFieldSystem->areaDataManager;
        return AreaDataManager_GetMapTexture(sFieldSystem->areaDataManager);
    }

    for (uint32_t i = 0; i < sExtraAreaManagerCount; i++) {
        if (sExtraAreaManagers[i].areaDataArchiveID == areaDataArchiveID) {
            *usesCurrentArea = 0;
            *areaDataManager = sExtraAreaManagers[i].areaDataManager;
            return sExtraAreaManagers[i].mapTexture;
        }
    }

    if (sExtraAreaManagerCount >= MAX_EXTRA_AREA_MANAGERS) {
        return NULL;
    }

    ExtraAreaManager *entry = &sExtraAreaManagers[sExtraAreaManagerCount];
    entry->areaDataArchiveID = areaDataArchiveID;
    NARC_ReadWholeMemberByIndexPair(
        &areaData,
        NARC_INDEX_FIELDDATA__AREADATA__AREA_DATA,
        areaDataArchiveID);
    entry->mapTextureFile = ReadExtraResource(
        NARC_INDEX_FIELDDATA__AREADATA__AREA_MAP_TEX__MAP_TEX_SET,
        areaData.mapTextureArchiveID);
    if (entry->mapTextureFile == NULL) {
        entry->areaDataArchiveID = -1;
        return NULL;
    }
    entry->mapTexture = NNS_G3dGetTex(entry->mapTextureFile);
    entry->mapPropTextureFile = ReadExtraResource(
        NARC_INDEX_FIELDDATA__AREADATA__AREA_BUILD_MODEL__AREABM_TEXSET,
        areaData.mapPropArchivesID);
    NNSG3dResTex *mapPropTexture = entry->mapPropTextureFile != NULL
        ? NNS_G3dGetTex(entry->mapPropTextureFile)
        : NULL;
    if (entry->mapTexture == NULL || mapPropTexture == NULL
        || !PrepareTextureSwap(entry->mapTexture, mapPropTexture)) {
        free(entry->mapTextureFile);
        if (entry->mapPropTextureFile != NULL) {
            free(entry->mapPropTextureFile);
        }
        entry->areaDataArchiveID = -1;
        entry->mapTextureFile = NULL;
        entry->mapTexture = NULL;
        entry->mapPropTextureFile = NULL;
        return NULL;
    }

    entry->areaDataManager = calloc(1, sizeof(AreaDataManager));
    if (entry->areaDataManager == NULL) {
        free(entry->mapTextureFile);
        free(entry->mapPropTextureFile);
        entry->mapTextureFile = NULL;
        entry->mapPropTextureFile = NULL;
        entry->mapTexture = NULL;
        return NULL;
    }
    entry->areaDataManager->areaData = areaData;
    entry->areaDataManager->areaDataArchiveID = areaDataArchiveID;
    entry->areaDataManager->mapTexture = entry->mapTexture;
    entry->areaDataManager->mapPropTexture = mapPropTexture;
    entry->areaDataManager->mapPropTextureFile = entry->mapPropTextureFile;
    entry->areaDataManager->mapPropMatShp = sFieldSystem->areaDataManager->mapPropMatShp;
    entry->areaDataManager->mapPropModelIDs = ReadExtraResource(
        NARC_INDEX_FIELDDATA__AREADATA__AREA_BUILD_MODEL__AREA_BUILD,
        areaData.mapPropArchivesID);
    if (entry->areaDataManager->mapPropModelIDs == NULL
        || !ActivateSwappedTexture(mapPropTexture)) {
        if (entry->areaDataManager->mapPropModelIDs != NULL) {
            free(entry->areaDataManager->mapPropModelIDs);
        }
        free(entry->areaDataManager);
        free(entry->mapTextureFile);
        free(entry->mapPropTextureFile);
        entry->areaDataManager = NULL;
        entry->mapTextureFile = NULL;
        entry->mapPropTextureFile = NULL;
        entry->mapTexture = NULL;
        return NULL;
    }

    uint16_t modelCount = entry->areaDataManager->mapPropModelIDs[0];
    for (uint32_t i = 0; i < modelCount; i++) {
        uint16_t modelID = entry->areaDataManager->mapPropModelIDs[i + 1];
        if (modelID >= MAX_MAP_PROP_MODEL_FILES) {
            continue;
        }
        entry->areaDataManager->mapPropModelFiles[modelID] = ReadExtraResource(
            NARC_INDEX_FIELDDATA__BUILD_MODEL__BUILD_MODEL,
            modelID);
        if (entry->areaDataManager->mapPropModelFiles[modelID] != NULL) {
            Easy3D_BindTextureToResource(
                entry->areaDataManager->mapPropModelFiles[modelID],
                mapPropTexture);
        }
    }
    if (entry->areaDataManager->mapPropModelFiles[0] == NULL) {
        entry->areaDataManager->mapPropModelFiles[0] = ReadExtraResource(
            NARC_INDEX_FIELDDATA__BUILD_MODEL__BUILD_MODEL,
            0);
        if (entry->areaDataManager->mapPropModelFiles[0] != NULL) {
            NNSG3dResTex *dummyTexture = NNS_G3dGetTex(entry->areaDataManager->mapPropModelFiles[0]);
            if (dummyTexture != NULL) {
                Easy3D_BindTextureToResource(entry->areaDataManager->mapPropModelFiles[0], dummyTexture);
            }
        }
    }
    ActivateSwappedTexture(sCurrentPropTextureBackup);
    sExtraAreaManagerCount++;
    *usesCurrentArea = 0;
    *areaDataManager = entry->areaDataManager;
    return entry->mapTexture;
}

static void FreeExtraChunk(ExtraChunk *chunk)
{
    // The animation manager outlives visual chunks and must not retain their
    // render objects when a chunk leaves the cache.
    if (chunk->mapPropManager != NULL && sFieldSystem != NULL
        && sFieldSystem->mapPropAnimMan != NULL) {
        for (uint32_t i = 0; i < MAP_PROP_ANIMATION_MANAGER_MAX_BICYCLE_SLOPE_ANIMATIONS; i++) {
            BicycleSlopeAnimation *animation = &sFieldSystem->mapPropAnimMan->bicycleSlopeAnimations[i];
            for (uint32_t j = 0; animation->loaded && j < MAX_LOADED_MAP_PROPS; j++) {
                if (animation->renderObj == &chunk->mapPropManager->loadedProps[j].renderObj) {
                    *animation = (BicycleSlopeAnimation){ 0 };
                }
            }
        }
    }
    free(chunk->mapPropManager);
    free(chunk->modelFile);
    free(chunk->bdhcBuffer);
    free(chunk->terrainAttributes);
    *chunk = (ExtraChunk){ .mapMatrixIndex = INVALID_MAP_MATRIX_INDEX,
        .landDataID = INVALID_LAND_DATA_ID };
}

static void ClearExtraChunks(void)
{
    for (uint32_t i = 0; i < sExtraChunkCount; i++) {
        FreeExtraChunk(&sExtraChunks[i]);
    }

    sExtraChunkCount = 0;
    sCachedLandDataMan = NULL;
    sCachedCenterX = -1;
    sCachedCenterZ = -1;
    sCachedRadius = 0;
    for (uint32_t i = 0; i < BASE_LOADED_CHUNK_COUNT; i++) {
        sCachedLoadedIndexes[i] = INVALID_MAP_MATRIX_INDEX;
    }
}

static void CloseExtraLandDataNarc(void)
{
    ClearExtraChunks();
    ClearExtraAreaManagers();
    RestoreCurrentPropTexture();
    if (sExtraLandDataNarc != NULL) {
        NARC_dtor(sExtraLandDataNarc);
        sExtraLandDataNarc = NULL;
    }
}

void CameraDistance_BeforeLandEnd(LandDataManager *landDataMan)
{
    // Return borrowed texture keys before the field destroys their owner and
    // resets its VRAM allocator. FieldCamera_Delete runs after that teardown.
    if (sFieldSystem != NULL && sFieldSystem->landDataMan == landDataMan) {
        CloseExtraLandDataNarc();
    }
}

static int IsCurrentAreaBaseChunk(const int mapMatrixIndex, const int *loadedIndexes)
{
    for (uint32_t i = 0; i < BASE_LOADED_CHUNK_COUNT; i++) {
        if (loadedIndexes[i] == mapMatrixIndex) {
            uint16_t mapHeaderID = MapMatrix_GetMapHeaderIDAtIndex(
                sFieldSystem->mapMatrix,
                mapMatrixIndex);
            return mapHeaderID == MAP_HEADER_EVERYWHERE
                || MapHeader_GetAreaDataArchiveID(mapHeaderID)
                    == sFieldSystem->areaDataManager->areaDataArchiveID;
        }
    }

    return 0;
}

static int IsLoadedBaseChunk(const int mapMatrixIndex, const int *loadedIndexes)
{
    for (uint32_t i = 0; i < BASE_LOADED_CHUNK_COUNT; i++) {
        if (loadedIndexes[i] == mapMatrixIndex) {
            return 1;
        }
    }

    return 0;
}

static int LoadedIndexesChanged(const int *loadedIndexes)
{
    for (uint32_t i = 0; i < BASE_LOADED_CHUNK_COUNT; i++) {
        if (sCachedLoadedIndexes[i] != loadedIndexes[i]) {
            return 1;
        }
    }

    return 0;
}

static uint32_t ExtraChunkRadius(void)
{
    uint32_t radius = sZoomMultiplier - 1;
    if (sZoomMultiplier == 1 || sFieldCamera == NULL || sFieldSystem == NULL) {
        return radius;
    }

    // Bound the viewport's intersection with the lowest terrain plane. At
    // large FOVs the top ray approaches the horizon, so zoom - 1 is not a
    // sufficient load radius. The horizontal norm also covers camera yaw.
    double sinPitch = fabs((double)FX_SinIdx(sFieldCamera->angle.x) / FX32_ONE);
    double cosPitch = fabs((double)FX_CosIdx(sFieldCamera->angle.x) / FX32_ONE);
    double tangent = (double)FX_SinIdx(sBaseFovY) * sZoomMultiplier / FX_CosIdx(sBaseFovY);
    double denominator = sinPitch - cosPitch * tangent;
    if (denominator <= 0.0) {
        return MAX_EXTRA_CHUNK_RADIUS;
    }
    double distance = (double)sFieldCamera->distance / FX32_ONE;
    int lowestAltitude = 255;
    const MapMatrix *matrix = sFieldSystem->mapMatrix;
    for (int i = 0; i < matrix->width * matrix->height; i++) {
        if (matrix->data.landDataIDs[i] != INVALID_LAND_DATA_ID
            && matrix->data.altitudes[i] < lowestAltitude) {
            lowestAltitude = matrix->data.altitudes[i];
        }
    }
    VecFx32 offset = { 0 };
    if (sFieldSystem->landDataMan != NULL) {
        LandDataManager_GetOffset(sFieldSystem->landDataMan, &offset);
    }
    double height = fmax(distance * sinPitch,
        (double)(sFieldCamera->lookAt.position.y - offset.y) / FX32_ONE
            - lowestAltitude * (MAP_OBJECT_TILE_SIZE / FX32_ONE / 2));
    double depth = height / denominator;
    double forward = fabs((depth - distance) * cosPitch) + depth * tangent * sinPitch;
    double sideways = depth * tangent * sFieldCamera->perspective.aspectRatio / FX32_ONE;
    double chunkSize = MAP_TILES_COUNT_X * (MAP_OBJECT_TILE_SIZE / FX32_ONE);
    double coverage = ceil(hypot(forward, sideways) / chunkSize) + 1;
    if (coverage >= MAX_EXTRA_CHUNK_RADIUS) {
        return MAX_EXTRA_CHUNK_RADIUS;
    }
    if (coverage > radius) {
        radius = (uint32_t)coverage;
    }
    return radius > MAX_EXTRA_CHUNK_RADIUS ? MAX_EXTRA_CHUNK_RADIUS : radius;
}

BOOL CameraDistance_GetTileAttributes(const FieldSystem *fieldSystem, int tileX, int tileZ, u16 *attributes)
{
    if (fieldSystem != sFieldSystem || tileX < 0 || tileZ < 0) return FALSE;
    int width = MapMatrix_GetWidth(fieldSystem->mapMatrix);
    if (tileX / MAP_TILES_COUNT_X >= width
        || tileZ / MAP_TILES_COUNT_Z >= MapMatrix_GetHeight(fieldSystem->mapMatrix)) return FALSE;
    int index = tileX / MAP_TILES_COUNT_X + (tileZ / MAP_TILES_COUNT_Z) * width;
    for (u32 i = 0; i < sExtraChunkCount; i++) {
        const ExtraChunk *chunk = &sExtraChunks[i];
        if (chunk->mapMatrixIndex == index && chunk->terrainAttributes != NULL) {
            *attributes = chunk->terrainAttributes[(tileZ % MAP_TILES_COUNT_Z) * MAP_TILES_COUNT_X
                + tileX % MAP_TILES_COUNT_X];
            return TRUE;
        }
    }
    return FALSE;
}

BOOL CameraDistance_GetObjectHeight(const FieldSystem *fieldSystem, int mapMatrixIndex,
    fx32 currentHeight, fx32 localX, fx32 localZ, fx32 *height)
{
    if (fieldSystem != sFieldSystem) return FALSE;
    for (u32 i = 0; i < sExtraChunkCount; i++) {
        if (sExtraChunks[i].mapMatrixIndex == mapMatrixIndex) {
            return CalculateObjectHeight(currentHeight, localX, localZ, &sExtraChunks[i].bdhc, height);
        }
    }
    return FALSE;
}

static int LoadExtraChunk(int mapMatrixIndex)
{
    AreaDataManager *areaDataManager;
    LandDataFileHeader header;
    NNSG3dResTex *mapTexture;
    NNSG3dResMdl *mapModel;
    void *terrainAttributes = NULL;
    int landDataID;
    int usesCurrentArea;
    uint16_t mapHeaderID;

    for (uint32_t i = 0; i < sExtraChunkCount; i++) {
        if (sExtraChunks[i].mapMatrixIndex == mapMatrixIndex) {
            return 1;
        }
    }

    if (sExtraChunkCount >= MAX_EXTRA_CHUNKS || sFieldSystem == NULL) {
        return 0;
    }

    if (sExtraLandDataNarc == NULL) {
        sExtraLandDataNarc = NARC_ctor(NARC_INDEX_FIELDDATA__LAND_DATA__LAND_DATA, HEAP_ID_FIELD1);
        if (sExtraLandDataNarc == NULL) {
            return 0;
        }
    }

    mapHeaderID = MapMatrix_GetMapHeaderIDAtIndex(sFieldSystem->mapMatrix, mapMatrixIndex);
    if (sFieldSystem->location == NULL) {
        return 0;
    }

    landDataID = MapMatrix_GetLandDataIDByIndex(mapMatrixIndex, sFieldSystem->mapMatrix);
    if (landDataID < 0 || landDataID == INVALID_LAND_DATA_ID) {
        return 0;
    }

    mapTexture = GetMapTextureForHeader(mapHeaderID, &usesCurrentArea, &areaDataManager);
    if (mapTexture == NULL) {
        return 0;
    }

    NARC_ReadFromMember(sExtraLandDataNarc, (uint32_t)landDataID, 0, sizeof(header), &header);
    if (header.terrainAttributesSize < 0 || header.terrainAttributesSize > 0x10000
        || header.mapPropsSize < 0 || header.mapPropsSize > 0x10000
        || header.mapModelSize <= 0 || header.mapModelSize > 0x100000) {
        return 0;
    }

    ExtraChunk *chunk = &sExtraChunks[sExtraChunkCount];
    chunk->mapMatrixIndex = mapMatrixIndex;
    chunk->landDataID = landDataID;
    chunk->mapTexture = mapTexture;
    chunk->areaDataManager = areaDataManager;
    chunk->usesCurrentArea = usesCurrentArea;
    chunk->modelFile = malloc((size_t)header.mapModelSize);
    chunk->mapPropManager = malloc(sizeof(MapPropManager));
    if (chunk->modelFile == NULL || chunk->mapPropManager == NULL) {
        free(chunk->mapPropManager);
        free(chunk->modelFile);
        chunk->mapPropManager = NULL;
        chunk->modelFile = NULL;
        chunk->mapTexture = NULL;
        chunk->areaDataManager = NULL;
        return 0;
    }

    if (header.terrainAttributesSize > 0) {
        terrainAttributes = malloc((size_t)header.terrainAttributesSize);
        if (terrainAttributes == NULL) {
            free(chunk->mapPropManager);
            free(chunk->modelFile);
            chunk->mapPropManager = NULL;
            chunk->modelFile = NULL;
            chunk->mapTexture = NULL;
            chunk->areaDataManager = NULL;
            return 0;
        }
        NARC_ReadFromMember(
            sExtraLandDataNarc,
            (uint32_t)landDataID,
            sizeof(header),
            (uint32_t)header.terrainAttributesSize,
            terrainAttributes);
        // Distant objects also need tile behavior to finish initialization.
        if (header.terrainAttributesSize >= MAP_TILES_COUNT_X * MAP_TILES_COUNT_Z * sizeof(u16)) {
            chunk->terrainAttributes = terrainAttributes;
        } else {
            free(terrainAttributes);
        }
    } else {
        NARC_ReadFromMember(sExtraLandDataNarc, (uint32_t)landDataID, sizeof(header), 0, &header);
    }

    MapPropManager_Init(chunk->mapPropManager);
    MapPropManager_Load(
        sExtraLandDataNarc,
        header.mapPropsSize,
        areaDataManager,
        chunk->mapPropManager,
        sFieldSystem->mapPropAnimMan);

    if (!usesCurrentArea && !ActivateSwappedTexture(mapTexture)) {
        FreeExtraChunk(chunk);
        return 0;
    }
    mapModel = LandDataManager_LoadMapModel(
        sExtraLandDataNarc,
        header.mapModelSize,
        &chunk->renderObj,
        &chunk->modelFile,
        mapTexture);
    if (mapModel == NULL) {
        FreeExtraChunk(chunk);
        return 0;
    }
    // Keep collision geometry alongside every extra visual block. Distant map
    // objects need the same height solver as objects in the four native blocks.
    if (header.bdhcSize >= 16 && header.bdhcSize <= 0x100000) {
        chunk->bdhcBuffer = malloc(header.bdhcSize);
        if (chunk->bdhcBuffer != NULL) {
            BDHC_Load(sExtraLandDataNarc, header.bdhcSize, &chunk->bdhc, chunk->bdhcBuffer);
        }
    }
    if (sCurrentPropTextureBackup != NULL) {
        ActivateSwappedTexture(sCurrentPropTextureBackup);
    }

    if (AreaDataManager_IsOutdoorsLighting(sFieldSystem->areaDataManager)) {
        AreaLight_UseGlobalModelAttributes(mapModel);
    }
    sExtraChunkCount++;
    return 1;
}

static void RebuildExtraChunkCache(
    LandDataManager *landDataMan,
    int centerX,
    int centerZ,
    uint32_t radius,
    const int *loadedIndexes)
{
    int mapWidth = MapMatrix_GetWidth(sFieldSystem->mapMatrix);
    int mapHeight = MapMatrix_GetHeight(sFieldSystem->mapMatrix);

    if (sCachedLandDataMan != landDataMan) {
        ClearExtraChunks();
    }
    for (uint32_t i = 0; i < sExtraChunkCount;) {
        int index = sExtraChunks[i].mapMatrixIndex;
        int x = index % mapWidth;
        int z = index / mapWidth;
        int inRange = x >= centerX - (int)radius && x <= centerX + (int)radius
            && z >= centerZ - (int)radius && z <= centerZ + (int)radius;
        if (IsCurrentAreaBaseChunk(index, loadedIndexes)
            || (!inRange && !IsLoadedBaseChunk(index, loadedIndexes))) {
            FreeExtraChunk(&sExtraChunks[i]);
            sExtraChunks[i] = sExtraChunks[--sExtraChunkCount];
        } else {
            i++;
        }
    }
    sCachedLandDataMan = landDataMan;
    sCachedCenterX = centerX;
    sCachedCenterZ = centerZ;
    sCachedRadius = radius;
    for (uint32_t i = 0; i < BASE_LOADED_CHUNK_COUNT; i++) {
        sCachedLoadedIndexes[i] = loadedIndexes[i];
    }

    /* Reserve texture capacity from the chunks the game already considers
     * adjacent before looking farther away.  Otherwise an unrelated distant
     * area can size the shared texture slot too small for a visible border. */
    for (uint32_t i = 0; i < BASE_LOADED_CHUNK_COUNT; i++) {
        if (loadedIndexes[i] != INVALID_MAP_MATRIX_INDEX
            && !IsCurrentAreaBaseChunk(loadedIndexes[i], loadedIndexes)) {
            LoadExtraChunk(loadedIndexes[i]);
        }
    }

    for (int mapZ = centerZ - (int)radius; mapZ <= centerZ + (int)radius; mapZ++) {
        if (mapZ < 0 || mapZ >= mapHeight) {
            continue;
        }

        for (int mapX = centerX - (int)radius; mapX <= centerX + (int)radius; mapX++) {
            if (mapX < 0 || mapX >= mapWidth) {
                continue;
            }

            int mapMatrixIndex = mapZ * mapWidth + mapX;
            // Every valid chunk in the zoom radius is a terrain candidate.
            // Restricting headers to the immediate 3x3 neighborhood leaves
            // distant routes empty while their expanded object view is drawn.
            if (IsLoadedBaseChunk(mapMatrixIndex, loadedIndexes)) {
                continue;
            }

            LoadExtraChunk(mapMatrixIndex);
        }
    }
}

static void RenderForeignMapProps(
    const VecFx32 *chunkOffset,
    ModelAttributes *modelAttrs,
    ExtraChunk *chunk)
{
    static const MtxFx33 identity = {
        FX32_ONE, 0, 0,
        0, FX32_ONE, 0,
        0, 0, FX32_ONE,
    };

    for (uint32_t i = 0; i < MAX_LOADED_MAP_PROPS; i++) {
        MapProp *prop = &chunk->mapPropManager->loadedProps[i];
        MtxFx33 rotation = identity;
        VecFx32 position;

        if (!prop->loaded || prop->hidden || prop->model == NULL) {
            continue;
        }

        position = prop->position;
        position.x += chunkOffset->x;
        position.z += chunkOffset->z;
        if (prop->applyRotation) {
            MTX_Rot33Vec(&rotation, &prop->rotation);
        }
        if (AreaDataManager_IsOutdoorsLighting(chunk->areaDataManager)) {
            ModelAttributes_ApplyToModel(
                modelAttrs,
                prop->model,
                MODEL_LIGHT_VECTOR_1 | MODEL_LIGHT_VECTOR_2
                    | MODEL_LIGHT_COLOR_1 | MODEL_LIGHT_COLOR_2
                    | MODEL_DIFFUSE_REFLECT_COLOR | MODEL_AMBIENT_REFLECT_COLOR
                    | MODEL_SPECULAR_REFLECT_COLOR | MODEL_EMISSION_COLOR);
        }
        Easy3D_DrawRenderObj(&prop->renderObj, &position, &rotation, &prop->scale);
    }
}

static int ChunkIsInView(const ExtraChunk *chunk, const VecFx32 *position, const double clip[4][4])
{
    const NNSG3dResMdlInfo *info = &chunk->renderObj.resMdl->info;
    double boxScale = (double)info->boxPosScale / FX32_ONE;
    // Include objects extending beyond the terrain bounds (trees and roofs).
    double minimum[3] = {
        (double)position->x / FX32_ONE + info->boxX * boxScale / FX32_ONE - 64,
        (double)position->y / FX32_ONE + info->boxY * boxScale / FX32_ONE - 128,
        (double)position->z / FX32_ONE + info->boxZ * boxScale / FX32_ONE - 64,
    };
    double extent[3] = {
        info->boxW * boxScale / FX32_ONE + 128,
        info->boxH * boxScale / FX32_ONE + 256,
        info->boxD * boxScale / FX32_ONE + 128,
    };
    unsigned outsideAll = 0x3f;
    for (int corner = 0; corner < 8; corner++) {
        double point[4] = { 0, 0, 0, 1 }, projected[4] = { 0 };
        for (int axis = 0; axis < 3; axis++) {
            point[axis] = minimum[axis] + ((corner & (1 << axis)) ? extent[axis] : 0);
        }
        for (int column = 0; column < 4; column++) {
            for (int row = 0; row < 4; row++) {
                projected[column] += point[row] * clip[row][column];
            }
        }
        unsigned outside = 0;
        for (int axis = 0; axis < 3; axis++) {
            if (projected[axis] < -projected[3]) outside |= 1u << (axis * 2);
            if (projected[axis] > projected[3]) outside |= 2u << (axis * 2);
        }
        outsideAll &= outside;
    }
    return outsideAll == 0;
}

static void RenderExtraChunks(const LandDataManager *landDataMan, ModelAttributes *modelAttrs)
{
    static const MtxFx33 rotation = {
        FX32_ONE, 0, 0,
        0, FX32_ONE, 0,
        0, 0, FX32_ONE,
    };
    static const VecFx32 scale = { FX32_ONE, FX32_ONE, FX32_ONE };
    VecFx32 landOffset;
    VecFx32 positions[MAX_EXTRA_CHUNKS];
    int visible[MAX_EXTRA_CHUNKS];
    double clip[4][4] = { 0 };
    const MtxFx43 *view = NNS_G3dGlbGetCameraMtx();
    const MtxFx44 *projection = NNS_G3dGlbGetProjectionMtx();
    for (int row = 0; row < 4; row++) {
        for (int column = 0; column < 4; column++) {
            for (int k = 0; k < 4; k++) {
                double v = k < 3 ? (double)view->m[row][k] / FX32_ONE : (row == 3);
                clip[row][column] += v * projection->m[k][column] / FX32_ONE;
            }
        }
    }

    LandDataManager_GetOffset(landDataMan, &landOffset);
    for (uint32_t i = 0; i < sExtraChunkCount; i++) {
        positions[i] = (VecFx32){ 0, 0, 0 };

        LandDataManager_CalculateRenderingPosition(
            sExtraChunks[i].mapMatrixIndex,
            MapMatrix_GetWidth(sFieldSystem->mapMatrix),
            sFieldSystem->mapMatrix,
            &positions[i]);
        positions[i].x += landOffset.x;
        positions[i].y += landOffset.y;
        positions[i].z += landOffset.z;
        visible[i] = ChunkIsInView(&sExtraChunks[i], &positions[i], clip);
    }

    // Upload each area's shared terrain/prop texture once per pass instead of
    // twice per chunk. Flush queued geometry before changing its texture data.
    for (uint32_t areaIndex = 0; areaIndex <= sExtraAreaManagerCount; areaIndex++) {
        AreaDataManager *area = areaIndex == 0 ? sFieldSystem->areaDataManager
            : sExtraAreaManagers[areaIndex - 1].areaDataManager;
        int areaVisible = 0;
        for (uint32_t i = 0; i < sExtraChunkCount; i++) {
            if (visible[i] && sExtraChunks[i].areaDataManager == area) {
                areaVisible = 1;
                break;
            }
        }
        if (!areaVisible) {
            continue;
        }
        NNSG3dResTex *terrainTexture = areaIndex == 0 ? NULL
            : sExtraAreaManagers[areaIndex - 1].mapTexture;
        if (areaIndex == 0 || ActivateSwappedTexture(terrainTexture)) {
            for (uint32_t i = 0; i < sExtraChunkCount; i++) {
                if (visible[i] && sExtraChunks[i].areaDataManager == area) {
                    Easy3D_DrawRenderObj(&sExtraChunks[i].renderObj, &positions[i], &rotation, &scale);
                }
            }
        }
        NNSG3dResTex *propTexture = areaIndex == 0
            ? sCurrentPropTextureBackup : area->mapPropTexture;
        if (propTexture != NULL && !ActivateSwappedTexture(propTexture)) {
            continue;
        }
        for (uint32_t i = 0; i < sExtraChunkCount; i++) {
            if (!visible[i] || sExtraChunks[i].areaDataManager != area) {
                continue;
            }
            if (areaIndex == 0) {
                MapPropManager_Render(&positions[i], area,
                    AreaDataManager_IsOutdoorsLighting(area), modelAttrs, sExtraChunks[i].mapPropManager);
            } else {
                RenderForeignMapProps(&positions[i], modelAttrs, &sExtraChunks[i]);
            }
        }
    }
    if (sCurrentPropTextureBackup != NULL) {
        ActivateSwappedTexture(sCurrentPropTextureBackup);
    }
}

BOOL CameraDistance_ShouldRenderMap(
    uint8_t index,
    const LandDataManager *landDataMan,
    ModelAttributes *modelAttrs)
{
    int mapMatrixIndex = LandDataManager_GetLoadedMapMatrixIndex(landDataMan, index);

    if (sFieldSystem != NULL && sFieldSystem->location != NULL
        && sZoomMultiplier > 1 && mapMatrixIndex != INVALID_MAP_MATRIX_INDEX) {
        uint16_t mapHeaderID = MapMatrix_GetMapHeaderIDAtIndex(
            sFieldSystem->mapMatrix,
            mapMatrixIndex);
        if (mapHeaderID != MAP_HEADER_EVERYWHERE
            && MapHeader_GetAreaDataArchiveID(mapHeaderID)
                != sFieldSystem->areaDataManager->areaDataArchiveID) {
            return FALSE;
        }
    }

    return TRUE;
}

void CameraDistance_PrepareFrame(const LandDataManager *landDataMan)
{
    int loadedIndexes[BASE_LOADED_CHUNK_COUNT];
    uint32_t radius;
    int playerTileX;
    int playerTileZ;
    int centerX;
    int centerZ;

    if (sFieldSystem == NULL || landDataMan != sFieldSystem->landDataMan || sZoomMultiplier == 1) {
        ClearExtraChunks();
        return;
    }

    radius = ExtraChunkRadius();
    ApplyClipping(radius);
    if (radius != sCachedRadius) {
        MapHeaderData_SetObjectViewRadius((int)radius);
    }
    playerTileX = PlayerAvatar_GetXPos(sFieldSystem->playerAvatar);
    playerTileZ = PlayerAvatar_GetZPos(sFieldSystem->playerAvatar);
    centerX = (playerTileX - LandDataManager_GetOffsetTileX(landDataMan)) / MAP_TILES_COUNT_X;
    centerZ = (playerTileZ - LandDataManager_GetOffsetTileZ(landDataMan)) / MAP_TILES_COUNT_Z;

    for (uint32_t i = 0; i < BASE_LOADED_CHUNK_COUNT; i++) {
        loadedIndexes[i] = LandDataManager_IsLoadedMapReady(landDataMan, (uint8_t)i)
            ? LandDataManager_GetLoadedMapMatrixIndex(landDataMan, (uint8_t)i)
            : INVALID_MAP_MATRIX_INDEX;
    }

    if (sCachedLandDataMan != landDataMan
        || sCachedCenterX != centerX
        || sCachedCenterZ != centerZ
        || sCachedRadius != radius
        || LoadedIndexesChanged(loadedIndexes)) {
        RebuildExtraChunkCache(
            (LandDataManager *)landDataMan,
            centerX,
            centerZ,
            radius,
            loadedIndexes);
    }
}

void CameraDistance_RenderExtra(const LandDataManager *landDataMan, ModelAttributes *modelAttrs)
{
    if (sFieldSystem != NULL && landDataMan == sFieldSystem->landDataMan && sZoomMultiplier > 1) {
        RenderExtraChunks(landDataMan, modelAttrs);
    }
}

static uint16_t ScaledFov(uint16_t baseFovY)
{
    if (sZoomMultiplier == 1) {
        return baseFovY;
    }

    fx32 scaledSin = (fx32)FX_SinIdx(baseFovY) * (fx32)sZoomMultiplier;
    fx32 cos = (fx32)FX_CosIdx(baseFovY);
    return FX_Atan2Idx(scaledSin, cos);
}

// Called by the host renderer for each fog-table boundary. Extend fog in
// world depth around the camera target, then project it with the new clipping
// planes; keeping the old normalized boundaries makes caves turn black.
float CameraDistance_AdjustFogDepth(float boundary)
{
    if (sFieldCamera == NULL || !Camera_IsActive(sFieldCamera)
        || sZoomMultiplier == 1 || sFieldSystem == NULL
        || !FieldSystem_IsRunningFieldMap(sFieldSystem)) {
        return boundary;
    }

    double near = sBaseNearClip;
    double far = sBaseFarClip;
    double newFar = sFieldCamera->perspective.farClip;
    double depth;
    if (sFieldCamera->projection == CAMERA_PROJECTION_PERSPECTIVE) {
        double denominator = far + near - boundary * (far - near);
        if (denominator <= 0.0) {
            return boundary;
        }
        depth = 2.0 * near * far / denominator;
    } else {
        depth = near + (boundary + 1.0) * (far - near) / 2.0;
    }
    double fogScale = far > sFieldCamera->distance
        ? (newFar - sFieldCamera->distance) / (far - sFieldCamera->distance)
        : sZoomMultiplier;
    depth = sFieldCamera->distance + (depth - sFieldCamera->distance) * fogScale;
    if (depth < near) {
        depth = near;
    }
    if (sFieldCamera->projection == CAMERA_PROJECTION_PERSPECTIVE) {
        return (newFar + near - 2.0 * near * newFar / depth) / (newFar - near);
    }
    return 2.0 * (depth - near) / (newFar - near) - 1.0;
}

static void ApplyClipping(uint32_t radius)
{
    if (sFieldCamera == NULL) {
        return;
    }
    fx32 farClip = sBaseFarClip;
    if (sZoomMultiplier > 1) {
        // Enclose the loaded square, including its outer chunk edges and
        // the matrix's altitude range, rather than scaling the DS margin.
        double span = (radius + 1) * MAP_TILES_COUNT_X
            * (MAP_OBJECT_TILE_SIZE / FX32_ONE);
        double verticalSpan = 255 * (MAP_OBJECT_TILE_SIZE / FX32_ONE / 2)
            + MAP_TILES_COUNT_X * (MAP_OBJECT_TILE_SIZE / FX32_ONE);
        farClip = sFieldCamera->distance
            + (fx32)(ceil(sqrt(2 * span * span + verticalSpan * verticalSpan)) * FX32_ONE);
        if (farClip < sBaseFarClip) {
            farClip = sBaseFarClip;
        }
    }
    // Camera distance can still change during a transition with the same
    // chunk radius. Update clipping before either native or extra draws.
    if (sFieldCamera->perspective.farClip != farClip
        || sFieldCamera->perspective.nearClip != sBaseNearClip) {
        Camera_SetClipping(sBaseNearClip, farClip, sFieldCamera);
    }
}

static void ApplyZoom(void)
{
    if (sFieldCamera != NULL) {
        Camera_SetFOV(ScaledFov(sBaseFovY), sFieldCamera);
        ApplyClipping(ExtraChunkRadius());
    }
}

void CameraDistance_OnCreate(FieldSystem *fieldSystem)
{
    CloseExtraLandDataNarc();
    sFieldSystem = fieldSystem;
    sFieldCamera = FieldSystemCamera(fieldSystem);
    if (sFieldCamera != NULL) {
        sBaseFovY = sFieldCamera->fovY;
        sBaseNearClip = sFieldCamera->perspective.nearClip;
        sBaseFarClip = sFieldCamera->perspective.farClip;
        ApplyZoom();
    }
}

void CameraDistance_OnDelete(FieldSystem *fieldSystem)
{
    Camera *camera = FieldSystemCamera(fieldSystem);

    if (camera == sFieldCamera) {
        CloseExtraLandDataNarc();
        sFieldSystem = NULL;
        sFieldCamera = NULL;
        sBaseFovY = 0;
        sBaseNearClip = 0;
        sBaseFarClip = 0;
    }
}

void CameraDistance_ApplySettings(void)
{
    uint32_t newMultiplier = SIM_GetConfigPtr()->cameraDistance;

    if (newMultiplier < 1 || newMultiplier > SIM_CONFIG_CAMERA_DISTANCE_MAX) {
        return;
    }

    if (newMultiplier == sZoomMultiplier) {
        MapHeaderData_SetObjectViewRadius((int)ExtraChunkRadius());
        return;
    }
    sZoomMultiplier = newMultiplier;
    MapHeaderData_SetObjectViewRadius((int)ExtraChunkRadius());
    ClearExtraChunks();
    ApplyZoom();
}
