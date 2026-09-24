local rate = 4
return {
    id = 'battle_encounters', name = 'Battle Encounters', version = '0.0.1',
    on_load = function()
        local random = game.bind('LCRNG_RandMod', 'u16', {'u16'})
        game.option('encounter_rate', 'ENCOUNTER RATE',
            {'0%', '25%', '50%', '75%', '100%', '125%', '150%', '175%', '200%'}, 4,
            function(value) rate = value end)
        game.hook('CheckEncounterRateSuccess', 'i32', {'ptr', 'u32'}, function(original, field, base)
            if rate == 4 then return original(field, base) end
            if rate == 0 or base == 0 then return 0 end
            return random(400) < base * rate and 1 or 0
        end)
    end,
}
