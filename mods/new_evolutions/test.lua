-- Run from repository root. Uses real imported evolution records; no save I/O.
local function enum(path)
    local values, i = {}, 0
    for name in io.lines(path) do values[name] = i; i = i + 1 end
    return values
end
local species = enum('generated/species.txt')
local items = enum('generated/items.txt')
local methods = enum('generated/evolution_methods.txt')
local file = assert(io.open('build_linux/assets/poketool/personal/evo.narc', 'rb'))
local narc = file:read('a'); file:close()
local entries, payload = {}, nil
local pos = 17
while pos <= #narc do
    local tag, size = string.unpack('<c4I4', narc, pos)
    if tag == 'BTAF' then
        local count = string.unpack('<I2', narc, pos + 8)
        for i=0,count-1 do
            local first,last = string.unpack('<I4I4', narc, pos+12+i*8)
            entries[i] = {first,last}
        end
    elseif tag == 'GMIF' then payload = narc:sub(pos+8,pos+size-1) end
    pos = pos + size
end
local hook, toggle
local writes = 0
game = {
    hook = function(name, result, args, callback)
        assert(name == 'NARC_ReadWholeMemberByIndexPair' and result == 'void')
        assert(table.concat(args, ',') == 'ptr,i32,i32'); hook = callback
    end,
    option = function(_, _, _, default, callback) assert(default == 0); toggle = callback end,
    write = function(data, kind, value, offset)
        assert(kind == 'u16' and offset >= 0 and offset <= 40 and offset%2 == 0)
        data[offset] = value; writes = writes + 1
    end,
}
local mod = dofile(arg[1] or 'mods/new_evolutions/new_evolutions.lua')
assert(mod.name == 'New evolutions'); mod.on_load()
local function load(id, archive, enabled)
    toggle(enabled and 0 or 1)
    local result, original, calls = {}, {}, 0
    writes = 0
    hook(function(dest,a,s)
        assert(dest == result and a == archive and s == id); calls = calls + 1
        local range = assert(entries[id]); assert(range[2]-range[1] >= 42)
        for offset=0,40,2 do
            original[offset] = string.unpack('<I2',payload,range[1]+offset+1)
            dest[offset] = original[offset]
        end
    end,result,archive,id)
    assert(calls == 1)
    return result,original
end
local cases = {
    {'KADABRA','ALAKAZAM',32}, {'GRAVELER','GOLEM',42},
    {'ONIX','STEELIX','METAL_COAT'}, {'MACHOKE','MACHAMP',45},
    {'HAUNTER','GENGAR',42}, {'RHYDON','RHYPERIOR','PROTECTOR'},
    {'DUSCLOPS','DUSKNOIR','REAPER_CLOTH'}, {'PORYGON','PORYGON2','UPGRADE'},
    {'PORYGON2','PORYGON_Z','DUBIOUS_DISC'}, {'SCYTHER','SCIZOR','METAL_COAT'},
    {'ELECTABUZZ','ELECTIVIRE','ELECTIRIZER'}, {'MAGMAR','MAGMORTAR','MAGMARIZER'},
}
local changed = {}
for _,case in ipairs(cases) do
    local id,target = species['SPECIES_'..case[1]],species['SPECIES_'..case[2]]
    changed[id] = true
    local data,original = load(id,34,true)
    assert(original[4] == target, 'unexpected original target: '..case[1])
    assert(data[4] == target)
    if type(case[3]) == 'number' then
        assert(data[0] == methods.EVO_LEVEL and data[2] == case[3])
        for offset=6,40,2 do assert(data[offset] == 0) end
    else
        local item = assert(items['ITEM_'..case[3]])
        assert(data[0] == methods.EVO_LEVEL_WITH_HELD_ITEM_DAY and data[2] == item)
        assert(data[6] == methods.EVO_LEVEL_WITH_HELD_ITEM_NIGHT and data[8] == item and data[10] == target)
        for offset=12,40,2 do assert(data[offset] == 0) end
    end
    for _,mode in ipairs({{34,false},{33,true}}) do
        local untouched,baseline = load(id,mode[1],mode[2])
        assert(writes == 0)
        for k,v in pairs(baseline) do assert(untouched[k] == v) end
    end
end
local unchanged = 0
for id=1,493 do
    if not changed[id] then
        local data,original = load(id,34,true)
        assert(writes == 0)
        for k,v in pairs(original) do assert(data[k] == v) end
        unchanged = unchanged + 1
    end
end
print('PASS: 12 evolution rules, day/night item conditions, disabled/archive isolation, '..unchanged..' unaffected species')
