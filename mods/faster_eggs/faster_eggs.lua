local speed, breeding, breedingMon = 0, 0, nil
return {
    id = 'faster_eggs', name = 'Faster Eggs', version = '0.0.1',
    on_load = function()
        local getStep = game.bind('Daycare_GetStepCounter', 'i32', {'ptr'})
        local setStep = game.bind('Daycare_SetStepCounter', 'void', {'ptr', 'i32'})
        local count = game.bind('Party_GetCurrentCount', 'i32', {'ptr'})
        local mon = game.bind('Party_GetPokemonBySlotIndex', 'ptr', {'ptr', 'i32'})
        local get = game.bind('Pokemon_GetValue', 'u32', {'ptr', 'i32', 'ptr'})
        local set = game.bind('Pokemon_SetValue', 'void', {'ptr', 'i32', 'ptr'})
        local daycareMon = game.bind('Daycare_GetDaycareMon', 'ptr', {'ptr', 'i32'})
        local length = game.bind('Daycare_GetEggCycleLength', 'i32', {'ptr'})
        local zero = game.alloc(1)
        game.hook('DaycareMon_GetSteps', 'u32', {'ptr'}, function(original, pokemon)
            local steps = original(pokemon)
            if pokemon ~= breedingMon or breeding == 0 then return steps end
            local interval = breeding == 4 and 1 or 64 * (4 - breeding)
            return steps % interval == interval - 1 and 255 or 0
        end)
        game.hook('Daycare_GetCompatibilityScore', 'u8', {'ptr'}, function(original, daycare)
            local score = original(daycare)
            return breedingMon and breeding == 4 and score ~= 0 and 101 or score
        end)
        game.hook('Daycare_GetEggCycleLength', 'i32', {'ptr'}, function(original, field)
            local normal = original(field)
            if speed == 0 then return normal end
            return speed == 4 and 1 or (normal * (4 - speed) + 3) // 4
        end)
        game.hook('Daycare_Update', 'i32', {'ptr', 'ptr', 'ptr'}, function(original, daycare, party, field)
            local cycle = length(field)
            if speed ~= 0 and getStep(daycare) >= cycle then setStep(daycare, cycle - 1) end
            if speed == 4 then
                for i = 0, count(party) - 1 do
                    local pokemon = mon(party, i)
                    if get(pokemon, 76, nil) ~= 0 and get(pokemon, 3, nil) == 0 then set(pokemon, 9, zero) end
                end
            end
            breedingMon = breeding ~= 0 and daycareMon(daycare, 1) or nil
            local result = original(daycare, party, field)
            breedingMon = nil
            return result
        end)
        local values = {'0%', '25%', '50%', '75%', '100%'}
        game.option('hatch_speed', 'HATCH SPEED', values, 3, function(value) speed = value end)
        game.option('breeding_speed', 'BREEDING SPEED', values, 3, function(value) breeding = value end)
    end,
}
