#ifdef SDK_PORT
#include "port/camera_distance.h"
#endif
#include "map_header_data.h"

#include <nitro.h>
#include <string.h>

#include "constants/heap.h"
#include "constants/versions.h"
#include "generated/map_headers.h"

#include "field/field_system.h"
#include "overlay006/wild_encounters.h"

#include "heap.h"
#include "map_header.h"
#include "map_object.h"
#include "narc.h"
#ifdef SDK_PORT
#include "map_matrix.h"
#include "player_avatar.h"
#include "script_manager.h"
#endif

static void MapHeaderData_LoadEvents(MapHeaderData *data, enum MapHeaderID mapHeaderID);
static void MapHeaderData_ParseEvents(MapHeaderData *data);
static void MapHeaderData_LoadInitScripts(MapHeaderData *data, int headerID);

#ifdef SDK_PORT
static int sObjectViewRadius;

void MapHeaderData_SetObjectViewRadius(int radius)
{
    sObjectViewRadius = radius < 0 ? 0 : (radius > CAMERA_DISTANCE_MAX_RADIUS ? CAMERA_DISTANCE_MAX_RADIUS : radius);
}

static BOOL ObjectView_Contains(const MapHeaderData *data, const ObjectEvent *event, int owner, int currentHeader)
{
    for (u32 i = 0; i < data->numObjectEvents; i++) {
        const ObjectEvent *existing = &data->visibleObjectEvents[i];
        int existingOwner = existing->script == 0xffff ? existing->hiddenFlag : currentHeader;
        if (existing->localID == event->localID && existingOwner == owner) {
            return TRUE;
        }
        if (existing->graphicsID == event->graphicsID
            && existing->x == event->x && existing->z == event->z) {
            return TRUE;
        }
    }
    return FALSE;
}

static u32 ObjectView_Distance(const ObjectEvent *event, int x, int z)
{
    int dx = (int)event->x - x;
    int dz = (int)event->z - z;
    return dx * dx + dz * dz;
}

static BOOL UpdateObjectView(FieldSystem *fieldSystem)
{
    MapHeaderData *data = fieldSystem->mapHeaderData;
    int currentHeader = fieldSystem->location->mapHeaderID;
    int radius = MapHeader_IsOnMainMatrix(currentHeader) ? sObjectViewRadius : 0;
    int centerX = PlayerAvatar_GetXPos(fieldSystem->playerAvatar) / 32;
    int centerZ = PlayerAvatar_GetZPos(fieldSystem->playerAvatar) / 32;

    if (data->objectViewX == centerX && data->objectViewZ == centerZ
        && data->objectViewRadius == radius) {
        return FALSE;
    }

    // Preserve edits to local events made by scripts before rebuilding the view.
    if (data->objectEvents == data->visibleObjectEvents && data->numLocalObjectEvents != 0) {
        memcpy(data->localObjectEvents, data->visibleObjectEvents,
            data->numLocalObjectEvents * sizeof(ObjectEvent));
    }
    data->objectViewX = centerX;
    data->objectViewZ = centerZ;
    data->objectViewRadius = radius;
    data->objectEvents = data->localObjectEvents;
    data->numObjectEvents = data->numLocalObjectEvents;

    const u32 capacity = sizeof(data->visibleObjectEvents) / sizeof(ObjectEvent);
    if (radius == 0 || data->numLocalObjectEvents >= capacity) {
        return TRUE;
    }
    if (data->numLocalObjectEvents != 0) {
        memcpy(data->visibleObjectEvents, data->localObjectEvents,
            data->numLocalObjectEvents * sizeof(ObjectEvent));
    }
    data->objectEvents = data->visibleObjectEvents;

    u32 activeCount = 0;
    for (u32 i = 0; i < data->numLocalObjectEvents; i++) {
        const ObjectEvent *event = &data->localObjectEvents[i];
        if (event->script == 0xffff || !FieldSystem_CheckFlag(fieldSystem, event->hiddenFlag)) {
            activeCount++;
        }
    }
    int playerX = PlayerAvatar_GetXPos(fieldSystem->playerAvatar);
    int playerZ = PlayerAvatar_GetZPos(fieldSystem->playerAvatar);

    int width = MapMatrix_GetWidth(fieldSystem->mapMatrix);
    int height = MapMatrix_GetHeight(fieldSystem->mapMatrix);
    int visited[CAMERA_DISTANCE_MAX_CHUNKS];
    int visitedCount = 0;
    // Prefer nearby areas when the object manager reaches its fixed capacity.
    for (int ring = 0; ring <= radius; ring++) {
        for (int z = centerZ - ring; z <= centerZ + ring; z++) {
            for (int x = centerX - ring; x <= centerX + ring; x++) {
                if (x < 0 || z < 0 || x >= width || z >= height
                    || (ring != 0 && x != centerX - ring && x != centerX + ring
                        && z != centerZ - ring && z != centerZ + ring)) {
                    continue;
                }
                int owner = MapMatrix_GetMapHeaderIDAtCoords(fieldSystem->mapMatrix, x, z);
                if (owner == currentHeader || owner == MAP_HEADER_EVERYWHERE) {
                    continue;
                }
                int i;
                for (i = 0; i < visitedCount && visited[i] != owner; i++) { }
                if (i < visitedCount) {
                    continue;
                }
                visited[visitedCount++] = owner;

                u32 events[512];
                int archive = MapHeader_GetEventsArchiveID(owner);
                u32 size = NARC_GetMemberSizeByIndexPair(NARC_INDEX_FIELDDATA__EVENTDATA__ZONE_EVENT, archive);
                if (size < 8 || size > sizeof(events)) {
                    continue;
                }
                NARC_ReadWholeMemberByIndexPair(events, NARC_INDEX_FIELDDATA__EVENTDATA__ZONE_EVENT, archive);
                if (events[0] > (size - 8) / sizeof(BgEvent)) {
                    continue;
                }
                u32 offset = 4 + events[0] * sizeof(BgEvent);
                u32 count = *(u32 *)((u8 *)events + offset);
                offset += 4;
                if (count > (size - offset) / sizeof(ObjectEvent)) {
                    continue;
                }
                const ObjectEvent *objects = (const ObjectEvent *)((u8 *)events + offset);
                for (u32 j = 0; j < count; j++) {
                    const ObjectEvent *event = &objects[j];
                    if (event->script == 0xffff || FieldSystem_CheckFlag(fieldSystem, event->hiddenFlag)
                        || event->x / 32 < centerX - radius || event->x / 32 > centerX + radius
                        || event->z / 32 < centerZ - radius || event->z / 32 > centerZ + radius
                        || ObjectView_Contains(data, event, owner, currentHeader)) {
                        continue;
                    }
                    u32 slot = data->numObjectEvents;
                    if (activeCount >= MAP_OBJECT_VIEW_ACTIVE_LIMIT || slot >= capacity) {
                        // A full manager must prefer nearby objects, not the
                        // first entries in the first neighbouring event file.
                        u32 farthest = ObjectView_Distance(event, playerX, playerZ);
                        for (u32 k = data->numLocalObjectEvents; k < data->numObjectEvents; k++) {
                            u32 distance = ObjectView_Distance(&data->visibleObjectEvents[k], playerX, playerZ);
                            if (distance > farthest) {
                                farthest = distance;
                                slot = k;
                            }
                        }
                        if (slot == data->numObjectEvents) {
                            continue;
                        }
                    } else {
                        data->numObjectEvents++;
                        activeCount++;
                    }
                    ObjectEvent *alias = &data->visibleObjectEvents[slot];
                    *alias = *event;
                    // Native neighbouring-area aliases retain identity and pose,
                    // and become regular interactive NPCs on entering their area.
                    alias->script = 0xffff;
                    alias->hiddenFlag = owner;
                    alias->trainerType = 0;
                }
            }
        }
    }
    return TRUE;
}

BOOL MapHeaderData_UpdateObjectView(FieldSystem *fieldSystem)
{
    return CameraDistance_FilterObjectEvents(fieldSystem, UpdateObjectView(fieldSystem));
}


void MapHeaderData_RefreshObjectView(FieldSystem *fieldSystem)
{
    if (!MapHeaderData_UpdateObjectView(fieldSystem)) {
        return;
    }

    MapHeaderData *data = fieldSystem->mapHeaderData;
    int header = fieldSystem->location->mapHeaderID;
    sub_0206184C(fieldSystem->mapObjMan, header, header, data->numObjectEvents, data->objectEvents);
    // Only add neighbouring objects. Replaying local events would resurrect NPCs
    // removed by a running script, or duplicate objects restored from a save.
    for (u32 i = data->numLocalObjectEvents; i < data->numObjectEvents; i++) {
        MapObjectMan_AddMapObjectFromHeader(fieldSystem->mapObjMan, &data->objectEvents[i], header);
    }
}
#endif

void MapHeaderData_Init(FieldSystem *fieldSystem, enum HeapID heapID)
{
    GF_ASSERT(fieldSystem->mapHeaderData == NULL);
    fieldSystem->mapHeaderData = Heap_Alloc(heapID, sizeof(MapHeaderData));
}

void MapHeaderData_Free(FieldSystem *fieldSystem)
{
    GF_ASSERT(fieldSystem->mapHeaderData != NULL);
    Heap_Free(fieldSystem->mapHeaderData);
}

void MapHeaderData_Load(FieldSystem *fieldSystem, enum MapHeaderID mapHeaderID)
{
    GF_ASSERT(fieldSystem->mapHeaderData != NULL);

    MapHeaderData_LoadEvents(fieldSystem->mapHeaderData, mapHeaderID);
    MapHeaderData_ParseEvents(fieldSystem->mapHeaderData);
    MapHeaderData_LoadWildEncounters(&fieldSystem->mapHeaderData->wildEncounters, mapHeaderID);
    MapHeaderData_LoadInitScripts(fieldSystem->mapHeaderData, mapHeaderID);
}

static void MapHeaderData_LoadEvents(MapHeaderData *data, enum MapHeaderID mapHeaderID)
{
    int eventsID = MapHeader_GetEventsArchiveID(mapHeaderID);
    GF_ASSERT(NARC_GetMemberSizeByIndexPair(NARC_INDEX_FIELDDATA__EVENTDATA__ZONE_EVENT, eventsID) < sizeof(data->tmpEventsBuf));
    NARC_ReadWholeMemberByIndexPair(data->tmpEventsBuf, NARC_INDEX_FIELDDATA__EVENTDATA__ZONE_EVENT, eventsID);
}

void sub_0203A418(FieldSystem *fieldSystem)
{
#ifdef SDK_PORT
    MapHeaderData_UpdateObjectView(fieldSystem);
#endif
    int numObjectEvents = fieldSystem->mapHeaderData->numObjectEvents;

    GF_ASSERT(fieldSystem->mapHeaderData != NULL);

    if (numObjectEvents != 0) {
        sub_02062068(fieldSystem->mapObjMan, fieldSystem->location->mapHeaderID, numObjectEvents, fieldSystem->mapHeaderData->objectEvents);
    }
}

const BgEvent *MapHeaderData_GetBgEvents(const FieldSystem *fieldSystem)
{
    return fieldSystem->mapHeaderData->bgEvents;
}

int MapHeaderData_GetNumBgEvents(const FieldSystem *fieldSystem)
{
    return fieldSystem->mapHeaderData->numBgEvents;
}

const WarpEvent *MapHeaderData_GetWarpEventByIndex(const FieldSystem *fieldSystem, int index)
{
    return (index >= fieldSystem->mapHeaderData->numWarpEvents)
        ? NULL
        : &fieldSystem->mapHeaderData->warpEvents[index];
}

int MapHeaderData_GetIndexOfWarpEventAtPos(const FieldSystem *fieldSystem, int x, int z)
{
    for (int i = 0; i < fieldSystem->mapHeaderData->numWarpEvents; i++) {
        if (fieldSystem->mapHeaderData->warpEvents[i].x == x
            && fieldSystem->mapHeaderData->warpEvents[i].z == z) {
            return i;
        }
    }

    return -1;
}

int MapHeaderData_GetNumCoordEvents(const FieldSystem *fieldSystem)
{
    return fieldSystem->mapHeaderData->numCoordEvents;
}

const CoordEvent *MapHeaderData_GetCoordEvents(const FieldSystem *fieldSystem)
{
    return fieldSystem->mapHeaderData->coordEvents;
}

u32 MapHeaderData_GetNumObjectEvents(const FieldSystem *fieldSystem)
{
    return fieldSystem->mapHeaderData->numObjectEvents;
}

const ObjectEvent *MapHeaderData_GetObjectEvents(const FieldSystem *fieldSystem)
{
    return fieldSystem->mapHeaderData->objectEvents;
}

BOOL MapHeaderData_SetObjectEventPos(FieldSystem *fieldSystem, int localID, u16 x, u16 z)
{
    int i;
    ObjectEvent *objectEvent = fieldSystem->mapHeaderData->objectEvents;
    u32 numObjectEvents = fieldSystem->mapHeaderData->numObjectEvents;

    for (i = 0; i < numObjectEvents; i++) {
        if (objectEvent[i].localID == localID) {
            objectEvent[i].x = x;
            objectEvent[i].z = z;
            return TRUE;
        }
    }

    GF_ASSERT(FALSE);
    return FALSE;
}

BOOL MapHeaderData_SetObjectEventDir(FieldSystem *fieldSystem, int localID, int dir)
{
    int i;
    ObjectEvent *objectEvent = fieldSystem->mapHeaderData->objectEvents;
    u32 numObjectEvents = fieldSystem->mapHeaderData->numObjectEvents;

    for (i = 0; i < numObjectEvents; i++) {
        if (objectEvent[i].localID == localID) {
            objectEvent[i].dir = dir;
            return TRUE;
        }
    }

    GF_ASSERT(FALSE);
    return FALSE;
}

BOOL MapHeaderData_SetObjectEventMovementType(FieldSystem *fieldSystem, int localID, int movementType)
{
    int i;
    ObjectEvent *objectEvent = fieldSystem->mapHeaderData->objectEvents;
    u32 numObjectEvents = fieldSystem->mapHeaderData->numObjectEvents;

    for (i = 0; i < numObjectEvents; i++) {
        if (objectEvent[i].localID == localID) {
            objectEvent[i].movementType = movementType;
            return TRUE;
        }
    }

    GF_ASSERT(FALSE);
    return FALSE;
}

BOOL MapHeaderData_SetWarpEventPos(FieldSystem *fieldSystem, u16 index, u16 x, u16 z)
{
    WarpEvent *warpEvent = fieldSystem->mapHeaderData->warpEvents;
    warpEvent[index].x = x;
    warpEvent[index].z = z;
    return TRUE;
}

BOOL MapHeaderData_SetWarpEventDestHeaderID(FieldSystem *fieldSystem, u16 index, u16 destHeaderID)
{
    WarpEvent *warpEvents = fieldSystem->mapHeaderData->warpEvents;
    warpEvents[index].destHeaderID = destHeaderID;
    return TRUE;
}

BOOL MapHeaderData_SetWarpEventDestWarpID(FieldSystem *fieldSystem, u16 index, u16 destWarpID)
{
    WarpEvent *warpEvents = fieldSystem->mapHeaderData->warpEvents;
    warpEvents[index].destWarpID = destWarpID;
    return TRUE;
}

BOOL MapHeaderData_SetBgEventPos(FieldSystem *fieldSystem, u16 index, u16 x, u16 z)
{
    BgEvent *bgEvent = MapHeaderData_GetBgEvents(fieldSystem);

    bgEvent += index;
    bgEvent->x = x;
    bgEvent->z = z;

    return TRUE;
}

#define CONSUME_EVENTS(T, dataTEvents, dataNumTEvents) \
    do {                                               \
        (dataNumTEvents) = *(u32 *)events;             \
        events += sizeof(u32);                         \
        if ((dataNumTEvents) != 0) {                   \
            (dataTEvents) = (const T *)events;         \
        } else {                                       \
            (dataTEvents) = NULL;                      \
        }                                              \
        events += sizeof(T) * (dataNumTEvents);        \
    } while (0)

static void MapHeaderData_ParseEvents(MapHeaderData *data)
{
    const u8 *events = (const u8 *)data->tmpEventsBuf;

    CONSUME_EVENTS(BgEvent, data->bgEvents, data->numBgEvents);
    CONSUME_EVENTS(ObjectEvent, data->objectEvents, data->numObjectEvents);
    CONSUME_EVENTS(WarpEvent, data->warpEvents, data->numWarpEvents);
    CONSUME_EVENTS(CoordEvent, data->coordEvents, data->numCoordEvents);
#ifdef SDK_PORT
    data->localObjectEvents = (ObjectEvent *)data->objectEvents;
    data->numLocalObjectEvents = data->numObjectEvents;
    data->objectViewX = -1;
    data->objectViewZ = -1;
    data->objectViewRadius = -1;
#endif
}

void MapHeaderData_LoadWildEncounters(WildEncounters *data, enum MapHeaderID mapHeaderID)
{
    memset(data, 0, sizeof(WildEncounters));
    if (MapHeader_HasWildEncounters(mapHeaderID)) {
        enum NarcID narcID = (GAME_VERSION == VERSION_DIAMOND || GAME_VERSION == VERSION_PLATINUM)
            ? NARC_INDEX_FIELDDATA__ENCOUNTDATA__PL_ENC_DATA
            : NARC_INDEX_FIELDDATA__ENCOUNTDATA__P_ENC_DATA;
        NARC_ReadWholeMemberByIndexPair(data, narcID, MapHeader_GetWildEncountersArchiveID(mapHeaderID));
    }
}

const WildEncounters *MapHeaderData_GetWildEncounters(const FieldSystem *fieldSystem)
{
    return &fieldSystem->mapHeaderData->wildEncounters;
}

static void MapHeaderData_LoadInitScripts(MapHeaderData *data, int headerID)
{
    int initScriptsID = MapHeader_GetInitScriptsArchiveID(headerID);

    MI_CpuClearFast(data->initScripts, sizeof(data->initScripts));
    GF_ASSERT(NARC_GetMemberSizeByIndexPair(NARC_INDEX_FIELDDATA__SCRIPT__SCR_SEQ, initScriptsID) < sizeof(data->initScripts));

    NARC_ReadWholeMemberByIndexPair(data->initScripts, NARC_INDEX_FIELDDATA__SCRIPT__SCR_SEQ, initScriptsID);
}

const u8 *MapHeaderData_GetInitScriptBytes(const FieldSystem *fieldSystem)
{
    GF_ASSERT(fieldSystem->mapHeaderData != NULL);
    return (const u8 *)&fieldSystem->mapHeaderData->initScripts;
}

BOOL MapHeaderData_IsPosFreeOfObjectEvents(const FieldSystem *fieldSystem, u16 x, u16 z)
{
    const MapHeaderData *data = fieldSystem->mapHeaderData;

    for (u32 i = 0; i < data->numObjectEvents; i++) {
        if (data->objectEvents[i].x == x && data->objectEvents[i].z == z) {
            return FALSE;
        }
    }

    return TRUE;
}
