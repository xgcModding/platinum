local unseen, uncaught, active, capture = false, false, false, false
local pending, owner, duel = 0, nil, nil
local displayed, F = {}, {}
local result, reserved, balls = game.alloc(4), game.alloc(1424), game.alloc(60)
local function mon(party, slot) return game.address(party,8+slot*236) end
return {
    id = 'pokedex_battle', name = 'Pokédex Battle', version = '0.0.7',
    on_load = function()
        local system = game.symbol('gSystem')
        F.env = game.bind('FieldTask_GetEnv','ptr',{'ptr'})
        F.state = game.bind('FieldTask_GetState','ptr',{'ptr'})
        F.encounter = game.bind('Encounter_NewVsSpeciesAtLevel','void',{'ptr','u16','u8','ptr','i32'})
        F.won = game.bind('CheckPlayerWonBattle','i32',{'u32'})
        F.blackout = game.bind('FieldTask_StartBlackOutFromBattle','void',{'ptr'})
        F.seen = game.bind('Pokedex_HasSeenSpecies','i32',{'ptr','u16'})
        F.caught = game.bind('Pokedex_HasCaughtSpecies','i32',{'ptr','u16'})
        F.string = game.bind('MessageLoader_GetString','void',{'ptr','u32','ptr'})
        F.display = game.bind('BattleBagText_DisplayMessage','void',{'ptr'})
        F.field = game.bind('FieldTask_GetFieldSystem','ptr',{'ptr'})
        F.party = game.bind('SaveData_GetParty','ptr',{'ptr'})
        F.get = game.bind('Pokemon_GetValue','u32',{'ptr','i32','ptr'})
        F.empty = game.bind('PCBoxes_FirstEmptyBox','u32',{'ptr'})
        local taskCallback = game.callback('i32',{'ptr'},function(task)
            local state = F.state(task)
            if game.read(state,'i32') == 0 then
                local species = pending
                game.write(state,'i32',1)
                pending = 0;game.write(result,'i32',0)
                local party = F.party(game.read(F.field(task),'ptr',24))
                local first = mon(party,0)
                if game.read(party,'i32',4) == 0 or F.get(first,76,nil) ~= 0 or F.get(first,163,nil) == 0 then return 1 end
                active = true
                F.encounter(task,species,F.get(first,161,nil),result,0)
                return 0
            end
            active = false
            if game.read(state,'i32') == 1 and F.won(game.read(result,'u32')) == 0 then
                game.write(state,'i32',2);F.blackout(task);return 0
            end
            return 1
        end)
        game.hook('PopulateDisplayPokedex_Blanks','void',{'ptr','ptr','i32'},function(original,sorted,dex,length)
            original(sorted,dex,length)
            owner = sorted
            for i = 0,492 do displayed[i] = i < length and game.read(dex,'u16',i*2) or 0 end
            if unseen then
                for i = game.read(sorted,'i32',5928),length-1 do game.write(sorted,'i32',494,3952+i*4) end
                game.write(sorted,'i32',length,5928)
            end
        end)
        game.hook('ov21_021D5F7C','void',{'ptr','ptr','i32'},function(original,data,graphics,state)
            local keys = game.read(system,'u32',116)
            original(data,graphics,state)
            if (keys & 0x800) == 0 or (keys & 0x407) ~= 0 then return end
            local sort = game.read(data,'ptr',8)
            local sorted = game.address(sort,8)
            local index = game.read(sorted,'i32',5924)
            if index < 0 or index >= game.read(sorted,'i32',5928) or index >= 493 then return end
            local status = game.read(sorted,'i32',3952+index*4)
            local species = 0
            if status >= 0 and status < game.read(sorted,'i32',3948) then species = game.read(sorted,'i32',status*8)
            elseif unseen and sorted == owner then species = displayed[index] end
            local dex = game.read(sort,'ptr')
            if species > 0 and species <= 493 and (unseen or F.seen(dex,species) ~= 0) then
                pending,capture = species,uncaught or F.caught(dex,species) ~= 0
                game.write(data,'i32',1,48)
            end
        end)
        game.hook('StartMenu_ExitPokedex','i32',{'ptr'},function(original,task)
            local menu = F.env(task)
            local finished = original(task)
            if pending ~= 0 then
                game.write(menu,'ptr',nil,0x370);game.write(menu,'ptr',taskCallback,0x320);game.write(menu,'i32',10,0x74)
            end
            return finished
        end)
        game.hook('TryUseItem','u8',{'ptr'},function(original,bag)
            if active and not capture and game.read(bag,'u8',4509) == 2 then
                F.string(game.read(bag,'ptr',32),34,game.read(bag,'ptr',48))
                F.display(bag);game.write(bag,'u8',8,4507);return 9
            end
            return original(bag)
        end)
        -- BattleItemUse is four integer bytes, passed in one integer ABI register.
        game.hook('BattleController_EmitSelectedBagItem','void',{'ptr','i32','u32'},function(original,battle,battler,item)
            if active and not capture and ((item >> 16) & 255) == 2 then item = 255 end
            original(battle,battler,item)
        end)
        game.hook('StartEncounter','void',{'ptr','ptr','i32','i32','ptr'},function(original,task,dto,effect,music,resultPointer)
            if active and resultPointer == game.address(result) then
                duel = dto
                local party = game.read(dto,'ptr',8)
                game.copy(reserved,party,1424)
                local count = game.read(reserved,'i32',4)
                if count == 6 and F.empty(game.read(dto,'ptr',328)) == 18 then capture = false end
                game.write(party,'i32',1,4);game.write(party,'i32',7-count)
                game.copy(mon(party,1),game.alloc(236*5),236*5)
                if not capture then
                    local pocket = game.address(game.read(dto,'ptr',304),1724)
                    game.copy(balls,pocket,60);game.copy(pocket,game.alloc(60),60)
                end
            end
            original(task,dto,effect,music,resultPointer)
        end)
        game.hook('FieldBattleDTO_UpdateFieldSystem','void',{'ptr','ptr'},function(original,dto,field)
            if dto == duel then
                local party = game.read(dto,'ptr',8)
                game.copy(mon(reserved,0),mon(party,0),236)
                local count = game.read(reserved,'i32',4)
                for i = 1,game.read(party,'i32',4)-1 do
                    if count >= 6 then break end
                    game.copy(mon(reserved,count),mon(party,i),236);count=count+1
                end
                game.write(reserved,'i32',count,4);game.copy(party,reserved,1424)
                if not capture then game.copy(game.address(game.read(dto,'ptr',304),1724),balls,60) end
                duel = nil
            end
            original(dto,field)
        end)
        game.option('allow_unseen','BATTLE UNSEEN',{'OFF','ON'},0,function(value) unseen = value == 1 end)
        game.option('allow_uncaught','CATCH UNCAUGHT',{'OFF','ON'},0,function(value) uncaught = value == 1 end)
    end,
}
