local speed, remainder, consumption = 0, 0, 0
local consumed, tracked = {}, nil
local function ripen(patch, growth)
    local id, stage = game.read(patch, 'u8'), game.read(patch, 'u8', 1)
    if id == 0 or id > 64 or stage < 1 or stage >= 5 or game.read(patch, 'u8', 13) == 0 then return end
    local data = game.address(growth, (id - 1) * 3)
    local minutes, mulch = game.read(data, 'u8') * 60, game.read(patch, 'u8', 12)
    if mulch == 1 then minutes = minutes * 3 // 4 elseif mulch == 2 then minutes = minutes + minutes // 2 end
    game.write(patch, 'u16', math.max(2, game.read(data, 'u8', 2) * game.read(patch, 'u8', 11)), 8)
    game.write(patch, 'u8', 5, 1)
    game.write(patch, 'u16', minutes * (mulch == 3 and 6 or 4), 2)
end
return {
    id = 'better_berries', name = 'Better Berries', version = '0.0.3',
    on_load = function()
        local patches = game.bind('MiscSaveBlock_GetBerryPatches', 'ptr', {'ptr'})
        local objectData = game.bind('MapObject_GetDataAt', 'i32', {'ptr','i32'})
        local appData = game.bind('ApplicationManager_Data', 'ptr', {'ptr'})
        local battleType = game.bind('BattleSystem_GetBattleType', 'u32', {'ptr'})
        local status = game.bind('BattleSystem_GetBattleStatusMask', 'u32', {'ptr'})
        local count = game.bind('BattleSystem_GetMaxBattlers', 'i32', {'ptr'})
        local party = game.bind('BattleSystem_GetParty', 'ptr', {'ptr','i32'})
        local pokemon = game.bind('BattleSystem_GetPartyPokemon', 'ptr', {'ptr','i32','i32'})
        local bag = game.bind('BattleSystem_GetBag', 'ptr', {'ptr'})
        local get = game.bind('Pokemon_GetValue', 'u32', {'ptr','i32','ptr'})
        local set = game.bind('Pokemon_SetValue', 'void', {'ptr','i32','ptr'})
        local remove = game.bind('Bag_TryRemoveItem', 'i32', {'ptr','u16','u16','i32'})
        game.hook('BerryPatches_ElapseMinutes', 'void', {'ptr','ptr','i32'}, function(original, list, growth, minutes)
            if speed > 0 and speed < 4 and minutes > 0 then
                local scaled, divisor = minutes * 4 + remainder, 4 - speed
                remainder, minutes = scaled % divisor, math.min(0x7fffffff, scaled // divisor)
            end
            original(list, growth, minutes)
            if speed == 4 then for i = 0, 127 do ripen(game.address(list, i * 14), growth) end end
        end)
        game.hook('BerryPatches_PlantInPatch', 'void', {'ptr','i32','ptr','i32'}, function(original, list, id, growth, berry)
            original(list, id, growth, berry)
            if speed == 4 then ripen(game.address(list, id * 14), growth) end
        end)
        game.hook('BerryPatches_GetGrowthStage', 'i32', {'ptr','ptr'}, function(original, field, object)
            local sub = game.read(field, 'ptr', 8)
            local manager = sub and game.read(sub, 'ptr', 48)
            if speed == 4 and manager then
                local id = objectData(object, 0)
                if id >= 0 and id < 128 then
                    ripen(game.address(patches(game.read(field, 'ptr', 24)), id * 14), game.read(manager, 'ptr', 8))
                end
            end
            return original(field, object)
        end)
        game.hook('BtlCmd_RemoveItem', 'i32', {'ptr','ptr'}, function(original, battle, context)
            if consumption == 0 or (battleType(battle) & 0x484) ~= 0 or (status(battle) & 16) ~= 0 then
                return original(battle, context)
            end
            if tracked ~= battle then consumed, tracked = {}, battle end
            local items, mons = {}, {}
            local player = party(battle, 0)
            for i = 0, count(battle) - 1 do
                local item = game.read(context, 'u16', 11608 + i * 192 + 120)
                if item >= 149 and item <= 212 and party(battle, i) == player then
                    items[i], mons[i] = item, pokemon(battle, i, game.read(context, 'u8', 8628 + i))
                end
            end
            local result = original(battle, context)
            for i, mon in pairs(mons) do
                if game.read(context, 'u16', 11608 + i * 192 + 120) == 0 then consumed[mon] = items[i] end
            end
            return result
        end)
        game.hook('BattleMain_CopyBattleSysToDTOAndFree', 'void', {'ptr'}, function(original, app)
            local battle = appData(app)
            if tracked == battle and consumption ~= 0 then
                local value = game.alloc(2)
                for mon, item in pairs(consumed) do
                    if get(mon, 6, nil) == 0 and (consumption == 2 or remove(bag(battle), item, 1, 5) ~= 0) then
                        game.write(value, 'u16', item)
                        set(mon, 6, value)
                    end
                end
            end
            consumed, tracked = {}, nil
            original(app)
        end)
        game.option('growth_speed', 'GROWTH SPEED', {'0%','25%','50%','75%','100%'}, 3,
            function(value) speed, remainder = value, 0 end)
        game.option('consumption', 'CONSUMPTION', {'Default','Refill','Inf'}, 2, function(value) consumption = value end)
    end,
    on_event = function(event) if event == 8 then remainder = 0 end end,
}
