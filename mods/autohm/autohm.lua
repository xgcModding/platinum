-- AutoHM is a standalone Lua mod. Hooks, native signatures, layouts and all
-- behavior live here; the executable knows nothing about AutoHM.
-- Target: this port's public C ABI (64-bit macOS/Linux). See README.md.
assert(game.api_version == 1, "AutoHM requires Lua mod API 1")
local C = {
    FONT_SYSTEM = 0,
    FieldMoves_Text_PokemonStrengthMadePossibleToMove = 11,
    FieldMoves_Text_PokemonUsedCut = 1,
    FieldMoves_Text_PokemonUsedDefog = 16,
    FieldMoves_Text_PokemonUsedFlash = 26,
    FieldMoves_Text_PokemonUsedRockClimb = 19,
    FieldMoves_Text_PokemonUsedRockSmash = 5,
    FieldMoves_Text_PokemonUsedStrength = 10,
    FieldMoves_Text_PokemonUsedSurf = 13,
    FieldMoves_Text_PokemonUsedWaterfall = 23,
    FieldMoves_Text_StrengthMadePossibleToMove = 9,
    FieldMoves_Text_WouldYouLikeToUseCut = 0,
    FieldMoves_Text_WouldYouLikeToUseDefog_Unused = 15,
    FieldMoves_Text_WouldYouLikeToUseRockClimb = 18,
    FieldMoves_Text_WouldYouLikeToUseRockSmash = 3,
    FieldMoves_Text_WouldYouLikeToUseStrength = 6,
    FieldMoves_Text_WouldYouLikeToUseSurf = 12,
    FieldMoves_Text_WouldYouLikeToUseWaterfall = 22,
    HEAP_ID_FIELD1 = 4,
    HEAP_ID_PARTY_MENU = 12,
    ITEM_HM01 = 420,
    ITEM_HM02 = 421,
    ITEM_HM03 = 422,
    ITEM_HM04 = 423,
    ITEM_HM05 = 424,
    ITEM_HM06 = 425,
    ITEM_HM07 = 426,
    ITEM_HM08 = 427,
    ITEM_NONE = 0,
    ITEM_TM28 = 355,
    ITEM_TM70 = 397,
    MAX_LETTER_HEIGHT_TILES = 2,
    MAX_PARTY_SIZE = 6,
    MENU_YES = 0,
    MOD_EVENT_SAVE_LOADED = 8,
    MON_DATA_CHECKSUM_FAILED = 3,
    MON_DATA_FORM = 112,
    MON_DATA_IS_EGG = 76,
    MON_DATA_SPECIES = 5,
    MOVE_CUT = 15,
    MOVE_DEFOG = 432,
    MOVE_DIG = 91,
    MOVE_FLASH = 148,
    MOVE_FLY = 19,
    MOVE_ROCK_CLIMB = 431,
    MOVE_ROCK_SMASH = 249,
    MOVE_STRENGTH = 70,
    MOVE_SURF = 57,
    MOVE_TELEPORT = 100,
    MOVE_WATERFALL = 127,
    NUM_MENU_COLORS = 128,
    NUM_PARTY_MENU_SPRITES = 29,
    NUM_PARTY_MENU_STRS = 20,
    NUM_PARTY_MENU_WINS = 37,
    PAD_BUTTON_B = 2,
    PARTY_MENU_MODE_FIELD = 0,
    PARTY_MENU_STR_MOVE0 = 16,
    PARTY_SLOT_NONE = 255,
    PartyMenu_Text_FieldMove0 = 157,
    SCRIPT_MANAGER_IS_MSG_BOX_OPEN = 6,
    TEXT_BANK_FIELD_MOVES = 381,
}

-- C structure declarations carried by the mod, equivalent to its old headers.
-- Alignment comes from the running host, rather than hardcoded byte offsets.
local function struct(fields)
    local t = { size = 0, alignment = 1, fields = {} }
    for _, f in ipairs(fields) do
        local size, alignment
        if type(f[2]) == "table" then size, alignment = f[2].size, f[2].alignment
        else size, alignment = game.sizeof(f[2]) end
        t.size = ((t.size + alignment - 1) // alignment) * alignment
        t.fields[f[1]] = { offset = t.size, type = f[2] }
        t.size = t.size + size * (f[3] or 1)
        t.alignment = math.max(t.alignment, alignment)
    end
    t.size = ((t.size + t.alignment - 1) // t.alignment) * t.alignment
    return t
end
local T = {}
T.Window = struct {
    {"bgConfig", "ptr"}, {"bgLayer", "u8"}, {"tilemapLeft", "u8"},
    {"tilemapTop", "u8"}, {"width", "u8"}, {"height", "u8"},
    {"palette", "u8"}, {"tileFlags", "u16"}, {"pixels", "ptr"},
}
T.MenuTemplate = struct {
    {"choices", "ptr"}, {"window", "ptr"}, {"fontID", "u8"},
    {"xSize", "u8"}, {"ySize", "u8"}, {"flags", "u8"},
}
T.MessageLoader = struct {
    {"mode", "u16"}, {"heapID", "u16"}, {"narcID", "u16"},
    {"bankID", "u16"}, {"bank", "ptr"},
}
T.ScriptContext = struct {
    {"stackPointer", "u8"}, {"state", "u8"}, {"comparisonResult", "u8"},
    {"shouldResume", "ptr"}, {"scriptPtr", "ptr"}, {"stack", "ptr", 20},
    {"cmdTable", "ptr"}, {"cmdTableSize", "u32"}, {"data", "u32", 4},
    {"task", "ptr"}, {"loader", "ptr"}, {"scripts", "ptr"}, {"fieldSystem", "ptr"},
}
T.PartyMenu = struct {
    {"party", "ptr"}, {"bag", "ptr"}, {"mailbox", "ptr"}, {"options", "ptr"},
    {"broadcast", "ptr"}, {"battleRegulation", "ptr"}, {"fieldMoveContext", "ptr"},
    {"fieldSystem", "ptr"}, {"mode", "u8"}, {"type", "u8"},
    {"selectedMonSlot", "u8"}, {"menuSelectionResult", "u8"},
    {"usedItemID", "u16"}, {"learnedMove", "u16"}, {"selectedMoveSlot", "u8"},
    {"unk_29", "u8"}, {"contestType", "u8"}, {"contestRank", "u8"},
    {"selectionOrder", "u8", 6}, {"selectionFlags", "u8"}, {"reqLevel", "u8"},
    {"levelUpMoveIndex", "i32"}, {"evoTargetSpecies", "u16"},
    {"padding_3A", "u8", 2}, {"evoType", "i32"},
}
T.PartyMenuMember = struct {
    {"name", "ptr"}, {"species", "u16"}, {"curHP", "u16"}, {"maxHP", "u16"},
    {"level", "u16"}, {"heldItem", "u16"}, {"statusFlags", "u16"},
    {"isEgg", "u8"}, {"form", "u8"}, {"ballSeal", "u16"},
    {"panelXPos", "i8"}, {"panelYPos", "i8"}, {"spriteXDelta", "i16"},
    {"spriteYDelta", "i16"}, {"statusXPos", "i16"}, {"statusYPos", "i16"},
    {"itemXPos", "i16"}, {"itemYPos", "i16"}, {"sprite", "ptr"},
    {"unk_28", "u8"}, {"isPresent", "u8"},
}
T.PartyOrderSwitchData = struct {
    {"bufPanels", "u16", 192}, {"bufWindows", "u16", 192},
    {"slots", "u8", 2}, {"sides", "u8", 2}, {"inProgress", "u8"},
    {"state", "u8"}, {"scrollTicker", "u8"},
}
T.PartyMenuApplication = struct {
    {"bgConfig", "ptr"}, {"windows", T.Window, C.NUM_PARTY_MENU_WINS},
    {"menuWindows", T.Window}, {"leadMemberPanel", "u16", 96},
    {"backMemberPanel", "u16", 96}, {"noneMemberPanel", "u16", 96},
    {"colors", "u16", C.NUM_MENU_COLORS}, {"partyMenu", "ptr"},
    {"spriteSystem", "ptr"}, {"spriteMan", "ptr"},
    {"sprites", "ptr", C.NUM_PARTY_MENU_SPRITES},
    {"sprites_unused", "ptr", C.NUM_PARTY_MENU_SPRITES}, {"specialChars", "ptr"},
    {"messageLoader", "ptr"}, {"template", "ptr"}, {"tmpString", "ptr"},
    {"tmpFormat", "ptr"}, {"menuStrings", "ptr", C.NUM_PARTY_MENU_STRS},
    {"contextMenuChoices", "ptr"}, {"contextMenu", "ptr"},
    {"partyMembers", T.PartyMenuMember, 6}, {"cursorPosTable", "ptr"},
    {"orderSwitch", T.PartyOrderSwitchData}, {"callback", "ptr"},
    {"yesnoCallbacks", "ptr", 2}, {"unk_B0C", "u8"}, {"unk_B0D", "u8"},
    {"stateAfterMessage", "u8"}, {"targetFlags", "u8"}, {"textPrinterID", "u8"},
    {"currPartySlot", "u8"}, {"prevPartySlot", "u8"}, {"callbackState", "u8"},
    {"monStats", "u16", 6}, {"heightWeight", "ptr"}, {"formChanger", "ptr"},
    {"formChange3DPipeline", "ptr"},
}
T.SaveEvent = struct { {"structSize", "u32"}, {"saveData", "ptr"} }
local function get(p, t, field)
    local f = t.fields[field]
    return game.read(p, f.type, f.offset)
end
local function set(p, t, field, value)
    local f = t.fields[field]
    game.write(p, f.type, value, f.offset)
end
local function at(p, t, field)
    return game.address(p, t.fields[field].offset)
end
local pointerSize = game.sizeof("ptr")
local saveData, ignoreParty, animations = nil, false, true
local skippedStorage = game.alloc(1) -- kept alive for the lifetime of this mod
local skippedCutIn = game.address(skippedStorage)
local F = {}
local moves = {
    [C.MOVE_DIG] = C.ITEM_TM28, [C.MOVE_FLASH] = C.ITEM_TM70,
    [C.MOVE_FLY] = C.ITEM_HM02, [C.MOVE_DEFOG] = C.ITEM_HM05,
    [C.MOVE_CUT] = C.ITEM_HM01, [C.MOVE_SURF] = C.ITEM_HM03,
    [C.MOVE_STRENGTH] = C.ITEM_HM04, [C.MOVE_ROCK_SMASH] = C.ITEM_HM06,
    [C.MOVE_WATERFALL] = C.ITEM_HM07, [C.MOVE_ROCK_CLIMB] = C.ITEM_HM08,
}
local menuMoves = { C.MOVE_DIG, C.MOVE_FLASH, C.MOVE_FLY, C.MOVE_DEFOG, C.MOVE_TELEPORT }
local skippedMessages = {}
for _, id in ipairs {
    C.FieldMoves_Text_PokemonUsedCut, C.FieldMoves_Text_PokemonUsedRockSmash,
    C.FieldMoves_Text_PokemonUsedStrength, C.FieldMoves_Text_PokemonUsedSurf,
    C.FieldMoves_Text_PokemonUsedDefog, C.FieldMoves_Text_PokemonUsedRockClimb,
    C.FieldMoves_Text_PokemonUsedWaterfall, C.FieldMoves_Text_PokemonUsedFlash,
    C.FieldMoves_Text_StrengthMadePossibleToMove,
    C.FieldMoves_Text_PokemonStrengthMadePossibleToMove,
    C.FieldMoves_Text_WouldYouLikeToUseCut, C.FieldMoves_Text_WouldYouLikeToUseRockSmash,
    C.FieldMoves_Text_WouldYouLikeToUseStrength, C.FieldMoves_Text_WouldYouLikeToUseRockClimb,
    C.FieldMoves_Text_WouldYouLikeToUseSurf, C.FieldMoves_Text_WouldYouLikeToUseDefog_Unused,
    C.FieldMoves_Text_WouldYouLikeToUseWaterfall,
} do skippedMessages[id] = true end

local function bagContains(bag, move, heap)
    if move == C.MOVE_TELEPORT then return true end
    return bag ~= nil and moves[move] ~= nil
        and F.Bag_GetItemQuantity(bag, moves[move], heap or C.HEAP_ID_FIELD1) > 0
end
local function compatible(mon, move)
    if move == C.MOVE_TELEPORT then
        local learnset = game.alloc(512)
        local count = F.Pokemon_LoadLevelUpMoveIdsOf(
            F.Pokemon_GetValue(mon, C.MON_DATA_SPECIES, nil),
            F.Pokemon_GetValue(mon, C.MON_DATA_FORM, nil), learnset)
        for i = 0, count - 1 do
            if game.read(learnset, "u16", i * 2) == move then return true end
        end
        return false
    end
    return moves[move] ~= nil
        and F.Pokemon_CanLearnTM(mon, F.Item_TMHMNumber(moves[move])) ~= 0
end
local function autoSlot(party, move)
    if party == nil or (not moves[move] and move ~= C.MOVE_TELEPORT) then return C.PARTY_SLOT_NONE end
    local count = F.Party_GetCurrentCount(party)
    if count == 0 then return C.PARTY_SLOT_NONE end
    if ignoreParty and move ~= C.MOVE_DIG and move ~= C.MOVE_FLASH and move ~= C.MOVE_TELEPORT then
        return 0
    end
    for slot = 0, count - 1 do
        local mon = F.Party_GetPokemonBySlotIndex(party, slot)
        if mon and F.Pokemon_GetValue(mon, C.MON_DATA_IS_EGG, nil) == 0
            and compatible(mon, move) then return slot end
    end
    return C.PARTY_SLOT_NONE
end
local function fieldMoveScript(ctx)
    local loader = get(ctx, T.ScriptContext, "loader")
    return loader ~= nil and get(loader, T.MessageLoader, "bankID") == C.TEXT_BANK_FIELD_MOVES
end
local function noMessage(ctx)
    return fieldMoveScript(ctx) and game.read(F.FieldSystem_GetScriptMemberPtr(
        get(ctx, T.ScriptContext, "fieldSystem"), C.SCRIPT_MANAGER_IS_MSG_BOX_OPEN), "u8") == 0
end

local function drawContextMenu(original, app, entries, count)
    original(app, entries, count)
    local partyMenu = get(app, T.PartyMenuApplication, "partyMenu")
    if get(partyMenu, T.PartyMenu, "mode") ~= C.PARTY_MENU_MODE_FIELD or not saveData
        or get(partyMenu, T.PartyMenu, "party") ~= F.SaveData_GetParty(saveData)
        or F.FieldSystem_IsInBattleTowerSalon(get(partyMenu, T.PartyMenu, "fieldSystem")) ~= 0 then return end
    local mon = F.Party_GetPokemonBySlotIndex(get(partyMenu, T.PartyMenu, "party"),
        get(app, T.PartyMenuApplication, "currPartySlot"))
    if F.Pokemon_GetValue(mon, C.MON_DATA_IS_EGG, nil) ~= 0
        or F.Pokemon_GetValue(mon, C.MON_DATA_CHECKSUM_FAILED, nil) ~= 0 then return end
    local extra = {}
    for _, move in ipairs(menuMoves) do
        local action, present = F.GetFieldMoveIndex(move), false
        for j = 0, count - 1 do
            if game.read(entries, "u8", j) == action then present = true end
        end
        if not present and bagContains(F.SaveData_GetBag(saveData), move, C.HEAP_ID_PARTY_MENU)
            and compatible(mon, move) then extra[#extra + 1] = move end
    end
    if #extra == 0 then return end
    local A = T.PartyMenuApplication
    local window = at(app, A, "menuWindows")
    local baseTile = get(window, T.Window, "tileFlags") & 0x7fff
    F.Menu_Free(get(app, A, "contextMenu"), nil)
    F.StringList_Free(get(app, A, "contextMenuChoices"))
    F.Window_EraseStandardFrame(window, 1)
    F.Window_ClearAndCopyToVRAM(window)
    F.Window_Remove(window)
    local total = count + #extra
    local columns = total > 11 and 2 or 1
    local rows = (total + columns - 1) // columns
    local slots = rows * columns
    F.Window_Add(get(app, A, "bgConfig"), window, 0, columns == 2 and 1 or 19,
        columns == 2 and 1 or 23 - rows * C.MAX_LETTER_HEIGHT_TILES,
        columns == 2 and 30 or 12, rows * C.MAX_LETTER_HEIGHT_TILES, 0, baseTile)
    local choices = F.StringList_New(slots, C.HEAP_ID_PARTY_MENU)
    set(app, A, "contextMenuChoices", choices)
    local fieldMove = 0
    for i = 0, count - 1 do
        if i == count - 1 then
            local format = F.MessageLoader_GetNewString(get(app, A, "messageLoader"), C.PartyMenu_Text_FieldMove0)
            local name = F.String_Init(128, C.HEAP_ID_PARTY_MENU)
            for _, move in ipairs(extra) do
                F.StringTemplate_SetMoveName(get(app, A, "template"), 0, move)
                F.StringTemplate_Format(get(app, A, "template"), name, format)
                F.StringList_AddFromString(choices, name, F.PartyMenu_GetAction(F.GetFieldMoveIndex(move)))
            end
            F.String_Free(name)
            F.String_Free(format)
        end
        local entry = game.read(entries, "u8", i)
        local index = entry
        if entry >= C.PARTY_MENU_STR_MOVE0 then
            index = C.PARTY_MENU_STR_MOVE0 + fieldMove
            fieldMove = fieldMove + 1
        end
        F.StringList_AddFromString(choices, game.read(at(app, A, "menuStrings"), "ptr", index * pointerSize),
            F.PartyMenu_GetAction(entry))
    end
    if slots > total then
        local entry = game.read(entries, "u8", count - 1)
        F.StringList_AddFromString(choices, game.read(at(app, A, "menuStrings"), "ptr", entry * pointerSize),
            F.PartyMenu_GetAction(entry))
    end
    local menu = game.alloc(T.MenuTemplate.size)
    set(menu, T.MenuTemplate, "choices", choices)
    set(menu, T.MenuTemplate, "window", window)
    set(menu, T.MenuTemplate, "fontID", C.FONT_SYSTEM)
    set(menu, T.MenuTemplate, "xSize", columns)
    set(menu, T.MenuTemplate, "ySize", rows)
    set(menu, T.MenuTemplate, "flags", 1 << 6) -- loopAround bitfield
    F.Window_DrawStandardFrame(window, 1, 1, 14)
    set(app, A, "contextMenu", F.Menu_NewAndCopyToVRAM(menu, 8, 0, 0, C.HEAP_ID_PARTY_MENU, C.PAD_BUTTON_B))
end

local function onLoad()
    local function bind(symbol, result, ...)
        F[symbol] = game.bind(symbol, result, {...})
    end
    bind("FieldSystem_GetScriptMemberPtr", "ptr", "ptr", "u32")
    bind("ScriptContext_ReadHalfWord", "u16", "ptr")
    bind("FieldSystem_GetVarPointer", "ptr", "ptr", "u16")
    bind("FieldSystem_TryGetVar", "u16", "ptr", "u16")
    bind("FieldSystem_GetSaveData", "ptr", "ptr")
    bind("SaveData_GetParty", "ptr", "ptr")
    bind("SaveData_GetBag", "ptr", "ptr")
    bind("Bag_GetItemQuantity", "u16", "ptr", "u16", "i32")
    bind("Party_GetCurrentCount", "i32", "ptr")
    bind("Party_GetPokemonBySlotIndex", "ptr", "ptr", "i32")
    bind("Pokemon_GetValue", "u32", "ptr", "i32", "ptr")
    bind("Item_TMHMNumber", "u8", "u16")
    bind("Pokemon_CanLearnTM", "i32", "ptr", "u8")
    bind("Pokemon_LoadLevelUpMoveIdsOf", "i32", "i32", "i32", "ptr")
    bind("FieldSystem_IsInBattleTowerSalon", "i32", "ptr")
    bind("GetFieldMoveIndex", "u8", "u16")
    bind("Menu_Free", "void", "ptr", "ptr")
    bind("StringList_Free", "void", "ptr")
    bind("Window_EraseStandardFrame", "void", "ptr", "u8")
    bind("Window_ClearAndCopyToVRAM", "void", "ptr")
    bind("Window_Remove", "void", "ptr")
    bind("Window_Add", "void", "ptr", "ptr", "u8", "u8", "u8", "u8", "u8", "u8", "u16")
    bind("StringList_New", "ptr", "u32", "u32")
    bind("StringList_AddFromString", "void", "ptr", "ptr", "u64")
    bind("PartyMenu_GetAction", "u64", "u8")
    bind("MessageLoader_GetNewString", "ptr", "ptr", "u32")
    bind("String_Init", "ptr", "u32", "u32")
    bind("String_Free", "void", "ptr")
    bind("StringTemplate_SetMoveName", "void", "ptr", "u32", "i32")
    bind("StringTemplate_Format", "void", "ptr", "ptr", "ptr")
    bind("Window_DrawStandardFrame", "void", "ptr", "u8", "u16", "u8")
    bind("Menu_NewAndCopyToVRAM", "ptr", "ptr", "u8", "u8", "u8", "u8", "u32")

    -- These are the real hooks. No AutoHM registration exists in the engine.
    game.hook("HMCutIn_StartTask", "ptr", {"ptr", "i32", "ptr", "i32"},
        function(original, fieldSystem, isFly, mon, gender)
            if not animations then return skippedCutIn end
            return original(fieldSystem, isFly, mon, gender)
        end)
    game.hook("HMCutIn_IsFinished", "i32", {"ptr"}, function(original, task)
        if task == skippedCutIn then return 1 end
        return original(task)
    end)
    game.hook("HMCutIn_EndTask", "void", {"ptr"}, function(original, task)
        if task ~= skippedCutIn then original(task) end
    end)
    game.hook("Party_HasMonWithMove", "i32", {"ptr", "u16"}, function(original, party, move)
        local slot = original(party, move)
        if slot ~= C.PARTY_SLOT_NONE or not saveData or party ~= F.SaveData_GetParty(saveData)
            or not bagContains(F.SaveData_GetBag(saveData), move) then return slot end
        return autoSlot(party, move)
    end)
    game.hook("ScrCmd_FindPartySlotWithMove", "i32", {"ptr"}, function(original, ctx)
        local copy = game.alloc(T.ScriptContext.size)
        game.copy(copy, ctx, T.ScriptContext.size)
        local destinationVar = F.ScriptContext_ReadHalfWord(copy)
        local moveVar = F.ScriptContext_ReadHalfWord(copy)
        local field = get(copy, T.ScriptContext, "fieldSystem")
        local destination = F.FieldSystem_GetVarPointer(field, destinationVar)
        local move = F.FieldSystem_TryGetVar(field, moveVar)
        local paused = original(ctx)
        if destination and game.read(destination, "u16") == C.MAX_PARTY_SIZE then
            local save = F.FieldSystem_GetSaveData(get(ctx, T.ScriptContext, "fieldSystem"))
            local slot = autoSlot(F.SaveData_GetParty(save), move)
            if slot ~= C.PARTY_SLOT_NONE and bagContains(F.SaveData_GetBag(save), move) then
                game.write(destination, "u16", slot)
            end
        end
        return paused
    end)
    game.hook("PartyMenu_DrawContextMenu", "void", {"ptr", "ptr", "u8"}, drawContextMenu)
    game.hook("ScrCmd_Message", "i32", {"ptr"}, function(original, ctx)
        local script = get(ctx, T.ScriptContext, "scriptPtr")
        if not animations and fieldMoveScript(ctx) and skippedMessages[game.read(script, "u8")] then
            set(ctx, T.ScriptContext, "scriptPtr", game.address(script, 1))
            return 0
        end
        return original(ctx)
    end)
    game.hook("ScrCmd_ShowYesNoMenu", "i32", {"ptr"}, function(original, ctx)
        if not animations and fieldMoveScript(ctx) then
            local destination = F.ScriptContext_ReadHalfWord(ctx)
            game.write(F.FieldSystem_GetVarPointer(get(ctx, T.ScriptContext, "fieldSystem"), destination),
                "u16", C.MENU_YES)
            return 0
        end
        return original(ctx)
    end)
    for _, symbol in ipairs {"ScrCmd_WaitButton", "ScrCmd_CloseMessage"} do
        game.hook(symbol, "i32", {"ptr"}, function(original, ctx)
            if noMessage(ctx) then return 0 end
            return original(ctx)
        end)
    end
    game.option("enabled", "IGNORE PARTY", {"DISABLED", "ENABLED"}, 0,
        function(selected) ignoreParty = selected == 1 end)
    game.option("animations", "ANIMATIONS", {"DISABLED", "ENABLED"}, 0,
        function(selected) animations = selected == 1 end)
end

return {
    id = "autohm", name = "Auto HM", version = "2.0.0",
    on_load = onLoad,
    on_event = function(event, data)
        if event == C.MOD_EVENT_SAVE_LOADED and data
            and get(data, T.SaveEvent, "structSize") == T.SaveEvent.size then
            saveData = get(data, T.SaveEvent, "saveData")
        end
    end,
    -- Exposed only for the standalone ABI/behavior regression tests.
    _abi = T, _constants = C,
}
