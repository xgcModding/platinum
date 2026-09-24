project('pokeplatinum', ['c', 'cpp'],
    version: '1.0',
    meson_version: '>=1.12.0',
    default_options : [
        'buildtype=plain',
        'warning_level=0'
    ]
)

fs = import('fs')
build_target = get_option('build_target')
if build_target not in ['linux', 'osx', 'win64']
    error('This source-only port builds desktop executables. Import your own ROM at runtime.')
endif

if build_target == 'arm'
    add_languages('nasm', native: false)
endif

display_version = fs.read('VERSION.txt').split('\n')[0].strip()
version_config = configuration_data()
version_config.set_quoted('POKEPLATINUM_VERSION', display_version)
configure_file(
    output: 'pokeplatinum_version.h',
    configuration: version_config,
)

############################################################
###                    INCLUDE PATHS                     ###
############################################################
public_includes = include_directories('include', 'asm', 'include/res')
toplevel_includes = include_directories('.')


############################################################
###                    COMPILER FLAGS                    ###
############################################################
if (build_target == 'arm')
shared_args = [
    '-proc', 'arm946e',
    '-enum', 'int',
    '-Cpp_exceptions', 'off',
    '-gccext,on',
    '-msgstyle', 'gcc',
    '-ipa', 'file',
    '-interworking',
    '-inline', 'on,noauto',
    '-char', 'signed',
    '-nosyspath',
    '-stdinc',
    '-sym', 'on',
    '-DPOKEPLATINUM_GENERATED_ENUM',
    '-D_MSL_RESTRICT=',
]
else
shared_args = ['-DPOKEPLATINUM_GENERATED_ENUM', '-g']
if build_target in ['linux', 'win64']
    shared_args += '-fpatchable-function-entry=12'
endif
endif

if get_option('revision') == '0'
    shared_args += [
        '-DPOKEPLATINUM_REVISION=0'
    ]
else
    shared_args += [
        '-DPOKEPLATINUM_REVISION=1'
    ]
endif

if (build_target == 'win64')
    # libntr also declares host-machine helper targets for its desktop build.
    add_global_arguments('-I' + dependency('sdl2', native: true).get_variable(pkgconfig: 'includedir') / 'SDL2',
        language: ['c', 'cpp'], native: false)
	shared_args += [
		'-w',
        # Nitro public headers include SDL.h in every SDK library.
        '-I' + dependency('sdl2', native: true).get_variable(pkgconfig: 'includedir') / 'SDL2',
	]
endif

if (build_target == 'arm')
if get_option('gdb_debugging')
    shared_args += [
        '-O1,p',
#        '-inline', 'off' todo: figure out why this makes the rom crash after the copyright screen
    ]
else
    shared_args += '-O4,p'
endif
endif

if get_option('tracy_enable')
    shared_args += '-DTRACY_ENABLE'
endif

if (build_target == 'nx')
    shared_args += [ '-D__SWITCH__' ]

    if(get_option('tracy_enable'))
    add_global_arguments(['-D__SWITCH__', 
                          '-DTRACY_ENABLE',        
                          '-I/opt/devkitpro/portlibs/switch/include',
                          '-I/opt/devkitpro/libnx/include'],
        language: 'cpp',
        native: false
    )
    else
    add_global_arguments(['-D__SWITCH__',         
                          '-I/opt/devkitpro/portlibs/switch/include',
                          '-I/opt/devkitpro/libnx/include'],
        language: 'cpp',
        native: false
    )
    endif

endif


c_args = []
cpp_args = []
if (build_target == 'arm')
    c_args = shared_args + ['-lang', 'c99']
    cpp_args = shared_args + ['-RTTI', 'off']
else
    c_args = shared_args
    cpp_args = shared_args
endif

add_global_arguments(c_args,
    language: 'c',
    native: (build_target == 'linux' or build_target == 'osx' or build_target == 'win64')
)

add_global_arguments(cpp_args,
    language: 'cpp',
    native: (build_target == 'linux' or build_target == 'osx' or build_target == 'win64')
)

pokeplatinum_args = [
    '-DPM_KEEP_ASSERTS',
    '-DGAME_VERSION=VERSION_PLATINUM',
    '-DGAME_LANGUAGE=LANGUAGE_ENGLISH'
]

if(build_target != 'arm')
    pokeplatinum_args += [
        '-std=c99'
    ]
endif

if(build_target == 'nx')
    pokeplatinum_args += '-fPIC'
endif

if get_option('gdb_debugging')
    pokeplatinum_args += '-DGDB_DEBUGGING'
endif

if get_option('logging_enabled')
    pokeplatinum_args += [
        '-DLOGGING_ENABLED',
    ]
endif

asm_args = [
    '-proc', 'arm5TE',
    '-16',
    '-gccinc'
]

if (build_target == 'arm')
link_args = [
    '-w', 'off',
    '-proc', 'arm946e',
    '-nopic',
    '-nopid',
    '-interworking',
    '-map', 'closure,unused',
    '-symtab', 'sort',
    '-msgstyle' ,'gcc',
    '-m', '_start',
    '-force_active', 'DGTi_hash2_arm4_small',
    '-sym', 'on'
]
elif (build_target == 'nx')
link_args = [
    '-specs=/opt/devkitpro/libnx/switch.specs',
    '-g',
    '-march=armv8-a+crc+crypto',
    '-mtune=cortex-a56',
    '-mtp=soft',
    '-fPIE',
    '--verbose',
    '-Wl,-Map=main.map'
]
else
link_args = [
]
endif

############################################################
###                     DEPENDENCIES                     ###
############################################################
libntr_prj = subproject('libntr',
    default_options: [
        'card_game_code=CPUE',
        'card_maker_code=01'
    ])

nitrosdk_dep = dependency('NitroSDK',
    default_options: {
        'mwrap_sdk': '2.0/sp1p2',
        'sp1p3_conditionals': true,
    }
)

nitrosystem_dep = dependency('NitroSystem',
    default_options: {
        'mwrap_sdk': '2.0/sp2',
    }
)

nitrowifi_dep = dependency('NitroWiFi',
    default_options: {
        'mwrap_sdk': '2.0/sp1p2',
        'sp1p3_conditionals': true,
    }
)

nitrodwc_dep = dependency('NitroDWC',
    default_options: {
        'mwrap_sdk': '2.0/sp2p2',
        'link_ppwlobby': true,
    }
)

libvct_dep = dependency('libvct')
if (build_target == 'arm')
libsyscall_dep = dependency('libsyscall')
endif

############################################################
###                       CONSTS                         ###
############################################################
subdir('generated')


############################################################
###                        TOOLS                         ###
############################################################
subdir('tools')


############################################################
###                      SPEC FILES                      ###
############################################################
# Collect inputs for main.lcf
main_lsf = files('meson/overlays.lsf')
main_lcf_template = subproject('libntr').get_variable('main_lcf_template')

# Generate main.lcf
main_lcf = custom_target('main.lcf',
    output: 'main.lcf',
    input: main_lsf + main_lcf_template,
    command: [makelcf_exe, '@INPUT@','@OUTPUT@']
)

# Filesystem paths contain no resource payloads.
filesys_csv = files('tools/config/resource_paths.csv')


############################################################
###                  INTERNAL LIBRARIES                  ###
############################################################
subdir('lib')


############################################################
###                     FILE SYSTEM                      ###
############################################################
naix_headers = []
h_headers = []
embed_headers = []
nitrofs_files = []

############################################################
###                       MODS                           ###
############################################################
subdir('mods')

############################################################
###                     ARM9 BINARY                      ###
############################################################
subdir('src')

if (build_target == 'arm')
trainer_ai_o = custom_target(
    'trainer_ai_script.o',
    output: 'trainer_ai_script.o',
    input: files('src/battle/trainer_ai/script.s'),
    command: [
        arm_none_eabi_gcc_exe,
        '-I', meson.global_source_root() / 'include',
        '-I', meson.global_source_root() / 'asm',
        '-I', meson.global_build_root(),
        '-x', 'assembler-with-cpp',
        '-MD', '-MF', '@DEPFILE@',
        '-c', '-o', '@OUTPUT@',
        '@INPUT@',
    ],
    depfile: '@BASENAME@.d',
)
elif(build_target == 'nx')
trainer_ai_ss = custom_target(
    'trainer_ai_script.ss',
    output: 'trainer_ai_script.ss',
    input: files('src/battle/trainer_ai/script.s'),
    command: [
        'gcc',
        '-D__ASSEMBLER__',
        '-I', meson.global_source_root() / 'asm',
        '-I', meson.global_build_root(),
        '-I', meson.global_source_root() / 'include',
        '-MD', '-MF', '@DEPFILE@',
        '-E', '-nostdinc', '-'
    ],
    depfile: '@BASENAME@.d',
    capture: true,
    feed: true
)

trainer_ai_o = custom_target(
    'trainer_ai_script.o',
    output: 'trainer_ai_script.o',
    input: trainer_ai_ss,
    command: [
        '/opt/devkitpro/devkitA64/bin/aarch64-none-elf-as',
        '-I', meson.global_source_root() / 'asm',
        '-I', meson.global_build_root(),
        '-o', '@OUTPUT@',
        '@INPUT@',
    ],
    depfile: '@BASENAME@.d'
)
elif(build_target == 'osx')
trainer_ai_ss = custom_target(
    'trainer_ai_script.ss',
    output: 'trainer_ai_script.ss',
    input: files('src/battle/trainer_ai/script.s'),
    command: [
        'gcc',
        '-D__ASSEMBLER__',
        '-I', meson.global_source_root() / 'asm',
        '-I', meson.global_build_root(),
        '-I', meson.global_source_root() / 'include',
        '-MD', '-MF', '@DEPFILE@',
        '-E', '-nostdinc', '-'
    ],
    depfile: '@BASENAME@.d',
    capture: true,
    feed: true
)

trainer_ai_o = custom_target(
    'trainer_ai_script.o',
    output: 'trainer_ai_script.o',
    input: trainer_ai_ss,
    command: [
        'sh', meson.global_source_root() / 'tools/scripts/build_trainer_ai_macos.sh',
        arm_none_eabi_gcc_exe,
        arm_none_eabi_objcopy_exe,
        '@INPUT@', '@OUTPUT@',
    ],
)
else
trainer_ai_ss = custom_target(
    'trainer_ai_script.ss',
    output: 'trainer_ai_script.ss',
    input: files('src/battle/trainer_ai/script.s'),
    command: [
        'gcc',
        '-D__ASSEMBLER__',
        '-I', meson.global_source_root() / 'asm',
        '-I', meson.global_build_root(),
        '-I', meson.global_source_root() / 'include',
        '-MD', '-MF', '@DEPFILE@',
        '-E', '-nostdinc', '-'
    ],
    depfile: '@BASENAME@.d',
    capture: true,
    feed: true
)

trainer_ai_o = custom_target(
    'trainer_ai_script.o',
    output: 'trainer_ai_script.o',
    input: trainer_ai_ss,
    command: [
        'as',
        '-I', meson.global_source_root() / 'asm',
        '-I', meson.global_build_root(),
        '-o', '@OUTPUT@',
        '@INPUT@',
    ],
    depfile: '@BASENAME@.d'
)
endif

if (build_target == 'arm')
libmslc = fs.copyfile('tools/metroskrew/lib/metroskrew/sdk/ds/2.0/sp2/msl/MSL_C/MSL_ARM/Lib/MSL_C_NITRO_Ai_LE.a')
libmslcpp = fs.copyfile('tools/metroskrew/lib/metroskrew/sdk/ds/2.0/sp2/msl/MSL_C++/MSL_ARM/Lib/MSL_CPP_NITRO_Ai_LE.a')

main = executable('main',
    sources: [
        pokeplatinum_c,
        c_consts_generators,
        naix_headers,
        h_headers,
        embed_headers,
    ],
    objects: trainer_ai_o,
    c_args: [
        pokeplatinum_args,
        '-thumb'
    ],
    c_pch: 'include/pch/global_pch.h',
    include_directories: [
        public_includes,
        toplevel_includes,
        libcrypto_public_includes,
        libgds_public_includes,
        libspl_public_includes,
        ppwlobby_public_includes,
    ],
    dependencies: [
        nitrosdk_dep,
        nitrosystem_dep,
        nitrowifi_dep,
        nitrodwc_dep,
        libvct_dep,
        libsyscall_dep,
    ],
    link_with: [
        libcrypto_ov97,
        libcrypto_static,
        libgds,
        libspl,
        libmslc,
        libmslcpp,
        libdwcenc,
        libdwcppwlobby,
        libdwcilobby
    ],
    link_args: [
        link_args,
        'main.lcf'
    ],
    link_depends: main_lcf,
    native: false
)
endif
if (build_target == 'linux')
dl_dep = meson.get_compiler('c').find_library('dl')
elif (build_target == 'osx')
dl_dep = declare_dependency()
endif

rom_import_dep = declare_dependency()
if build_target in ['linux', 'osx', 'win64']
    rom_picker_deps = [dependency('sdl2', native: true)]
    if build_target == 'linux'
        rom_picker_source = ['src/port/rom_picker_linux.c', 'src/port/native_menu_linux.cpp']
        rom_picker_deps += dependency('qt6', modules: ['Core', 'Gui', 'Widgets'], private_headers: true, native: true)
        rom_picker_deps += dependency('dbus-1', native: true)
    elif build_target == 'osx'
        add_languages('objc', native: true)
        rom_picker_source = ['src/port/rom_picker_macos.m', 'src/port/native_menu_macos.m']
        rom_picker_deps += dependency('appleframeworks', modules: ['AppKit'], native: true)
    else
        rom_picker_source = ['src/port/rom_picker_windows.c', 'src/port/native_menu_windows.c']
        rom_picker_deps += meson.get_compiler('c', native: true).find_library('comdlg32')
    endif
    rom_import = static_library('rom_import', ['src/port/rom_import.cpp', rom_picker_source],
        include_directories: include_directories('include'),
        dependencies: rom_picker_deps, override_options: ['cpp_std=c++17'], native: true)
    rom_import_dep = declare_dependency(link_whole: rom_import, dependencies: rom_picker_deps)
endif

lua_mod_dep = declare_dependency()
if build_target in ['linux', 'osx', 'win64']
    # Lua is embedded. macOS supplies libffi; Linux links it statically.
    lua_mod_dep = [subproject('lua').get_variable('lua_dep'),
        dependency('libffi', native: true, static: build_target == 'linux'),
        dependency('threads', native: true)]
    pokeplatinum_c += files('src/port/lua_mod.c')
endif

if (build_target == 'linux' or build_target == 'osx')
port_keyboard_dep = declare_dependency()
if build_target == 'osx'
    add_languages('objc', native: true)
    # Keep Carbon headers outside the game's Nitro precompiled header.
    port_keyboard = static_library('port_keyboard', 'src/port/keyboard_macos.m',
        dependencies: [dependency('sdl2'), dependency('appleframeworks', modules: ['Carbon'])])
    port_keyboard_dep = declare_dependency(link_with: port_keyboard,
        dependencies: dependency('appleframeworks', modules: ['Carbon']))
endif
port_main_args = [pokeplatinum_args]
if (build_target == 'linux' or build_target == 'osx')
    port_main_args += [
        '-O3',
        '-fno-inline',
        '-fkeep-static-functions',
        '-fno-ipa-cp',
        '-fno-ipa-cp-clone',
        '-fno-ipa-icf',
        # Mod replacements may clobber every caller-saved register allowed by
        # the ABI, even when the original local function leaves them untouched.
        '-fno-ipa-ra',
        '-fno-ipa-sra',
        '-fno-optimize-sibling-calls',
        '-fno-strict-aliasing',
        '-fno-delete-null-pointer-checks',
        '-fwrapv',
    ]
endif

main = executable('main',
    sources: [
        pokeplatinum_c,
        c_consts_generators,
        naix_headers,
        h_headers,
        embed_headers,
    ],
    objects: trainer_ai_o,
    c_args: port_main_args,
    c_pch: 'include/pch/global_pch.h',
    include_directories: [
        public_includes,
        toplevel_includes,
        libcrypto_public_includes,
        libgds_public_includes,
        libspl_public_includes,
        ppwlobby_public_includes,
    ],
    dependencies: [
        nitrosdk_dep,
        nitrosystem_dep,
        nitrowifi_dep,
        nitrodwc_dep,
        libvct_dep,
        dl_dep,
        port_keyboard_dep,
        lua_mod_dep,
        rom_import_dep,
    ],
    link_with: [
        libspl,
        libcrypto_ov97,
        libcrypto_static,
        libgds,
        libdwcenc,
        libdwcppwlobby,
        libdwcilobby,
    ],
    link_args: [
        link_args,
    ],
    export_dynamic: true,
    link_depends: main_lcf,
    native: true
)
endif
if (build_target == 'win64')
ws2_32_lib = meson.get_compiler('c').find_library('ws2_32')

ws2_32_dep = declare_dependency(dependencies: [ws2_32_lib])

# Windows headers conflict with Nitro types; compile the platform layer without
# the game's precompiled header.
mod_windows = static_library('mod_windows', 'src/port/mod_platform_windows.c',
    include_directories: toplevel_includes, native: true)
windows_resources = import('windows').compile_resources('tools/config/windows.rc',
    depend_files: files('icon.ico'))

main = executable('main',
    sources: [
        windows_resources,
        pokeplatinum_c,
        c_consts_generators,
        naix_headers,
        h_headers,
        embed_headers,
    ],
    objects: trainer_ai_o,
    c_args: [
        pokeplatinum_args,
        '-O3', '-fno-inline', '-fkeep-static-functions', '-fno-ipa-cp',
        '-fno-ipa-cp-clone', '-fno-ipa-icf', '-fno-ipa-ra', '-fno-ipa-sra',
        '-fno-optimize-sibling-calls', '-fno-strict-aliasing',
        '-fno-delete-null-pointer-checks', '-fwrapv',
    ],
    c_pch: 'include/pch/global_pch.h',
    include_directories: [
        public_includes,
        toplevel_includes,
        libcrypto_public_includes,
        libgds_public_includes,
        libspl_public_includes,
        ppwlobby_public_includes,
    ],
    dependencies: [
        nitrosdk_dep,
        nitrosystem_dep,
        nitrowifi_dep,
        nitrodwc_dep,
        libvct_dep,
        ws2_32_dep,
        lua_mod_dep,
        rom_import_dep,
    ],
    link_with: [
        mod_windows,
        libspl,
        libcrypto_ov97,
        libcrypto_static,
        libgds,
        libdwcenc,
        libdwcppwlobby,
        libdwcilobby,
    ],
    link_args: [
        link_args,
    ],
    link_depends: main_lcf,
    native: true
)
endif
if (build_target == 'nx')
main = executable('main',
    sources: [
        pokeplatinum_c,
        c_consts_generators,
        naix_headers,
        h_headers,
        embed_headers,
    ],
    objects: trainer_ai_o,
    c_args: [
        pokeplatinum_args,
    ],
    c_pch: 'include/pch/global_pch.h',
    include_directories: [
        public_includes,
        toplevel_includes,
        libcrypto_public_includes,
        libgds_public_includes,
        libspl_public_includes,
        ppwlobby_public_includes,
    ],
    dependencies: [
        nitrosdk_dep,
        nitrosystem_dep,
        nitrowifi_dep,
        nitrodwc_dep,
        libvct_dep,
    ],
    link_with: [
        libspl,
        libcrypto_ov97,
        libcrypto_static,
        libgds,
    ],
    link_args: [
        link_args,
    ],
    link_depends: main_lcf,
    native: false
)
endif

############################################################
###                    ARM7 BINARIES                     ###
############################################################
ichneumon_sub = subproject('libntr').get_variable('ichneumon_sub')
ichneumon_sub_defs = []


############################################################
###                   DESKTOP RELEASE                    ###
############################################################
copy_files_script = [find_program('bash'), files('tools/scripts/copy_pc_files.sh')]

custom_target('desktop-release',
    output: 'desktop-release',
    input: [main, mod_artifacts],
    command: [copy_files_script, meson.global_build_root(), filesys_csv,
        meson.get_external_property('release_dir', meson.project_source_root() / ('build_' + build_target)),
        meson.project_source_root()],
    depend_files: files('tools/scripts/create_macos_app.py', 'icon.png'),
    build_by_default: true,
)

############################################################
###                   GDB HELPERS                        ###
############################################################
nef_fixer = custom_target('debug.nef',
    output: [
        'debug.nef'
    ],
    input: [
        main
    ],
    command : [
        nef_fixer_py, '@INPUT@', '@OUTPUT@'
    ]
)

ovly_mapper = custom_target('overlay.map',
    output: [
        'overlay.map'
    ],
    input: [
        main_lsf
    ],
    command : [
        overlay_mapper_py, '@INPUT@', '@OUTPUT@'
    ]
)


############################################################
###                        TESTS                         ###
############################################################
############################################################
###                       POSTCONF                       ###
############################################################
meson.add_postconf_script(postconf_py)
