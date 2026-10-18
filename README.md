# Pokémon Platinum

Pokémon Platinum for Windows, Linux and macOS, with mod support with LUA. I made this port for fun and to try things out. I also used AI for the development.

I did not did the decompilation process, the original author I can't add a link because their repository contains the 
full decompiled game with assets included so it's illegal, I removed every copyrighted data, and I added mod support,
increased performance and also unlock the framerate.

I was able to complete the game despite I did not fully tested the whole game so I can't garantee it's working 100%. 

## Play

Download your platform's build from [Releases](../../releases), extract it and launch:

- **Windows:** `main.exe`
- **Linux:** `pokeplatinum.AppImage` — mark it as executable first.
- **macOS:** `Pokémon Platinum.app`

The Linux x86-64 AppImage includes Qt, its plugins, SDL and the C/C++ runtime;
you do not need to install a matching Qt version. It supports Wayland and X11
and requires a working graphics driver with OpenGL 4.2 support. Shipped mods are
included and installed beside the AppImage on first launch, preserving existing
settings. Keep the AppImage in a writable folder. If FUSE is unavailable, launch
it with `--appimage-extract-and-run`.

Choose **File → Import ROM…** and select your Pokémon Platinum `.nds` file. Both US and European versions are supported. The game starts once the import finishes. You'll need your own ROM; it isn't included.

## Controls

| Action         | Keyboard            |
|----------------|---------------------|
| Move           | Arrow keys          |
| A / B          | Z / X               |
| X / Y          | C / V               |
| L / R          | A / D               |
| Start / Select | Enter / Right Shift |
| Save           | F5                  |
| Reset          | F9                  |
| Swap screens   | F12                 |
| Map            | M                   |

Use the mouse for the touch screen. Keyboard and controller bindings are under **Input → Configure Controls…**, including shortcuts for the bike, fishing, field moves and Pokétch apps.

## New options

Available in the in-game **Options** menu, alongside the original settings.

| Option                       | What it does                                                                               |
|------------------------------|--------------------------------------------------------------------------------------------|
| Move Speed / Battle Speed    | Adjust movement and battles separately, from 1× to 4×. Both start at 2×.                   |
| Capture Exp                  | Earn experience from catching Pokémon.                                                     |
| Exp Share                    | Share experience with the rest of your party without held Exp. Share items.                |
| Hide PKMN                    | Hide the opponent's next Pokémon when you're offered a switch.                             |
| Resolution                   | Increase the rendering resolution.                                                         |
| Window Size / Monitor        | Choose a window size, fullscreen mode and display.                                         |
| Screen Layout / Swap Screens | Stack the screens, put them side by side or enlarge the main screen; swap their positions. |
| FPS / VSync                  | Set the frame rate and toggle vertical sync.                                               |
| FPS Counter                  | Show the current frame rate.                                                               |
| Camera Distance              | Zoom the camera out.                                                                       |
| Skip Intro                   | Skip to the load screen or straight into your game.                                        |
| Autosave                     | Save automatically after battles, zone changes and visits to the Pokémon Center or PC.     |

## Included mods

Open **MODS** in the game menu and select a mod to change its settings.

### Auto HM

Use HMs in the field without teaching them to a Pokémon. You still need the HM in your Bag, the required badge and, by default, a Pokémon that can learn it. **IGNORE PARTY** removes that last requirement. **ANIMATIONS** lets you keep or skip the field-move animations and prompts.

Dig and Flash also work without occupying a move slot if you have their TM and select a compatible Pokémon. Teleport is available to Pokémon whose species can learn it.

### Battle Encounters

Change how often you run into wild Pokémon while moving around. **100%** keeps the normal encounter rate, **50%** halves it and **0%** turns random encounters off. The setting goes up to **200%**. It doesn't remove trainer battles or scripted encounters.

### Better Berries

**GROWTH SPEED** reduces how long planted berries take to grow: **0%** is normal, **75%** cuts the wait to a quarter and **100%** makes them ready to harvest immediately. The intermediate settings also speed up drying and withering, so remember to water and harvest them.

**CONSUMPTION** controls what happens to held berries used in battle:

- **Default:** the berry is consumed as usual.
- **Refill:** after battle, equip another of the same berry from your Bag, if you have one.
- **Inf:** after battle, return the consumed berry without taking one from your Bag.

Starts at **75%** growth reduction and **Inf** consumption. Berries aren't replenished during the battle.

### Experience

Multiply the experience your Pokémon earn. **1×** is normal, **2×** doubles it and **0×** stops experience gain. Goes up to **10×** and starts at **1×**. Level Cap still applies if that mod is loaded.

### Fast Ball Battle

Throw a Poké Ball without opening the Bag. Use **Q / E** to cycle through the balls you have, then press **L** — **A on the keyboard** by default — to throw the selected one during your turn.

The capture settings also let you change the rules:

- **MAX TRIES:** allow one to five throws per battle, or unlimited attempts. Separate switches decide whether the limit applies to legendary and shiny Pokémon.
- **CAPTURE / CAPTURE S:** guarantee catches for all Pokémon or just shinies, while other capture restrictions still apply.
- **CAPTURE RULES:** keep normal rules, allow one catch per area, or combine that restriction with a limit of six Pokémon across your party and PC.
- **ONLY FIRST:** restrict catches to the first encounter in an area, even if it escapes or faints. **IF NOT NEW** skips species you've already caught when deciding which encounter uses that chance.

Starts with normal catch chances, unlimited attempts and no area restrictions.

### Faster Eggs

Two separate settings reduce the steps needed to **produce an Egg at the Day Care** and to **hatch an Egg in your party**. **0%** keeps the normal pace; **75%** cuts the step intervals to a quarter. At **100%**, compatible parents can produce an Egg on the next step and Eggs in your party can hatch without the usual wait. Incompatible parents still can't breed.

Both settings start at **75%**.

### Infinite Honey

Slather Honey Trees without using up your Honey. You still need at least one Honey in your Bag. **WAIT HOURS** changes the wait from zero to six hours; zero makes the tree ready immediately. A tree can still attract nothing, and Honey still expires after 24 hours.

The mod starts disabled. Enabling it uses a one-hour wait unless you change the setting. Using Honey directly from the Bag still consumes it.

### Infinite TM

Teaching a move no longer uses up its TM, so one copy can teach multiple Pokémon. You still have to obtain the TM, and each Pokémon must be able to learn the move. Enabled by default.

### Level Cap

Stops Pokémon from gaining experience once they reach the current level limit. Rare Candies can't take them past it either. The limit rises as you beat key story battles and earn badges: it starts at **5**, rises to **9** after the first rival battle and reaches **100** after becoming Champion.

Check the current limit on your Trainer Card or in the mod's menu. Already being above the cap won't lower a Pokémon's level. This mod is active by default.

### New evolutions

Evolve Pokémon that normally require trading by leveling them up instead. Enabled by default.

| Evolution               | New requirement                 |
|-------------------------|---------------------------------|
| Kadabra → Alakazam      | Level 32                        |
| Graveler → Golem        | Level 42                        |
| Haunter → Gengar        | Level 42                        |
| Machoke → Machamp       | Level 45                        |
| Onix → Steelix          | Level up holding a Metal Coat   |
| Scyther → Scizor        | Level up holding a Metal Coat   |
| Rhydon → Rhyperior      | Level up holding a Protector    |
| Dusclops → Dusknoir     | Level up holding a Reaper Cloth |
| Porygon → Porygon2      | Level up holding an Up-Grade    |
| Porygon2 → Porygon-Z    | Level up holding a Dubious Disc |
| Electabuzz → Electivire | Level up holding an Electirizer |
| Magmar → Magmortar      | Level up holding a Magmarizer   |

Held-item evolutions work day or night and consume the item.

### No Hidden Items

Show markers over hidden items so you can spot them while exploring. You still pick them up normally; the marker disappears once the item is collected. Enabled by default.

### nuzzlocke

Two optional rules, both disabled by default:

- **PKMN DIE:** Pokémon that faint in battle are removed from your party afterward and sent to the PC, starting with the last box. Pokémon in that last box can't be withdrawn while the rule is active.
- **PKMN LIMIT:** match the opposing trainer's team size. If they have two Pokémon, only your first two fit Pokémon take part; the rest return to your party afterward.

For first-encounter and one-catch-per-area rules, use **Fast Ball Battle**.

### Pokédex Battle

Select a species in the Pokédex list and press **Y** — **V on the keyboard** by default — to fight it. Your lead Pokémon battles alone against a wild Pokémon of the same level. Your other party members return afterward.

Normally, you can battle species you've seen but can only catch species you've already caught. **BATTLE UNSEEN** lets you select unseen species, and **CATCH UNCAUGHT** lets you catch a species for the first time through these battles. Both settings start off.

### Shiny Rate

Increase shiny odds for newly generated wild Pokémon and Eggs. Choose from **1×** (the original odds) up to **8192×** (guaranteed shiny). Starts at **16×**. Changing the setting doesn't turn Pokémon you already own into shinies.

## Compilation

I forgot every dependency required

`make linux|windows|osx`
