local enabled, wait = true, 6
return {
    id = 'infinite_honey', name = 'Infinite Honey', version = '0.0.4',
    on_load = function()
        local check = game.bind('ScrCmd_CheckItem', 'i32', {'ptr'})
        local read = game.bind('ScriptContext_ReadHalfWord', 'u16', {'ptr'})
        local variable = game.bind('FieldSystem_TryGetVar', 'u16', {'ptr', 'u16'})
        local member = game.bind('FieldSystem_GetScriptMemberPtr', 'ptr', {'ptr', 'u32'})
        game.hook('ScrCmd_RemoveItem', 'i32', {'ptr'}, function(original, context)
            if enabled then
                local copy = game.alloc(248)
                game.copy(copy, context, 248)
                local field = game.read(context, 'ptr', 240)
                local item = variable(field, read(copy))
                local count = variable(field, read(copy))
                if item == 94 and count == 1 and game.read(member(field, 8), 'u16') == 2008
                    and game.read(member(field, 13), 'ptr') == context then return check(context) end
            end
            return original(context)
        end)
        game.hook('SixHoursSinceSlathered', 'i32', {'i32'}, function(original, remaining)
            if not enabled then return original(remaining) end
            return remaining > 0 and remaining <= (24 - wait) * 60 and 1 or 0
        end)
        game.option('enabled', 'ENABLE MOD', {'DISABLED', 'ENABLED'}, 0, function(value) enabled = value == 1 end)
        game.option('wait_hours', 'WAIT HOURS', {'0','1','2','3','4','5','6'}, 1, function(value) wait = value end)
    end,
}
