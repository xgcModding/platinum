/* Generic Lua/native bridge. Game symbols, signatures and behavior belong to
 * scripts. This file intentionally includes no game headers or mod-specific API. */
#include "mods/lua_mod.h"
#include <ffi.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ARGS 24
#define BUFFER_MT "mod.buffer"

typedef union Value {
    ffi_arg word;
    uint64_t u64;
    int64_t i64;
    uint32_t u32;
    int32_t i32;
    uint16_t u16;
    int16_t i16;
    uint8_t u8;
    int8_t i8;
    float f;
    double d;
    void *ptr;
} Value;

typedef struct Signature {
    ffi_cif cif;
    ffi_type *args[MAX_ARGS];
    void *function;
} Signature;

typedef struct LuaHook {
    struct LuaHook *next;
    LuaMod *mod;
    Signature signature;
    ffi_closure *closure;
    void *code;
    void *original;
    PokePlatinumModHook *handle;
    int callback;
    int failed;
} LuaHook;

typedef struct LuaCallback {
    struct LuaCallback *next;
    LuaMod *mod;
    int ref;
    int failed;
} LuaCallback;

struct LuaMod {
    lua_State *state;
    pthread_t ownerThread;
    PokePlatinumMod descriptor;
    const PokePlatinumModHost *host;
    LuaHook *hooks;
    LuaCallback *callbacks;
    int module;
    int loading;
    int frameFailed;
    int eventFailed;
    unsigned depth;
    char id[128], name[128], version[64];
    char directory[4096];
};

typedef struct OriginalCall {
    LuaHook *hook;
    int active;
    int called;
    Value result;
} OriginalCall;

static LuaMod *Context(lua_State *L)
{
    return *(LuaMod **)lua_getextraspace(L);
}

static void LogError(LuaMod *mod)
{
    const char *error = lua_tostring(mod->state, -1);
    if (mod->host) mod->host->log(mod->id, error ? error : "Lua error");
    else fprintf(stderr, "[mods] %s: %s\n", mod->id, error ? error : "Lua error");
}

static void Budget(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    luaL_error(L, "Lua callback instruction limit exceeded");
}

static int ProtectedCall(LuaMod *mod, int args, int results)
{
    /* Nested calls share the outer budget: game calls can re-enter Lua hooks. */
    if (mod->depth++ == 0) lua_sethook(mod->state, Budget, LUA_MASKCOUNT, 1000000);
    int status = lua_pcall(mod->state, args, results, 0);
    if (--mod->depth == 0) lua_sethook(mod->state, NULL, 0, 0);
    if (status != LUA_OK) LogError(mod);
    return status == LUA_OK;
}

static ffi_type *Type(lua_State *L, int index, int allowVoid)
{
    static const struct { const char *name; ffi_type *type; } types[] = {
        {"void", &ffi_type_void}, {"ptr", &ffi_type_pointer},
        {"u8", &ffi_type_uint8}, {"i8", &ffi_type_sint8},
        {"u16", &ffi_type_uint16}, {"i16", &ffi_type_sint16},
        {"u32", &ffi_type_uint32}, {"i32", &ffi_type_sint32},
        {"u64", &ffi_type_uint64}, {"i64", &ffi_type_sint64},
        {"float", &ffi_type_float}, {"double", &ffi_type_double},
    };
    const char *name = luaL_checkstring(L, index);
    for (size_t i = allowVoid ? 0 : 1; i < sizeof(types) / sizeof(types[0]); i++)
        if (!strcmp(name, types[i].name)) return types[i].type;
    luaL_error(L, "unsupported native type: %s", name);
    return NULL;
}

static void *Pointer(lua_State *L, int index)
{
    if (lua_isnil(L, index)) return NULL;
    if (lua_islightuserdata(L, index)) return lua_touserdata(L, index);
    return luaL_checkudata(L, index, BUFFER_MT);
}

static void PushValue(lua_State *L, ffi_type *type, const void *data)
{
    Value v = {0};
    if (type == &ffi_type_void) { lua_pushnil(L); return; }
    memcpy(&v, data, type->size);
    if (type == &ffi_type_pointer) {
        if (v.ptr) lua_pushlightuserdata(L, v.ptr); else lua_pushnil(L);
    } else if (type == &ffi_type_float) lua_pushnumber(L, v.f);
    else if (type == &ffi_type_double) lua_pushnumber(L, v.d);
    else if (type == &ffi_type_uint8) lua_pushinteger(L, v.u8);
    else if (type == &ffi_type_sint8) lua_pushinteger(L, v.i8);
    else if (type == &ffi_type_uint16) lua_pushinteger(L, v.u16);
    else if (type == &ffi_type_sint16) lua_pushinteger(L, v.i16);
    else if (type == &ffi_type_uint32) lua_pushinteger(L, v.u32);
    else if (type == &ffi_type_sint32) lua_pushinteger(L, v.i32);
    else lua_pushinteger(L, v.i64);
}

static void GetValue(lua_State *L, int index, ffi_type *type, Value *v)
{
    memset(v, 0, sizeof(*v));
    if (type == &ffi_type_void) return;
    if (type == &ffi_type_pointer) { v->ptr = Pointer(L, index); return; }
    if (type == &ffi_type_float) { v->f = luaL_checknumber(L, index); return; }
    if (type == &ffi_type_double) { v->d = luaL_checknumber(L, index); return; }
    lua_Integer n = luaL_checkinteger(L, index);
#define INTEGER(t, field, min, max) \
    if (type == &ffi_type_##t) { \
        luaL_argcheck(L, n >= (lua_Integer)(min) && n <= (lua_Integer)(max), index, "integer out of range"); \
        v->field = n; return; \
    }
    INTEGER(uint8, u8, 0, UINT8_MAX)
    INTEGER(sint8, i8, INT8_MIN, INT8_MAX)
    INTEGER(uint16, u16, 0, UINT16_MAX)
    INTEGER(sint16, i16, INT16_MIN, INT16_MAX)
    INTEGER(uint32, u32, 0, UINT32_MAX)
    INTEGER(sint32, i32, INT32_MIN, INT32_MAX)
#undef INTEGER
    v->i64 = n;
}

static void Prepare(lua_State *L, Signature *s, int resultIndex, int argsIndex)
{
    ffi_type *result = Type(L, resultIndex, 1);
    luaL_checktype(L, argsIndex, LUA_TTABLE);
    size_t count = lua_rawlen(L, argsIndex);
    luaL_argcheck(L, count <= MAX_ARGS, argsIndex, "too many native arguments");
    for (size_t i = 0; i < count; i++) {
        lua_rawgeti(L, argsIndex, i + 1);
        s->args[i] = Type(L, -1, 0);
        lua_pop(L, 1);
    }
    if (ffi_prep_cif(&s->cif, FFI_DEFAULT_ABI, count, result, s->args) != FFI_OK)
        luaL_error(L, "invalid native signature");
}

static void CallNative(lua_State *L, Signature *s, void *function, Value *result)
{
    luaL_argcheck(L, lua_gettop(L) == (int)s->cif.nargs, 1, "wrong native argument count");
    Value values[MAX_ARGS];
    void *args[MAX_ARGS];
    for (unsigned i = 0; i < s->cif.nargs; i++) {
        GetValue(L, i + 1, s->args[i], &values[i]);
        args[i] = &values[i];
    }
    memset(result, 0, sizeof(*result));
    ffi_call(&s->cif, FFI_FN(function), result, args);
}

static int BoundCall(lua_State *L)
{
    Signature *s = lua_touserdata(L, lua_upvalueindex(1));
    Value result;
    CallNative(L, s, s->function, &result);
    PushValue(L, s->cif.rtype, &result);
    return 1;
}

static int Bind(lua_State *L)
{
    LuaMod *mod = Context(L);
    luaL_argcheck(L, mod->host != NULL, 1, "bind functions inside on_load");
    const char *symbol = luaL_checkstring(L, 1);
    Signature *s = lua_newuserdatauv(L, sizeof(*s), 0);
    memset(s, 0, sizeof(*s));
    Prepare(L, s, 2, 3);
    s->function = mod->host->resolveSymbol(symbol);
    if (!s->function) return luaL_error(L, "game symbol not found: %s", symbol);
    lua_pushcclosure(L, BoundCall, 1);
    return 1;
}

static int Symbol(lua_State *L)
{
    LuaMod *mod = Context(L);
    luaL_argcheck(L, mod->host != NULL, 1, "resolve symbols inside on_load");
    const char *name = luaL_checkstring(L, 1);
    void *address = mod->host->resolveSymbol(name);
    if (!address) return luaL_error(L, "game symbol not found: %s", name);
    lua_pushlightuserdata(L, address);
    return 1;
}

static int Original(lua_State *L)
{
    OriginalCall *call = lua_touserdata(L, lua_upvalueindex(1));
    if (!call->active) return luaL_error(L, "original is only valid during its hook callback");
    CallNative(L, &call->hook->signature, call->hook->original, &call->result);
    call->called = 1;
    PushValue(L, call->hook->signature.cif.rtype, &call->result);
    return 1;
}

/* Conversion is protected too: an invalid Lua return must never longjmp across
 * the native trampoline. Small integer returns occupy a full ffi_arg. */
static int ConvertReturn(lua_State *L)
{
    ffi_type *type = lua_touserdata(L, lua_upvalueindex(1));
    Value *result = lua_touserdata(L, lua_upvalueindex(2));
    GetValue(L, 1, type, result);
    if (type == &ffi_type_sint8) result->word = (ffi_sarg)result->i8;
    else if (type == &ffi_type_sint16) result->word = (ffi_sarg)result->i16;
    else if (type == &ffi_type_sint32) result->word = (ffi_sarg)result->i32;
    return 0;
}

static void HookCallback(ffi_cif *cif, void *result, void **args, void *userData)
{
    LuaHook *hook = userData;
    /* Lua states are confined to the game thread. Foreign-thread calls keep
     * their native behavior rather than racing or deadlocking the interpreter. */
    if (!pthread_equal(hook->mod->ownerThread, pthread_self()) || hook->failed) {
        ffi_call(cif, FFI_FN(hook->original), result, args);
        return;
    }
    LuaMod *mod = hook->mod;
    lua_State *L = mod->state;
    int top = lua_gettop(L);
    if (!lua_checkstack(L, (int)cif->nargs + 8)) {
        ffi_call(cif, FFI_FN(hook->original), result, args);
        return;
    }
    OriginalCall *call = lua_newuserdatauv(L, sizeof(*call), 0);
    *call = (OriginalCall){.hook = hook, .active = 1};
    int callIndex = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, hook->callback);
    lua_pushvalue(L, callIndex);
    lua_pushcclosure(L, Original, 1);
    for (unsigned i = 0; i < cif->nargs; i++) PushValue(L, cif->arg_types[i], args[i]);
    int ok = ProtectedCall(mod, cif->nargs + 1, 1);
    call->active = 0;
    Value converted = {0};
    if (ok) {
        lua_pushlightuserdata(L, cif->rtype);
        lua_pushlightuserdata(L, &converted);
        lua_pushcclosure(L, ConvertReturn, 2);
        lua_pushvalue(L, -2);
        ok = ProtectedCall(mod, 1, 0);
    }
    if (!ok) {
        hook->failed = 1;
        /* A callback may fail after calling original. Do not repeat its side effects. */
        if (call->called) converted = call->result;
        else ffi_call(cif, FFI_FN(hook->original), &converted, args);
    }
    if (cif->rtype != &ffi_type_void) {
        size_t size = cif->rtype->size;
        if (cif->rtype != &ffi_type_float && size < sizeof(ffi_arg)) size = sizeof(ffi_arg);
        memcpy(result, &converted, size);
    }
    lua_settop(L, top);
}

static int Hook(lua_State *L)
{
    LuaMod *mod = Context(L);
    luaL_argcheck(L, mod->loading, 1, "install hooks inside on_load");
    const char *symbol = luaL_checkstring(L, 1);
    luaL_checktype(L, 4, LUA_TFUNCTION);
    /* Validate with Lua-owned storage first so a bad signature cannot leak. */
    Signature *s = lua_newuserdatauv(L, sizeof(*s), 0);
    Prepare(L, s, 2, 3);
    LuaHook *hook = calloc(1, sizeof(*hook));
    if (!hook) return luaL_error(L, "out of memory");
    hook->mod = mod;
    hook->signature = *s;
    ffi_prep_cif(&hook->signature.cif, FFI_DEFAULT_ABI, s->cif.nargs, s->cif.rtype, hook->signature.args);
    hook->next = mod->hooks;
    mod->hooks = hook;
    lua_pushvalue(L, 4);
    hook->callback = luaL_ref(L, LUA_REGISTRYINDEX);
    hook->closure = ffi_closure_alloc(sizeof(ffi_closure), &hook->code);
    if (!hook->closure || ffi_prep_closure_loc(hook->closure, &hook->signature.cif,
            HookCallback, hook, hook->code) != FFI_OK)
        return luaL_error(L, "could not create Lua hook: %s", symbol);
    hook->handle = mod->host->installTrampolineHook(symbol, hook->code, &hook->original);
    if (!hook->handle) return luaL_error(L, "could not install Lua hook: %s", symbol);
    return 0;
}

static void NativeCallback(ffi_cif *cif, void *result, void **args, void *userData)
{
    LuaHook *callback = userData;
    LuaMod *mod = callback->mod;
    Value converted = {0};
    if (pthread_equal(mod->ownerThread, pthread_self()) && !callback->failed) {
        lua_State *L = mod->state;
        int top = lua_gettop(L);
        if (lua_checkstack(L, (int)cif->nargs + 8)) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, callback->callback);
            for (unsigned i = 0; i < cif->nargs; i++) PushValue(L, cif->arg_types[i], args[i]);
            int ok = ProtectedCall(mod, cif->nargs, 1);
            if (ok) {
                lua_pushlightuserdata(L, cif->rtype);
                lua_pushlightuserdata(L, &converted);
                lua_pushcclosure(L, ConvertReturn, 2);
                lua_pushvalue(L, -2);
                ok = ProtectedCall(mod, 1, 0);
            }
            callback->failed = !ok;
        }
        lua_settop(L, top);
    }
    if (cif->rtype != &ffi_type_void) {
        size_t size = cif->rtype->size;
        if (cif->rtype != &ffi_type_float && size < sizeof(ffi_arg)) size = sizeof(ffi_arg);
        memcpy(result, &converted, size);
    }
}

static int Callback(lua_State *L)
{
    LuaMod *mod = Context(L);
    luaL_argcheck(L, mod->loading, 1, "create native callbacks inside on_load");
    luaL_checktype(L, 3, LUA_TFUNCTION);
    Signature *s = lua_newuserdatauv(L, sizeof(*s), 0);
    Prepare(L, s, 1, 2);
    LuaHook *callback = calloc(1, sizeof(*callback));
    if (!callback) return luaL_error(L, "out of memory");
    callback->mod = mod;
    callback->signature = *s;
    ffi_prep_cif(&callback->signature.cif, FFI_DEFAULT_ABI, s->cif.nargs, s->cif.rtype, callback->signature.args);
    callback->next = mod->hooks;
    mod->hooks = callback;
    lua_pushvalue(L, 3);
    callback->callback = luaL_ref(L, LUA_REGISTRYINDEX);
    callback->closure = ffi_closure_alloc(sizeof(ffi_closure), &callback->code);
    if (!callback->closure || ffi_prep_closure_loc(callback->closure, &callback->signature.cif,
            NativeCallback, callback, callback->code) != FFI_OK)
        return luaL_error(L, "could not create native callback");
    lua_pushlightuserdata(L, callback->code);
    return 1;
}

static int ReadFile(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    luaL_argcheck(L, *name && !strchr(name, '/') && !strchr(name, '\\')
        && strcmp(name, ".") && strcmp(name, ".."), 1, "expected a filename beside the mod");
    char path[8192];
    snprintf(path, sizeof(path), "%s/%s", Context(L)->directory, name);
    FILE *file = fopen(path, "rb");
    if (!file) { lua_pushnil(L); return 1; }
    luaL_Buffer buffer;
    luaL_buffinit(L, &buffer);
    char bytes[4096];
    size_t count;
    while ((count = fread(bytes, 1, sizeof(bytes), file)) != 0) luaL_addlstring(&buffer, bytes, count);
    int failed = ferror(file);
    fclose(file);
    if (failed) return luaL_error(L, "could not read mod resource: %s", name);
    luaL_pushresult(&buffer);
    return 1;
}

static int Bytes(lua_State *L)
{
    size_t size;
    const char *bytes = luaL_checklstring(L, 1, &size);
    void *buffer = lua_newuserdatauv(L, size + 1, 0);
    memcpy(buffer, bytes, size);
    ((char *)buffer)[size] = '\0';
    luaL_setmetatable(L, BUFFER_MT);
    return 1;
}

static int SizeOf(lua_State *L)
{
    ffi_type *type = Type(L, 1, 0);
    lua_pushinteger(L, type->size);
    lua_pushinteger(L, type->alignment);
    return 2;
}

static int Allocate(lua_State *L)
{
    lua_Integer size = luaL_checkinteger(L, 1);
    luaL_argcheck(L, size > 0 && size <= 64 * 1024 * 1024, 1, "invalid buffer size");
    void *buffer = lua_newuserdatauv(L, size, 0);
    memset(buffer, 0, size);
    luaL_setmetatable(L, BUFFER_MT);
    return 1;
}

static void *Memory(lua_State *L, int index, lua_Integer offset, size_t size)
{
    void *pointer = Pointer(L, index);
    luaL_argcheck(L, pointer != NULL && offset >= 0, index, "null pointer or negative offset");
    if (lua_type(L, index) == LUA_TUSERDATA) {
        size_t length = lua_rawlen(L, index);
        luaL_argcheck(L, (uint64_t)offset <= length && size <= length - (size_t)offset,
            index, "buffer access out of bounds");
    }
    return (char *)pointer + offset;
}

static int Read(lua_State *L)
{
    ffi_type *type = Type(L, 2, 0);
    void *source = Memory(L, 1, luaL_optinteger(L, 3, 0), type->size);
    PushValue(L, type, source);
    return 1;
}

static int Write(lua_State *L)
{
    ffi_type *type = Type(L, 2, 0);
    void *destination = Memory(L, 1, luaL_optinteger(L, 4, 0), type->size);
    Value value;
    GetValue(L, 3, type, &value);
    memcpy(destination, &value, type->size);
    return 0;
}

static int Address(lua_State *L)
{
    lua_pushlightuserdata(L, Memory(L, 1, luaL_optinteger(L, 2, 0), 0));
    return 1;
}

static int Copy(lua_State *L)
{
    lua_Integer size = luaL_checkinteger(L, 3);
    luaL_argcheck(L, size >= 0, 3, "negative copy size");
    memmove(Memory(L, 1, 0, size), Memory(L, 2, 0, size), size);
    return 0;
}

static void OptionChanged(uint32_t selected, void *userData)
{
    LuaCallback *callback = userData;
    if (callback->failed) return;
    lua_State *L = callback->mod->state;
    int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, callback->ref);
    lua_pushinteger(L, selected);
    callback->failed = !ProtectedCall(callback->mod, 1, 0);
    lua_settop(L, top);
}

static int Option(lua_State *L)
{
    LuaMod *mod = Context(L);
    luaL_argcheck(L, mod->loading, 1, "register options inside on_load");
    const char *key = luaL_optstring(L, 1, NULL);
    const char *label = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    size_t count = lua_rawlen(L, 3);
    luaL_argcheck(L, count > 0 && count <= POKEPLATINUM_MOD_MAX_OPTION_VALUES, 3, "invalid option values");
    lua_Integer selected = luaL_checkinteger(L, 4);
    luaL_argcheck(L, selected >= 0 && (size_t)selected < count, 4, "invalid default option");
    uint32_t flags = (uint32_t)luaL_optinteger(L, 6, 0);
    if (!(flags & MOD_OPTION_FLAG_READ_ONLY)) luaL_checktype(L, 5, LUA_TFUNCTION);
    const char *values[POKEPLATINUM_MOD_MAX_OPTION_VALUES];
    for (size_t i = 0; i < count; i++) {
        lua_rawgeti(L, 3, i + 1);
        values[i] = luaL_checkstring(L, -1);
        /* Keep strings on the stack until the host has copied them. */
    }
    LuaCallback *callback = calloc(1, sizeof(*callback));
    if (!callback) return luaL_error(L, "out of memory");
    callback->mod = mod;
    lua_pushvalue(L, 5);
    callback->ref = luaL_ref(L, LUA_REGISTRYINDEX);
    callback->next = mod->callbacks;
    mod->callbacks = callback;
    PokePlatinumModOption option = {
        .structSize = sizeof(option), .label = label, .values = values,
        .valueCount = count, .selectedValue = selected,
        .onChanged = (flags & MOD_OPTION_FLAG_READ_ONLY) ? NULL : OptionChanged,
        .userData = callback, .configKey = key, .flags = flags,
    };
    if (!mod->host->registerOption(&option)) return luaL_error(L, "could not register option: %s", label);
    return 0;
}

static int OptionLabel(lua_State *L)
{
    LuaMod *mod = Context(L);
    const char *key = luaL_checkstring(L, 1);
    const char *value = luaL_checkstring(L, 2);
    if (!mod->host || !mod->host->setOptionValueLabel
        || !mod->host->setOptionValueLabel(key, value))
        return luaL_error(L, "could not update option: %s", key);
    return 0;
}

static int Log(lua_State *L)
{
    LuaMod *mod = Context(L);
    const char *message = luaL_checkstring(L, 1);
    if (mod->host) mod->host->log(mod->id, message);
    return 0;
}

static int PushMember(LuaMod *mod, const char *name)
{
    lua_State *L = mod->state;
    lua_rawgeti(L, LUA_REGISTRYINDEX, mod->module);
    lua_pushstring(L, name);
    lua_rawget(L, -2);
    lua_remove(L, -2);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return 0; }
    return 1;
}

LuaMod *LuaMod_Open(const char *path)
{
    LuaMod *mod = calloc(1, sizeof(*mod));
    if (!mod) return NULL;
    mod->ownerThread = pthread_self();
    snprintf(mod->id, sizeof(mod->id), "%s", path);
    snprintf(mod->directory, sizeof(mod->directory), "%s", path);
    char *separator = strrchr(mod->directory, '/');
#ifdef _WIN32
    char *backslash = strrchr(mod->directory, '\\');
    if (backslash != NULL && (separator == NULL || backslash > separator)) separator = backslash;
#endif
    if (separator) *separator = '\0';
    else strcpy(mod->directory, ".");
    lua_State *L = mod->state = luaL_newstate();
    if (!L) { free(mod); return NULL; }
    *(LuaMod **)lua_getextraspace(L) = mod;
    const struct { const char *name; lua_CFunction open; } libraries[] = {
        {"_G", luaopen_base}, {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8},
    };
    for (size_t i = 0; i < sizeof(libraries) / sizeof(libraries[0]); i++) {
        luaL_requiref(L, libraries[i].name, libraries[i].open, 1);
        lua_pop(L, 1);
    }
    lua_pushnil(L); lua_setglobal(L, "dofile");
    lua_pushnil(L); lua_setglobal(L, "loadfile");
    luaL_newmetatable(L, BUFFER_MT); lua_pop(L, 1);
    static const luaL_Reg api[] = {
        {"bind", Bind}, {"hook", Hook}, {"sizeof", SizeOf}, {"alloc", Allocate},
        {"read", Read}, {"write", Write}, {"address", Address}, {"copy", Copy},
        {"option", Option}, {"option_label", OptionLabel}, {"log", Log},
        {"symbol", Symbol}, {"callback", Callback}, {"read_file", ReadFile},
        {"bytes", Bytes}, {NULL, NULL},
    };
    luaL_newlib(L, api);
    lua_pushinteger(L, 1); lua_setfield(L, -2, "api_version");
    lua_setglobal(L, "game");
    if (luaL_loadfilex(L, path, "t") != LUA_OK) {
        LogError(mod); LuaMod_Close(mod); return NULL;
    }
    if (!ProtectedCall(mod, 0, 1)) { LuaMod_Close(mod); return NULL; }
    if (!lua_istable(L, -1)) {
        fprintf(stderr, "[mods] %s: Lua mod must return a descriptor table\n", path);
        LuaMod_Close(mod); return NULL;
    }
    const char *keys[] = {"id", "name", "version"};
    char *destinations[] = {mod->id, mod->name, mod->version};
    size_t sizes[] = {sizeof(mod->id), sizeof(mod->name), sizeof(mod->version)};
    for (unsigned i = 0; i < 3; i++) {
        lua_pushstring(L, keys[i]); lua_rawget(L, -2);
        const char *value = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : NULL;
        if (!value || !*value || strlen(value) >= sizes[i]) {
            fprintf(stderr, "[mods] %s: invalid Lua descriptor %s\n", path, keys[i]);
            LuaMod_Close(mod); return NULL;
        }
        snprintf(destinations[i], sizes[i], "%s", value);
        lua_pop(L, 1);
    }
    mod->module = luaL_ref(L, LUA_REGISTRYINDEX);
    mod->descriptor = (PokePlatinumMod){.structSize = sizeof(PokePlatinumMod),
        .id = mod->id, .name = mod->name, .version = mod->version};
    return mod;
}

const PokePlatinumMod *LuaMod_Descriptor(LuaMod *mod) { return &mod->descriptor; }

int LuaMod_Load(LuaMod *mod, const PokePlatinumModHost *host)
{
    mod->host = host;
    mod->loading = 1;
    int top = lua_gettop(mod->state);
    int ok = 1;
    if (PushMember(mod, "on_load")) {
        ok = ProtectedCall(mod, 0, 1);
        if (ok && lua_isboolean(mod->state, -1) && !lua_toboolean(mod->state, -1)) ok = 0;
    }
    mod->loading = 0;
    lua_settop(mod->state, top);
    return ok;
}

void LuaMod_Dispatch(LuaMod *mod, PokePlatinumModEvent event, void *data)
{
    lua_State *L = mod->state;
    int top = lua_gettop(L);
    if (event == MOD_EVENT_FRAME && !mod->frameFailed && PushMember(mod, "on_frame")) {
        mod->frameFailed = !ProtectedCall(mod, 0, 0);
        lua_settop(L, top);
    }
    if (!mod->eventFailed && PushMember(mod, "on_event")) {
        lua_pushinteger(L, event);
        if (data) lua_pushlightuserdata(L, data); else lua_pushnil(L);
        mod->eventFailed = !ProtectedCall(mod, 2, 0);
        lua_settop(L, top);
    }
}

void LuaMod_Unload(LuaMod *mod)
{
    int top = lua_gettop(mod->state);
    if (PushMember(mod, "on_unload")) ProtectedCall(mod, 0, 0);
    lua_settop(mod->state, top);
}

/* The owner must detach all native hooks/options before closing the VM. */
void LuaMod_Close(LuaMod *mod)
{
    if (!mod) return;
    while (mod->hooks) {
        LuaHook *hook = mod->hooks;
        mod->hooks = hook->next;
        if (hook->closure) ffi_closure_free(hook->closure);
        free(hook);
    }
    while (mod->callbacks) {
        LuaCallback *callback = mod->callbacks;
        mod->callbacks = callback->next;
        free(callback);
    }
    lua_close(mod->state);
    free(mod);
}
