local multiplier, trainer, rolls, active = 1, 0, 0, false
return {
    id = 'shiny_rate', name = 'Shiny Rate', version = '0.0.2',
    on_load = function()
        local different = game.bind('Daycare_AreParentLanguagesDifferent', 'i32', {'ptr'})
        local random = game.bind('LCRNG_Next', 'u16', {})
        local shiny = game.bind('Pokemon_FindShinyPersonality', 'u32', {'u32'})
        local nature = game.bind('Pokemon_GetNatureOf', 'u8', {'u32'})
        local gender = game.bind('Pokemon_GetGenderOf', 'u8', {'u16', 'u32'})
        local function force()
            if multiplier <= 1 then return false end
            if rolls * multiplier >= 8192 then return true end
            local low, high = random(), random()
            return (((low | (high << 16)) * (8192 - rolls)) >> 32) < rolls * (multiplier - 1)
        end
        game.hook('Pokemon_InitWith', 'void', {'ptr', 'i32', 'i32', 'i32', 'i32', 'u32', 'i32', 'u32'},
            function(original, pokemon, species, level, ivs, use, personality, source, ot)
                local value = (trainer >> 16) ~ (trainer & 0xffff) ~ (personality >> 16) ~ (personality & 0xffff)
                if active and use ~= 0 and source == 0 and value >= 8 and force() then
                    local n, g, slot = nature(personality), gender(species, personality), personality & 1
                    repeat personality = shiny(trainer)
                    until nature(personality) == n and gender(species, personality) == g and (personality & 1) == slot
                end
                original(pokemon, species, level, ivs, use, personality, source, ot)
            end)
        game.hook('CreateWildMon', 'void', {'u16', 'u8', 'i32', 'ptr', 'ptr', 'ptr'},
            function(original, species, level, destination, params, first, battle)
                trainer, rolls, active = game.read(params, 'u32'), 1, true
                original(species, level, destination, params, first, battle)
                active = false
            end)
        game.hook('Egg_SetInitialData', 'void', {'ptr', 'u16', 'ptr', 'u32', 'u8'},
            function(original, pokemon, species, daycare, ot, form)
                trainer, rolls, active = ot, different(daycare) ~= 0 and 5 or 1, true
                original(pokemon, species, daycare, ot, form)
                active = false
            end)
        local values = {}
        for i = 0, 13 do values[#values + 1] = 'x' .. (1 << i) end
        game.option('multiplier', 'SHINY RATE', values, 4, function(value) multiplier = 1 << value end)
    end,
}
