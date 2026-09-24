local enabled, owner = true, nil
local model, animationSet, animation, render = game.alloc(40), game.alloc(56), game.alloc(56), game.alloc(144)
local freeAnimation, freeModel
local function release()
    if not owner then return end
    freeAnimation(animation); freeAnimation(animationSet); freeModel(model)
    owner = nil
end
return {
    id = 'no_hidden_items', name = 'No Hidden Items', version = '0.0.1',
    on_load = function()
        local events = game.bind('MapHeaderData_GetBgEvents', 'ptr', {'ptr'})
        local count = game.bind('MapHeaderData_GetNumBgEvents', 'i32', {'ptr'})
        local flag = game.bind('FieldSystem_CheckFlag', 'i32', {'ptr', 'u16'})
        local hiddenFlag = game.bind('Script_GetHiddenItemFlag', 'u16', {'u16'})
        local xPos = game.bind('PlayerAvatar_GetXPos', 'i32', {'ptr'})
        local zPos = game.bind('PlayerAvatar_GetZPos', 'i32', {'ptr'})
        local height = game.bind('TerrainCollisionManager_GetHeight', 'i32', {'ptr','i32','i32','i32','ptr'})
        local loadModel = game.bind('FieldEffectManager_LoadModel', 'void', {'ptr','ptr','u32','u32','i32'})
        local loadAnimation = game.bind('FieldEffectManager_LoadAnimation', 'void', {'ptr','ptr','u32','u32','i32'})
        local copy = game.bind('FieldEffectManager_ApplyAnimCopyToModel', 'void', {'ptr','ptr','ptr','ptr','u32'})
        local create = game.bind('Simple3D_CreateRenderObjectWithAnim', 'void', {'ptr','ptr','ptr'})
        local update = game.bind('Simple3D_UpdateAnim', 'i32', {'ptr','i32','i32'})
        local draw = game.bind('Simple3D_DrawRenderObjWithPos', 'void', {'ptr','ptr'})
        freeAnimation = game.bind('Simple3D_FreeAnimation', 'void', {'ptr'})
        freeModel = game.bind('Simple3D_FreeModel', 'void', {'ptr'})
        game.hook('FieldEffectManager_Free', 'void', {'ptr'}, function(original, manager)
            if manager == owner then release() end
            original(manager)
        end)
        game.hook('FieldEffectManager_Render', 'void', {'ptr'}, function(original, manager)
            original(manager)
            local field = game.read(manager, 'ptr', 16)
            local player = game.read(field, 'ptr', 120)
            if not enabled or game.read(field, 'i32', 224) ~= 0 or not player
                or not game.read(field, 'ptr', 40) or not game.read(field, 'ptr', 184) then return end
            local list = events(field)
            if not list then return end
            local x, z, visible = xPos(player), zPos(player), 0
            local position = game.alloc(12)
            for i = 0, count(field) - 1 do
                local event = game.address(list, i * 20)
                local ex, ez = game.read(event, 'i32', 4), game.read(event, 'i32', 8)
                if game.read(event, 'u16', 2) == 2 and math.abs(ex - x) <= 32 and math.abs(ez - z) <= 32
                    and flag(field, hiddenFlag(game.read(event, 'u16'))) == 0 then
                    if owner ~= manager then
                        release()
                        loadModel(manager, model, 0, 92, 0)
                        loadAnimation(manager, animationSet, 0, 164, 0)
                        copy(manager, animation, model, animationSet, 0)
                        create(render, model, animation)
                        owner = manager
                    end
                    if game.read(model, 'i32') == 0 then return end
                    local px, pz = (ex * 16 + 8) * 4096, (ez * 16 + 8) * 4096
                    game.write(position, 'i32', px)
                    game.write(position, 'i32', height(field, game.read(event, 'i32', 12) * 4096, px, pz, nil) + 12 * 4096, 4)
                    game.write(position, 'i32', pz, 8)
                    draw(render, position)
                    visible = visible + 1
                end
            end
            if owner == manager and visible ~= 0 then update(animation, 4096, 1) end
        end)
        game.option('enabled', 'Enabled', {'FALSE','TRUE'}, 1, function(value) enabled = value == 1 end)
    end,
    on_unload = release,
}
