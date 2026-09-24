local die, limit = false, false
local limited, allowed, full, slots = nil, 0, game.alloc(1424), {}
local dead, battle, F = {}, nil, {}
local function real(kind, status) return (kind & 0x484) == 0 and (status & 16) == 0 end
local function mon(party, slot) return game.address(party, 8 + slot * 236) end
local function restore(dto)
    if dto ~= limited then return end
    local party = game.read(dto,'ptr',8)
    for i = 0, allowed - 1 do game.copy(mon(full,slots[i+1]),mon(party,i),236) end
    game.copy(party,full,1424)
    limited, allowed = nil, 0
end
local function begin(dto)
    local kind = game.read(dto,'u32')
    if not limit or (kind & 1) == 0 or not real(kind,game.read(dto,'u32',500)) or (kind & 0x80000000) ~= 0 then return true end
    local count = F.count(game.read(dto,'ptr',16))
    if (kind & 24) ~= 0 then count = count + F.count(game.read(dto,'ptr',32)) end
    local party = game.read(dto,'ptr',8)
    local total = F.count(party)
    if count <= 0 or count >= total then return true end
    game.copy(full,party,1424)
    limited, allowed, slots = dto, count, {}
    local included = {}
    for i = 0, total - 1 do
        local pokemon = F.mon(party,i)
        if #slots < count and F.get(pokemon,5,nil) ~= 0 and F.get(pokemon,76,nil) == 0 and F.get(pokemon,163,nil) ~= 0 then
            slots[#slots+1], included[i] = i, true
        end
    end
    for i = 0, total - 1 do if not included[i] then slots[#slots+1] = i end end
    for i = 0, total - 1 do game.copy(mon(party,i),mon(full,slots[i+1]),236) end
    for i = total - 1, count, -1 do F.remove(party,i) end
    for i = 0, count - 1 do
        local pokemon = F.mon(party,i)
        if F.get(pokemon,5,nil) ~= 0 and F.get(pokemon,76,nil) == 0 and F.get(pokemon,163,nil) ~= 0 then return true end
    end
    game.write(dto,'i32',2,40)
    restore(dto)
    return false
end
local function isDead(pokemon)
    if F.get(pokemon,76,nil) ~= 0 then return false end
    if F.get(pokemon,163,nil) == 0 then return true end
    for _, id in ipairs(dead) do
        if F.get(pokemon,0,nil) == id[1] and F.get(pokemon,7,nil) == id[2] then return true end
    end
    return false
end
local function lastBox(app)
    return die and game.read(game.read(app,'ptr'),'u32') == 17 and game.read(app,'u8',24) == 0
end
local function held(app) return (game.read(app,'u8',31) & 6) ~= 0 end
return {
    id = 'nuzzlocke', name = 'nuzzlocke', version = '0.0.2',
    on_load = function()
        F.get = game.bind('Pokemon_GetValue','u32',{'ptr','i32','ptr'})
        F.set = game.bind('Pokemon_SetValue','void',{'ptr','i32','ptr'})
        F.init = game.bind('Pokemon_InitWith','void',{'ptr','i32','i32','i32','i32','u32','i32','u32'})
        F.move = game.bind('Pokemon_SetMoveSlot','void',{'ptr','u16','u8'})
        F.add = game.bind('Party_AddPokemon','i32',{'ptr','ptr'})
        F.box = game.bind('Pokemon_GetBoxPokemon','ptr',{'ptr'})
        F.count = game.bind('Party_GetCurrentCount','i32',{'ptr'})
        F.mon = game.bind('Party_GetPokemonBySlotIndex','ptr',{'ptr','i32'})
        F.remove = game.bind('Party_RemovePokemonBySlotIndex','i32',{'ptr','i32'})
        F.party = game.bind('SaveData_GetParty','ptr',{'ptr'})
        F.boxes = game.bind('SaveData_GetPCBoxes','ptr',{'ptr'})
        F.store = game.bind('PCBoxes_TryStoreBoxMonInBox','i32',{'ptr','u32','ptr'})
        F.kind = game.bind('BattleSystem_GetBattleType','u32',{'ptr'})
        F.status = game.bind('BattleSystem_GetBattleStatusMask','u32',{'ptr'})
        F.battleParty = game.bind('BattleSystem_GetParty','ptr',{'ptr','i32'})
        F.battleMon = game.bind('BattleSystem_GetPartyPokemon','ptr',{'ptr','i32','i32'})
        F.args = game.bind('ApplicationManager_Args','ptr',{'ptr'})
        local clear = game.bind('BoxAppMan_ClearBoxApplicationAction','void',{'ptr'})
        local blocked = {}
        for _, name in ipairs({'BoxAppMan_PickUpMonAction','BoxAppMan_WithdrawMonAction','BoxAppMan_ShiftMonAction','BoxAppMan_MultiSelectAction'}) do
            blocked[game.symbol(name)] = true
        end
        game.hook('Battle_Main','i32',{'ptr','ptr'},function(original,app,state)
            local dto = F.args(app)
            if game.read(state,'i32') == 0 and not begin(dto) then return 1 end
            local finished = original(app,state)
            if finished ~= 0 then restore(dto) end
            return finished
        end)
        game.hook('BattleMain_CopyBattleSysToDTOAndFree','void',{'ptr'},function(original,app)
            local dto = F.args(app)
            original(app); restore(dto)
        end)
        game.hook('BtlCmd_PlayFaintAnimation','i32',{'ptr','ptr'},function(original,current,ctx)
            if die and real(F.kind(current),F.status(current)) then
                if current ~= battle then dead,battle = {},current end
                local battler = game.read(ctx,'i32',116)
                if F.battleParty(current,battler) == F.battleParty(current,0) then
                    local pokemon = F.battleMon(current,battler,game.read(ctx,'u8',8628+battler))
                    local personality, ot = F.get(pokemon,0,nil),F.get(pokemon,7,nil)
                    local found = false
                    for _, id in ipairs(dead) do if id[1] == personality and id[2] == ot then found = true end end
                    if not found and #dead < 6 then dead[#dead+1] = {personality,ot} end
                end
            end
            return original(current,ctx)
        end)
        game.hook('FieldBattleDTO_UpdateFieldSystem','void',{'ptr','ptr'},function(original,dto,field)
            original(dto,field)
            if die and real(game.read(dto,'u32'),game.read(dto,'u32',500)) then
                local save = game.read(field,'ptr',24)
                local party,boxes = F.party(save),F.boxes(save)
                local boxed = game.alloc(136)
                for slot = F.count(party)-1,0,-1 do
                    local pokemon = F.mon(party,slot)
                    if isDead(pokemon) then
                        game.copy(boxed,F.box(pokemon),136)
                        local stored = false
                        for box = 17,0,-1 do
                            if F.store(boxes,box,boxed) ~= 0 then F.remove(party,slot);stored = true;break end
                        end
                        if not stored then game.log('PC completo: no se ha eliminado el Pokemon del equipo.') end
                    end
                end
                if F.count(party) == 0 then
                    local pokemon,value = game.alloc(236),game.alloc(1)
                    for _,form in ipairs({11,14,18,4,17,27}) do
                        F.init(pokemon,201,1,0,0,0,0,0)
                        game.write(value,'u8',form);F.set(pokemon,112,value)
                        for slot = 0,3 do F.move(pokemon,slot == 0 and 120 or 0,slot) end
                        F.add(party,pokemon)
                    end
                end
            end
            dead,battle = {},nil
        end)
        game.hook('BoxMenu_FillTopLevelMenuItems','void',{'ptr'},function(original,app)
            original(app)
            if not lastBox(app) or game.read(app,'u8',30) == 0 then return end
            game.write(app,'u8',0,200)
            local count = 0
            if not held(app) then game.write(app,'i32',42,168);count=1 end
            game.write(app,'i32',43,168+count*4);game.write(app,'u8',count+1,201)
        end)
        game.hook('BoxMenu_FillItemsMenu','void',{'ptr'},function(original,app)
            original(app)
            if lastBox(app) then game.write(app,'u8',0,200);game.write(app,'u8',1,201);game.write(app,'i32',51,168) end
        end)
        game.hook('BoxAppMan_CheckReleaseMonValid','i32',{'ptr','ptr'},function(original,manager,message)
            if lastBox(manager) and not held(manager) then return 1 end
            return original(manager,message)
        end)
        game.hook('BoxAppMan_CheckShouldMonReturn','void',{'ptr'},function(original,manager)
            if lastBox(manager) and not held(manager) then
                game.write(manager,'u8',1,488);game.write(manager,'u8',1,489)
            else original(manager) end
        end)
        game.hook('BoxAppMan_RegisterBoxApplicationAction','void',{'ptr','ptr'},function(original,manager,action)
            if lastBox(manager) and blocked[action] then clear(manager) else original(manager,action) end
        end)
        game.option('Enabled','PKMN DIE',{'TRUE','FALSE'},1,function(value)
            die = value == 0
            if not die then dead,battle = {},nil end
        end)
        game.option('PokemonLimit','PKMN LIMIT',{'TRUE','FALSE'},1,function(value) limit = value == 0 end)
    end,
    on_unload = function() if limited then restore(limited) end end,
}
