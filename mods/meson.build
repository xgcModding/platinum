ncgr_gen = generator(nitrogfx_exe, arguments: ['@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@'], output: '@BASENAME@.NCGR')
nclr_gen = generator(nitrogfx_exe, arguments: ['@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@'], output: '@BASENAME@.NCLR')
ncer_gen = generator(nitrogfx_exe, arguments: ['@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@'], output: '@BASENAME@.NCER')
nanr_gen = generator(nitrogfx_exe, arguments: ['@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@'], output: '@BASENAME@.NANR')

# Keep port-only assets in the executable, outside the original ROM archives.
mod_loader_icon_ncgr = ncgr_gen.process(
    files('mod_loader/res/graphics/start_menu/icon.png'),
    extra_args: ['-clobbersize', '-version101'],
)
mod_loader_icon_header = custom_target('mod_loader_icon.h',
    input: mod_loader_icon_ncgr,
    output: 'mod_loader_icon.h',
    command: [find_program('python3'), files('mod_loader/embed_icon.py'), '@INPUT@', '@OUTPUT@'],
)
embed_headers += mod_loader_icon_header

mod_artifacts = []

if build_target in ['linux', 'osx', 'win64']

    foreach name : ['autohm', 'battle_encounters', 'better_berries', 'experience_x10',
                    'fast_ball_battle', 'faster_eggs', 'infinite_honey', 'infinite_tm',
                    'level_cap', 'new_evolutions', 'no_hidden_items', 'nuzzlocke', 'pokedex_battle',
                    'shiny_rate']
        mod_artifacts += configure_file(input: name / (name + '.lua'),
            output: name + '.lua', copy: true)
    endforeach
    lua_license = configure_file(input: 'lua-LICENSE.txt', output: 'lua-license.txt', copy: true)
    ffi_license = configure_file(input: 'libffi-LICENSE.txt', output: 'libffi-license.txt', copy: true)
    mod_artifacts += [lua_license, ffi_license]

    fast_ball_battle_frame_ncgr = ncgr_gen.process(
        files('fast_ball_battle/res/graphics/battle/misc/cell_indexed.png'),
        extra_args: ['-bitdepth', '4', '-clobbersize', '-mappingtype', '64'],
    )
    fast_ball_battle_frame_nclr = nclr_gen.process(
        files('fast_ball_battle/res/graphics/battle/misc/cell_indexed.png'),
        extra_args: ['-bitdepth', '4'],
    )
    fast_ball_battle_cell_ncer = ncer_gen.process(
        files('fast_ball_battle/res/graphics/battle/misc/cell_cell.json'),
    )
    fast_ball_battle_anim_nanr = nanr_gen.process(
        files('fast_ball_battle/res/graphics/battle/misc/single_frame_anim.json'),
    )
    fast_ball_battle_frame_ncgr_file = custom_target(
        'fast_ball_battle_frame_ncgr_file',
        input: fast_ball_battle_frame_ncgr,
        output: 'fast_ball_battle_frame.ncgr',
        command: [copy_exe, '@INPUT@', '@OUTPUT@'],
    )
    fast_ball_battle_frame_nclr_file = custom_target(
        'fast_ball_battle_frame_nclr_file',
        input: fast_ball_battle_frame_nclr,
        output: 'fast_ball_battle_frame.nclr',
        command: [copy_exe, '@INPUT@', '@OUTPUT@'],
    )
    fast_ball_battle_cell_ncer_file = custom_target(
        'fast_ball_battle_cell_ncer_file',
        input: fast_ball_battle_cell_ncer,
        output: 'fast_ball_battle_cell.ncer',
        command: [copy_exe, '@INPUT@', '@OUTPUT@'],
    )
    fast_ball_battle_anim_nanr_file = custom_target(
        'fast_ball_battle_anim_nanr_file',
        input: fast_ball_battle_anim_nanr,
        output: 'fast_ball_battle_anim.nanr',
        command: [copy_exe, '@INPUT@', '@OUTPUT@'],
    )
    mod_artifacts += [
        fast_ball_battle_frame_ncgr_file,
        fast_ball_battle_frame_nclr_file,
        fast_ball_battle_cell_ncer_file,
        fast_ball_battle_anim_nanr_file,
    ]

endif
