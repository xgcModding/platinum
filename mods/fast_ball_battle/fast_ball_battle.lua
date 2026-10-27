local F = {}
local BATTLE_TYPE_CATCH_TUTORIAL = 1 << 10
local function bindFunctions()
    F.GetSpriteSystem = game.bind('BattleSystem_GetSpriteSystem','ptr',{'ptr'})
    F.GetSpriteManager = game.bind('BattleSystem_GetSpriteManager','ptr',{'ptr'})
    F.HideCursor = game.bind('CursorRenderer_HideAllSprites','void',{'ptr'})
    F.GetHealthBox = game.bind('BattleSystem_GetHealthBox','ptr',{'ptr','i32'})
    F.GetBattleType = game.bind('BattleSystem_GetBattleType','u32',{'ptr'})
    F.GetBattleStatusMask = game.bind('BattleSystem_GetBattleStatusMask','u32',{'ptr'})
    F.GetBattleContext = game.bind('BattleSystem_GetBattleContext','ptr',{'ptr'})
    F.GetBag = game.bind('BattleSystem_GetBag','ptr',{'ptr'})
    F.GetParty = game.bind('BattleSystem_GetParty','ptr',{'ptr','i32'})
    F.GetPCBoxes = game.bind('BattleSystem_GetPCBoxes','ptr',{'ptr'})
    F.GetMaxBattlers = game.bind('BattleSystem_GetMaxBattlers','i32',{'ptr'})
    F.GetBattlerSide = game.bind('BattleSystem_GetBattlerSide','u8',{'ptr','i32'})
    F.GetBattlerOfType = game.bind('BattleSystem_GetBattlerOfType','i32',{'ptr','i32'})
    F.GetPartyCount = game.bind('Party_GetCurrentCount','i32',{'ptr'})
    F.GetPartyPokemon = game.bind('Party_GetPokemonBySlotIndex','ptr',{'ptr','i32'})
    F.GetFirstEmptyBox = game.bind('PCBoxes_FirstEmptyBox','u32',{'ptr'})
    F.CountAllBoxMons = game.bind('PCBoxes_CountAllBoxMons','u32',{'ptr'})
    F.GetBoxMonData = game.bind('PCBoxes_GetBoxMonData','u32',{'ptr','u32','u32','i32','ptr'})
    F.GetPokemonValue = game.bind('Pokemon_GetValue','u32',{'ptr','i32','ptr'})
    F.GetMapHeader = game.bind('BattleSystem_GetMapHeader','i32',{'ptr'})
    F.HasCaughtSpecies = game.bind('BattleSystem_HasCaughtSpecies','i32',{'ptr','u16'})
    F.GetVarsFlags = game.bind('SaveData_GetVarsFlags','ptr',{'ptr'})
    F.CheckFlag = game.bind('VarsFlags_CheckFlag','i32',{'ptr','u16'})
    F.SetFlag = game.bind('VarsFlags_SetFlag','void',{'ptr','u16'})
    F.HeapAlloc = game.bind('Heap_Alloc','ptr',{'i32','u32'})
    F.ResourceCollectionAdd = game.bind('ResourceCollection_Add','ptr',{'ptr','ptr','i32'})
    F.GetCharacterData = game.bind('NNS_G2dGetUnpackedCharacterData','i32',{'ptr','ptr'})
    F.GetPaletteData = game.bind('NNS_G2dGetUnpackedPaletteData','i32',{'ptr','ptr'})
    F.GetCellBank = game.bind('NNS_G2dGetUnpackedCellBank','i32',{'ptr','ptr'})
    F.GetAnimBank = game.bind('NNS_G2dGetUnpackedAnimBank','i32',{'ptr','ptr'})
    F.ReadItemResource = game.bind('NARC_AllocAndReadWholeMemberByIndexPair','ptr',{'i32','i32','i32'})
    F.RegisterLoadedResource = game.bind('RegisterLoadedResource','i32',{'ptr','ptr'})
    -- Imported item icons use 32K mapping; battle OBJ VRAM uses 64K.
    -- Resolve the image proxy with the active hardware mapping for both sprites.
    F.RequestCharTransfer = game.bind('SpriteTransfer_RequestCharAtEndWithHardwareMappingType','i32',{'ptr'})
    F.RequestPaletteTransfer = game.bind('SpriteTransfer_RequestPlttFreeSpace','i32',{'ptr'})
    F.VramTransferRequest = game.bind('VramTransfer_Request','i32',{'i32','u32','ptr','u32'})
    F.NewSprite = game.bind('SpriteSystem_NewSprite','ptr',{'ptr','ptr','ptr'})
    F.SetDrawFlag = game.bind('ManagedSprite_SetDrawFlag','void',{'ptr','i32'})
    F.GetDrawFlag = game.bind('ManagedSprite_GetDrawFlag','i32',{'ptr'})
    F.SetExplicitPalette = game.bind('ManagedSprite_SetExplicitPalette','void',{'ptr','i32'})
    F.SetPosition = game.bind('ManagedSprite_SetPositionXY','void',{'ptr','i16','i16'})
    F.GetPosition = game.bind('ManagedSprite_GetPositionXY','void',{'ptr','ptr','ptr'})
    F.ReplaceCharResource = game.bind('SpriteSystem_ReplaceCharResObj','void',{'ptr','ptr','i32','i32','i32','i32'})
    F.ReplacePaletteResource = game.bind('SpriteSystem_ReplacePlttResObj','void',{'ptr','ptr','i32','i32','i32','i32'})
    F.ItemFileID = game.bind('Item_FileID','u16',{'u16','i32'})
    F.EmitClearCommand = game.bind('BattleController_EmitClearCommand','void',{'ptr','i32','i32'})
    F.EmitClearTouchScreen = game.bind('BattleController_EmitClearTouchScreen','void',{'ptr','i32'})
    F.StartTask = game.bind('SysTask_Start','ptr',{'ptr','ptr','u32'})
    F.FinishTask = game.bind('SysTask_Done','void',{'ptr'})
    F.FindResource = game.bind('SpriteResourceCollection_Find','ptr',{'ptr','i32'})
    F.System = game.symbol('gSystem')
end
local flags = {
    0x006D, 0x006F, 0x0094, 0x0095, 0x0096, 0x00A4, 0x00AE, 0x00D4, 0x00EC,
    0x0101, 0x0114, 0x0122, 0x015F, 0x0163, 0x0167, 0x0168, 0x0169, 0x016A, 0x016B,
    0x016C, 0x016D, 0x016E, 0x016F, 0x0170, 0x0171, 0x0187, 0x0193, 0x01C4, 0x01C6,
    0x01CE, 0x01CF, 0x01D0, 0x021F, 0x023E, 0x02CD, 0x02CE, 0x02CF, 0x02D0, 0x02D1,
    0x02D2, 0x02D3, 0x02D4, 0x02D5, 0x02D6, 0x02D7, 0x02D8, 0x02D9, 0x03BF, 0x03C0,
    0x03C1, 0x03C2, 0x03C3, 0x03C4, 0x03C5, 0x03C6, 0x03C7, 0x03C8, 0x03C9, 0x03CA,
    0x03CB, 0x03CC, 0x03CD, 0x03CE, 0x03CF, 0x03D0, 0x03D1, 0x03D2, 0x03D3, 0x03D4,
    0x03D5, 0x03D6, 0x03D7, 0x03D8, 0x03D9, 0x040F, 0x04E4, 0x04F5, 0x053C, 0x053D,
    0x053E, 0x053F, 0x0540, 0x0541, 0x0542, 0x0543, 0x0544, 0x0545, 0x0546, 0x0547,
    0x0548, 0x0549, 0x054A, 0x054B, 0x054C, 0x054D, 0x054E, 0x054F, 0x0974, 0x0975,
    0x0976, 0x0990, 0x0991,
}
local battle, spriteSys, spriteMan, health, frameSprite, ballSprite, task, taskCallback
local anchorX, anchorY, selected, attempts, shortcutBattler = 0, 0, 0, 0, 0
local pending, prepared, allowed = false, false, true
local vars, framePalette, ballPalette
local tries, limitShiny, limitLegendary, always, alwaysShiny, rules, first = 5, false, false, false, false, 0, 2
local function battleMon(context, i) return game.address(context,11608+i*192) end
local function target(current)
    local context = current and F.GetBattleContext(current)
    if not context then return nil end
    local found
    for i=0,F.GetMaxBattlers(current)-1 do
        local mon = battleMon(context,i)
        if F.GetBattlerSide(current,i) ~= 0 and (game.read(context,'u8',12576) & (1 << i)) == 0
            and game.read(mon,'u16') ~= 0 and game.read(mon,'i32',76) > 0 then
            if found then return nil end
            found = mon
        end
    end
    return found
end
local function isShiny(mon) return mon and (game.read(mon,'u8',38) & 32) ~= 0 end
local function remaining(current)
    if tries == 5 then return true end
    local mon = target(current)
    if not mon then return true end
    local shiny, legendary = isShiny(mon),(F.GetBattleStatusMask(current) & 8) ~= 0
    if (shiny or legendary) and not ((shiny and limitShiny) or (legendary and limitLegendary)) then return true end
    return attempts < tries + 1
end
local function partyFrom(party,location)
    if not party then return false end
    for i=0,F.GetPartyCount(party)-1 do
        local mon=F.GetPartyPokemon(party,i)
        if mon and F.GetPokemonValue(mon,76,nil)==0 and F.GetPokemonValue(mon,153,nil)==location then return true end
    end
    return false
end
local function boxesFrom(boxes,location)
    if not boxes then return false end
    for box=0,17 do for slot=0,29 do
        if F.GetBoxMonData(boxes,box,slot,5,nil) ~= 0 and F.GetBoxMonData(boxes,box,slot,76,nil)==0
            and F.GetBoxMonData(boxes,box,slot,153,nil)==location then return true end
    end end
    return false
end
local function prepare(current)
    if prepared then return end
    if first == 2 then prepared=true;return end
    if not current or not vars then return end
    local location=F.GetMapHeader(current)
    local index = location >= 1 and location <= 97 and location or ({[117]=98,[121]=99,[123]=100,[124]=101,[125]=102})[location]
    if not index then prepared=true;return end
    local flag=flags[index]
    if partyFrom(F.GetParty(current,0),location) or boxesFrom(F.GetPCBoxes(current),location) then F.SetFlag(vars,flag) end
    if F.CheckFlag(vars,flag) ~= 0 then allowed,prepared=false,true;return end
    local mon=target(current)
    if not mon then return end
    if first ~= 0 or F.HasCaughtSpecies(current,game.read(mon,'u16')) == 0 then F.SetFlag(vars,flag) end
    prepared=true
end
local function rulesAllow(current)
    if not allowed then return false end
    if rules==0 or not current then return true end
    local party,boxes=F.GetParty(current,0),F.GetPCBoxes(current)
    if not party or not boxes then return true end
    if rules==2 and F.GetPartyCount(party)+F.CountAllBoxMons(boxes)>=6 then return false end
    local location=F.GetMapHeader(current)
    return not partyFrom(party,location) and not boxesFrom(boxes,location)
end
local function canCapture()
    if not battle then return false end
    prepare(battle)
    if (F.GetBattleType(battle) & 0x6a5) ~= 0 then return false end
    local party,boxes=F.GetParty(battle,0),F.GetPCBoxes(battle)
    if not party or not boxes or (F.GetPartyCount(party)==6 and F.GetFirstEmptyBox(boxes)==18) then return false end
    local mon=target(battle)
    return mon ~= nil and remaining(battle) and rulesAllow(battle)
        and (game.read(mon,'u32',128) & 0x200400c0)==0 and (game.read(mon,'u32',112) & 0x1000000)==0
end
local function pocket()
    local bag=battle and F.GetBag(battle)
    return bag and game.address(bag,1724)
end
local function hasSelected()
    local bag=pocket()
    if not bag or selected==0 then return false end
    for i=0,14 do if game.read(bag,'u16',i*4)==selected and game.read(bag,'u16',i*4+2)~=0 then return true end end
    return false
end
local function queuePalette(slot,palette)
    local raw=palette and game.read(palette,'ptr',16)
    return raw and F.VramTransferRequest(14,slot*32,raw,32)~=0
end
local function updateIcon(item)
    if not spriteSys or not spriteMan then return end
    F.ReplaceCharResource(spriteSys,spriteMan,16,F.ItemFileID(item,1),0,51002)
    F.ReplacePaletteResource(spriteSys,spriteMan,16,F.ItemFileID(item,2),0,51003)
    local palette=F.FindResource(game.read(spriteMan,'ptr',32),51003)
    if palette then
        ballPalette=game.read(game.read(palette,'ptr',16),'ptr')
        queuePalette(15,ballPalette)
    end
end
local function refreshBall()
    if hasSelected() then return end
    local bag=pocket()
    local nextBall=0
    if bag then
        for i=0,14 do
            local item=game.read(bag,'u16',i*4)
            if item~=0 and game.read(bag,'u16',i*4+2)~=0 then
                if nextBall==0 or item==4 then nextBall=item end
                if item==4 then break end
            end
        end
    end
    if nextBall==0 then nextBall=4 end
    if selected~=nextBall then selected=nextBall;updateIcon(selected) end
end
local function cycle(direction)
    local bag=pocket()
    if not bag or not hasSelected() then return end
    local slot=-1
    for i=0,14 do if game.read(bag,'u16',i*4)==selected and game.read(bag,'u16',i*4+2)~=0 then slot=i;break end end
    for offset=1,15 do
        local i=(slot+direction*offset+15)%15
        local item=game.read(bag,'u16',i*4)
        if item~=0 and game.read(bag,'u16',i*4+2)~=0 then
            if selected~=item then selected=item;updateIcon(item) end
            return
        end
    end
end
local xy=game.alloc(4)
local function sync()
    local main=health and game.read(health,'ptr',8)
    if not main or not frameSprite or not ballSprite then return end
    F.GetPosition(main,xy,game.address(xy,2))
    local dx,dy=game.read(xy,'i16')-anchorX,game.read(xy,'i16',2)-anchorY
    F.SetPosition(frameSprite,16+dx,70+dy);F.SetPosition(ballSprite,16+dx,74+dy)
    refreshBall()
    local draw=hasSelected() and canCapture() and F.GetDrawFlag(main)~=0 and 1 or 0
    F.SetDrawFlag(frameSprite,draw);F.SetDrawFlag(ballSprite,draw)
end
local function addResource(manager,kind,id,bytes,itemKind)
    local collection=game.read(manager,'ptr',24+kind*8)
    if not collection then return nil end
    local count,capacity=game.read(collection,'i32',20),game.read(collection,'i32',16)
    if count>=capacity then return nil end
    local resources=game.read(collection,'ptr',8)
    local resource
    for i=0,capacity-1 do
        local candidate=game.address(resources,i*24)
        if not game.read(candidate,'ptr') then resource=candidate;break end
    end
    if not resource then return nil end
    local copy
    if itemKind then
        copy=F.ReadItemResource(16,F.ItemFileID(selected~=0 and selected or 4,itemKind),5)
    else
        copy=F.HeapAlloc(5,#bytes)
        if copy then game.copy(copy,game.bytes(bytes),#bytes) end
    end
    if not copy then return nil end
    local raw=F.ResourceCollectionAdd(game.read(collection,'ptr'),copy,id)
    if not raw then return nil end
    game.write(resource,'ptr',raw);game.write(resource,'i32',kind,8)
    game.write(collection,'i32',count+1,20)
    local data=F.HeapAlloc(5,kind<=1 and 16 or 8)
    if not data then return nil end
    local unpack=({F.GetCharacterData,F.GetPaletteData,F.GetCellBank,F.GetAnimBank})[kind+1]
    if unpack(copy,data)==0 then return nil end
    if kind<=1 then game.write(data,'i32',1,8) end
    if kind==1 then game.write(data,'i32',1,12) end
    game.write(resource,'ptr',data,16)
    if kind==0 and F.RequestCharTransfer(resource)==0 then return nil end
    if kind==1 and F.RequestPaletteTransfer(resource)==0 then return nil end
    if F.RegisterLoadedResource(game.read(manager,'ptr',72+kind*8),resource)==0 then return nil end
    return resource,game.read(data,'ptr')
end
local function newSprite(sys,manager,char,palette,y,priority)
    local template=game.alloc(52)
    game.write(template,'i16',16);game.write(template,'i16',y,2)
    game.write(template,'i32',priority,8);game.write(template,'i32',1,16)
    for i,id in ipairs({char,palette,51004,51005,-1,-1}) do game.write(template,'i32',id,20+(i-1)*4) end
    local sprite=F.NewSprite(sys,manager,template)
    if sprite then F.SetDrawFlag(sprite,1) end
    return sprite
end
local function create(current)
    battle,attempts,selected,pending,allowed,prepared=current,0,0,false,true,false
    -- The NPC's demonstration must neither use player rules nor consume a zone's first encounter.
    if (F.GetBattleType(current) & BATTLE_TYPE_CATCH_TUTORIAL) ~= 0 then
        battle=nil
        return
    end
    prepare(current)
    local sys,manager=F.GetSpriteSystem(current),F.GetSpriteManager(current)
    if not sys or not manager then return end
    local specs={{0,51000,'frame.ncgr'},{1,51001,'frame.nclr'},{0,51002,nil,1},
        {1,51003,nil,2},{2,51004,'cell.ncer'},{3,51005,'anim.nanr'}}
    local bytes={}
    for i,spec in ipairs(specs) do
        if spec[3] then
            bytes[i]=game.read_file('fast_ball_battle_'..spec[3])
            if not bytes[i] then game.log('No se encontraron todos los recursos visuales junto al mod');return end
        end
    end
    for i,spec in ipairs(specs) do
        local resource,palette=addResource(manager,spec[1],spec[2],bytes[i],spec[4])
        if not resource then game.log('No se pudieron cargar los recursos visuales');return end
        if i==2 then framePalette=palette elseif i==4 then ballPalette=palette end
    end
    frameSprite=newSprite(sys,manager,51000,51001,70,2)
    ballSprite=newSprite(sys,manager,51002,51003,74,1)
    health=F.GetHealthBox(current,1)
    local main=health and game.read(health,'ptr',8)
    if not frameSprite or not ballSprite or not main then
        game.log('No se pudo crear el indicador visual');frameSprite,ballSprite,health=nil,nil,nil;return
    end
    F.GetPosition(main,xy,game.address(xy,2))
    anchorX,anchorY=game.read(xy,'i16'),game.read(xy,'i16',2)
    F.SetExplicitPalette(frameSprite,14);F.SetExplicitPalette(ballSprite,15)
    if not queuePalette(14,framePalette) or not queuePalette(15,ballPalette) then game.log('No se pudieron transferir las paletas del indicador') end
    spriteSys,spriteMan=sys,manager
    refreshBall();sync()
    task=F.StartTask(taskCallback,nil,1020)
    if not task then game.log('No se pudo sincronizar el indicador con la interfaz del combate') end
end
local function stop()
    if task then F.FinishTask(task);task=nil end
    battle,spriteSys,spriteMan,health,frameSprite,ballSprite=nil,nil,nil,nil,nil,nil
    selected,pending,allowed,prepared=0,false,true,false
end
return {
    id='fast_ball_battle',name='Fast Ball Battle',version='0.1.0',
    on_load=function()
        bindFunctions()
        local emit=game.bind('BattleController_EmitSelectedBagItem','void',{'ptr','i32','u32'})
        taskCallback=game.callback('void',{'ptr','ptr'},sync)
        game.hook('BattleSystem_CreateHealthboxes','void',{'ptr'},function(original,current) original(current);create(current) end)
        game.hook('BattleSystem_DestroyHealthboxes','void',{'ptr'},function(original,current) stop();original(current) end)
        game.hook('BattlerData_Delete','void',{'ptr','ptr','i32'},function(original,current,data,mode)
            if current==battle then stop() end
            original(current,data,mode)
        end)
        game.hook('BattleController_EmitSelectedBagItem','void',{'ptr','i32','u32'},function(original,current,battler,item)
            if current==battle and (item & 65535)~=255 and ((item >> 16) & 255)==2 then
                if not remaining(current) or not rulesAllow(current) then item=255
                elseif canCapture() then attempts=attempts+1 end
            end
            original(current,battler,item)
        end)
        game.hook('SpeciesData_GetSpeciesValue','u32',{'i32','i32'},function(original,species,param)
            if param==8 then
                local mon=target(battle)
                if mon and game.read(mon,'u16')==species and (always or (alwaysShiny and isShiny(mon))) then return 3060 end
            end
            return original(species,param)
        end)
        game.hook('BattleSystem_MenuInput','i32',{'ptr'},function(original,screen)
            local menu=screen and game.read(screen,'i8',2571)
            if menu and menu>=1 and menu<=6 and game.read(screen,'ptr')==battle and canCapture() then
                local keys=game.read(F.System,'u32',116)
                if (keys & 0x100000)~=0 then cycle(-1) end
                if (keys & 0x200000)~=0 then cycle(1) end
                if (keys & 0x200)~=0 and hasSelected() and canCapture() then
                    shortcutBattler=F.GetBattlerOfType(battle,game.read(screen,'u8',2570));pending=true
                    game.write(screen,'u32',0,2696)
                    F.HideCursor(game.read(screen,'ptr',2688));game.write(screen,'u8',1,2700)
                    return 2
                end
            end
            return original(screen)
        end)
        game.hook('BattleDisplay_InitTaskShowBagMenu','void',{'ptr','ptr','ptr'},function(original,current,data,message)
            local battler=game.read(data,'u8',496)
            if pending and current==battle and battler==shortcutBattler and game.read(message,'u8',1)==0
                and game.read(message,'u8',2)==0 and game.read(message,'u8',3)==0 and hasSelected() and canCapture() then
                pending=false
                emit(current,battler,selected | (2 << 16))
                F.EmitClearCommand(current,battler,game.read(message,'u8'));F.EmitClearTouchScreen(current,battler)
                return
            end
            pending=false;original(current,data,message)
        end)
        game.option('max_tries','MAX TRIES',{'1','2','3','4','5','INF'},5,function(v) tries=v end)
        game.option('enable_max_tries_for_legendary','MAX TRIES L',{'FALSE','TRUE'},0,function(v) limitLegendary=v~=0 end)
        game.option('enable_max_tries_for_shiny','MAX TRIES S',{'FALSE','TRUE'},0,function(v) limitShiny=v~=0 end)
        game.option('always_capture','CAPTURE',{'100%','DEFAULT'},1,function(v) always=v==0 end)
        game.option('always_capture_shiny','CAPTURE S',{'100%','DEFAULT'},1,function(v) alwaysShiny=v==0 end)
        game.option('capture_rules','CAPTURE RULES',{'DEFAULT','1 PER ZONE','MAX 6'},0,function(v) rules=v end)
        game.option('only_first','ONLY FIRST',{'IF NOT NEW','TRUE','FALSE'},2,function(v) first=v end)
    end,
    on_event=function(event,data)
        if event==8 and data and game.read(data,'u32')==16 then
            local save=game.read(data,'ptr',8)
            if save then vars=F.GetVarsFlags(save) end
        end
    end,
    on_unload=stop,
}
