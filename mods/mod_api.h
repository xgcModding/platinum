#ifndef POKEPLATINUM_MODS_API_H
#define POKEPLATINUM_MODS_API_H

#include <stdint.h>

#define POKEPLATINUM_MOD_ENTRYPOINT  "PokePlatinumMod_GetDescriptor"
#define POKEPLATINUM_MOD_MAX_OPTIONS 64
#define POKEPLATINUM_MOD_MAX_OPTION_VALUES 32

#if defined(_WIN32)
#define POKEPLATINUM_MOD_EXPORT __declspec(dllexport)
#else
#define POKEPLATINUM_MOD_EXPORT __attribute__((visibility("default")))
#endif

typedef enum PokePlatinumModEvent {
    MOD_EVENT_FRAME = 1,
    MOD_EVENT_FILTER_EXPERIENCE_GAIN = 6,
    MOD_EVENT_CHECK_LEVEL_UP_ITEM = 7,
    MOD_EVENT_SAVE_LOADED = 8,
} PokePlatinumModEvent;

typedef struct PokePlatinumModSaveEvent {
    uint32_t structSize;
    void *saveData;
} PokePlatinumModSaveEvent;

typedef struct PokePlatinumModExperienceEvent {
    uint32_t structSize;
    void *pokemon;
    uint32_t currentExperience;
    uint32_t requestedExperience;
    uint32_t grantedExperience;
} PokePlatinumModExperienceEvent;

typedef struct PokePlatinumModLevelUpItemEvent {
    uint32_t structSize;
    void *pokemon;
    uint16_t itemId;
    uint8_t blocked;
    uint8_t reserved;
} PokePlatinumModLevelUpItemEvent;

typedef enum PokePlatinumModMenuResult {
    MOD_MENU_RESULT_STAY_OPEN = 0,
    MOD_MENU_RESULT_CLOSE = 1,
    MOD_MENU_RESULT_SHOW_LOADED_MODS = 2,
} PokePlatinumModMenuResult;

typedef struct PokePlatinumModInfo {
    uint32_t structSize;
    const char *id;
    const char *name;
    const char *version;
} PokePlatinumModInfo;

typedef PokePlatinumModMenuResult (*PokePlatinumModMenuCallback)(void *gameContext, void *userData);
typedef void (*PokePlatinumModOptionChangedCallback)(uint32_t selectedValue, void *userData);
typedef void (*PokePlatinumModKeyCallback)(int32_t keycode, void *userData);

typedef struct PokePlatinumModOption {
    uint32_t structSize;
    const char *label;
    const char *const *values;
    uint32_t valueCount;
    uint32_t selectedValue;
    PokePlatinumModOptionChangedCallback onChanged;
    void *userData;
    const char *configKey;
    uint32_t flags;
} PokePlatinumModOption;

#define MOD_OPTION_FLAG_READ_ONLY (1u << 0)
#define MOD_OPTION_FLAG_ACTION    (1u << 1)

typedef struct PokePlatinumModHook PokePlatinumModHook;

typedef struct PokePlatinumModHost {
    uint32_t structSize;
    void (*log)(const char *modId, const char *message);
    void *(*resolveSymbol)(const char *name);
    uint32_t (*getLoadedModCount)(void);
    int (*getLoadedModInfo)(uint32_t index, PokePlatinumModInfo *info);
    int (*registerStartMenuItem)(const char *label, PokePlatinumModMenuCallback callback, void *userData);
    PokePlatinumModHook *(*installReplacementHook)(const char *symbolName, void *replacement);
    PokePlatinumModHook *(*installTrampolineHook)(const char *symbolName, void *replacement, void **originalFunction);
    int (*removeHook)(PokePlatinumModHook *hook);
    int (*registerOption)(const PokePlatinumModOption *option);
    int (*setOptionValueLabel)(const char *configKey, const char *value);
    int (*registerKeyBinding)(int32_t keycode, PokePlatinumModKeyCallback callback, void *userData);
} PokePlatinumModHost;

typedef struct PokePlatinumMod {
    uint32_t structSize;
    const char *id;
    const char *name;
    const char *version;
    int (*onLoad)(const PokePlatinumModHost *host);
    void (*onFrame)(void);
    void (*onEvent)(PokePlatinumModEvent event, void *eventData);
    void (*onUnload)(void);
} PokePlatinumMod;

typedef const PokePlatinumMod *(*PokePlatinumModEntrypoint)(void);

#endif // POKEPLATINUM_MODS_API_H
