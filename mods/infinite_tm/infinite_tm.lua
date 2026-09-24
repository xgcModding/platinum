local enabled, teachingBag, teachingItem = true, nil, 0
return {
    id = 'infinite_tm', name = 'Infinite TM', version = '0.0.1',
    on_load = function()
        local canRemove = game.bind('Bag_CanRemoveItem', 'i32', {'ptr', 'u16', 'u16', 'i32'})
        game.hook('TeachMove', 'void', {'ptr', 'ptr', 'u32'}, function(original, app, pokemon, slot)
            local oldBag, oldItem = teachingBag, teachingItem
            local menu = game.read(app, 'ptr', 1752)
            local item = game.read(menu, 'u16', 68)
            teachingBag = enabled and item >= 328 and item <= 419 and game.read(menu, 'ptr', 8) or nil
            teachingItem = item
            original(app, pokemon, slot)
            teachingBag, teachingItem = oldBag, oldItem
        end)
        game.hook('Bag_TryRemoveItem', 'i32', {'ptr', 'u16', 'u16', 'i32'}, function(original, bag, item, count, heap)
            if teachingBag and bag == teachingBag and item == teachingItem and count == 1 then
                return canRemove(bag, item, count, heap)
            end
            return original(bag, item, count, heap)
        end)
        game.option('Enabled', 'Enabled', {'TRUE', 'FALSE'}, 0, function(value) enabled = value == 0 end)
    end,
}
