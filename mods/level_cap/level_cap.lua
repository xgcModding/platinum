local cap, save, frames, inCard = 5, nil, 0, false
local rules, F = {}, {}
local badges = {'coal_badge','forest_badge','relic_badge','cobble_badge','fen_badge','mine_badge','icicle_badge','beacon_badge'}
local checkpoints = {
    {16548,3,'route_201_rival'}, {16520,1,'route_203_rival'},
    {16521,2,'valley_windworks_mars'}, {16507,2,'route_209_rival'},
    {16508,2,'pastoria_rival'}, {16504,1,'canalave_rival'},
    {16598,1,'galactic_hq_cyrus'}, {16469,10,'distortion_world_cyrus'},
}
local function recover(name) cap = math.max(cap, rules[name] or 0) end
local function refresh()
    if not save then return end
    cap = 5
    local trainer, flags = F.trainer(save), F.flags(save)
    if trainer and flags then
        for i = 1, math.min(F.badges(trainer), 8) do recover(badges[i]) end
        for _, rule in ipairs(checkpoints) do
            local pointer = F.var(flags, rule[1])
            if pointer and game.read(pointer, 'u16') >= rule[2] then recover(rule[3]) end
        end
        if F.flag(flags, 129) ~= 0 then recover('galactic_eterna_cleared') end
        if F.flag(flags, 2404) ~= 0 then recover('champion') end
    end
    game.option_label('level_cap', tostring(cap))
end
return {
    id = 'level_cap', name = 'Level Cap', version = '0.0.1',
    on_load = function()
        F.get = game.bind('Pokemon_GetValue','u32',{'ptr','i32','ptr'})
        F.exp = game.bind('Pokemon_GetSpeciesBaseExpAt','u32',{'i32','i32'})
        F.trainer = game.bind('SaveData_GetTrainerInfo','ptr',{'ptr'})
        F.badges = game.bind('TrainerInfo_BadgeCount','u32',{'ptr'})
        F.flags = game.bind('SaveData_GetVarsFlags','ptr',{'ptr'})
        F.flag = game.bind('VarsFlags_CheckFlag','i32',{'ptr','u16'})
        F.var = game.bind('VarsFlags_GetVarAddress','ptr',{'ptr','u16'})
        local copyString = game.bind('String_CopyChars','void',{'ptr','ptr'})
        local title = game.alloc(20)
        local letters = {310,303,320,303,310,478,301,299,314,65535}
        for i, code in ipairs(letters) do game.write(title,'u16',code,(i-1)*2) end
        game.hook('TrainerCard_DrawFrontText','void',{'ptr','ptr'},function(original,windows,card)
            local old = game.read(card,'u32',40)
            game.write(card,'u32',cap,40)
            inCard = true
            original(windows,card)
            inCard = false
            game.write(card,'u32',old,40)
        end)
        game.hook('MessageLoader_GetString','void',{'ptr','u32','ptr'},function(original,loader,id,text)
            if inCard and id == 4 then copyString(text,title) else original(loader,id,text) end
        end)
        local json = game.read_file('level_caps.json')
        if not json then game.log('level_caps.json was not found; using level cap 5')
        else
            local count = 0
            for object in json:gmatch('{([^}]+)}') do
                local name = object:match('"checkpoint"%s*:%s*"([^"\\]+)"')
                local value = tonumber(object:match('"level_cap"%s*:%s*(%d+)'))
                if name and #name < 160 and value and value >= 1 and value <= 100 then
                    if rules[name] then rules[name] = math.max(rules[name],value)
                    elseif count < 256 then rules[name], count = value, count + 1 end
                end
            end
        end
        game.option('level_cap','LEVEL CAP',{'5'},0,nil,1)
    end,
    on_frame = function()
        if not save then return end
        frames = frames + 1
        if frames >= 30 then frames = 0; refresh() end
    end,
    on_event = function(event,data)
        if not data then return end
        local size = game.read(data,'u32')
        if event == 8 and size == 16 then save,frames = game.read(data,'ptr',8),0; refresh()
        elseif event == 6 and size == 32 then
            local mon = game.read(data,'ptr',8)
            if not mon then return end
            local granted = 0
            if F.get(mon,161,nil) < cap then
                local max = F.exp(F.get(mon,5,nil),cap)
                granted = math.min(game.read(data,'u32',24),math.max(0,max-game.read(data,'u32',16)))
            end
            game.write(data,'u32',granted,24)
        elseif event == 7 and size == 24 then
            local mon = game.read(data,'ptr',8)
            if mon and F.get(mon,161,nil) >= cap then game.write(data,'u8',1,18) end
        end
    end,
}
