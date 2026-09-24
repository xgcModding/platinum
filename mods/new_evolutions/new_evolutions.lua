local enabled = true
local EVOLUTION_ARCHIVE = 34 -- NARC_INDEX_POKETOOL__PERSONAL__EVO
local EVO_LEVEL, EVO_HELD_DAY, EVO_HELD_NIGHT = 4, 18, 19

-- Species and item IDs follow generated/species.txt and generated/items.txt.
-- Each rule retains the named species' immediate evolution.
local rules = {
    [64]  = {target = 65,  level = 32}, -- Kadabra -> Alakazam
    [75]  = {target = 76,  level = 42}, -- Graveler -> Golem
    [95]  = {target = 208, item = 233}, -- Onix: Metal Coat
    [67]  = {target = 68,  level = 45}, -- Machoke -> Machamp
    [93]  = {target = 94,  level = 42}, -- Haunter -> Gengar
    [112] = {target = 464, item = 321}, -- Rhydon: Protector
    [356] = {target = 477, item = 325}, -- Dusclops: Reaper Cloth
    [137] = {target = 233, item = 252}, -- Porygon: Upgrade
    [233] = {target = 474, item = 324}, -- Porygon2: Dubious Disc
    [123] = {target = 212, item = 233}, -- Scyther: Metal Coat
    [125] = {target = 466, item = 322}, -- Electabuzz: Electirizer
    [126] = {target = 467, item = 323}, -- Magmar: Magmarizer
}

return {
    id = 'new_evolutions', name = 'New evolutions', version = '0.0.3',
    on_load = function()
        -- Use the game's normal level-up checks, Everstone, cancellation and
        -- held-item consumption. Paired day/night entries allow either time.
        game.hook('NARC_ReadWholeMemberByIndexPair', 'void', {'ptr', 'i32', 'i32'},
            function(original, destination, archive, species)
                original(destination, archive, species)
                local rule = rules[species]
                if not enabled or archive ~= EVOLUTION_ARCHIVE or not rule then return end
                -- Seven SpeciesEvolution entries, each containing three u16s.
                -- Replace the former condition rather than retaining trade or
                -- level alternatives alongside the requested condition.
                for offset = 0, 40, 2 do game.write(destination, 'u16', 0, offset) end
                local function entry(offset, method, parameter)
                    game.write(destination, 'u16', method, offset)
                    game.write(destination, 'u16', parameter, offset + 2)
                    game.write(destination, 'u16', rule.target, offset + 4)
                end
                if rule.level then
                    entry(0, EVO_LEVEL, rule.level)
                else
                    entry(0, EVO_HELD_DAY, rule.item)
                    entry(6, EVO_HELD_NIGHT, rule.item)
                end
            end)
        game.option('Enabled', 'Enabled', {'TRUE', 'FALSE'}, 0,
            function(value) enabled = value == 0 end)
    end,
}
