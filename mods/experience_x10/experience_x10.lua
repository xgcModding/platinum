local multiplier = 1
return {
    id = 'experience_x10', name = 'Experience', version = '0.0.3',
    on_load = function()
        local values = {}
        for i = 0, 10 do values[#values + 1] = 'x' .. i end
        game.option('multiplier', 'EXP MULTIPLIER', values, 1, function(value) multiplier = value end)
    end,
    on_event = function(event, data)
        if event ~= 6 or not data or multiplier == 1 or game.read(data, 'u32') ~= 32 then return end
        game.write(data, 'u32', math.min(0xffffffff, game.read(data, 'u32', 24) * multiplier), 24)
    end,
}
