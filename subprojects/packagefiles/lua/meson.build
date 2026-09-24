project('lua', 'c', version: '5.4.9')
lua_sources = files(
  'src/lapi.c', 'src/lcode.c', 'src/lctype.c', 'src/ldebug.c', 'src/ldo.c',
  'src/ldump.c', 'src/lfunc.c', 'src/lgc.c', 'src/llex.c', 'src/lmem.c',
  'src/lobject.c', 'src/lopcodes.c', 'src/lparser.c', 'src/lstate.c',
  'src/lstring.c', 'src/ltable.c', 'src/ltm.c', 'src/lundump.c', 'src/lvm.c',
  'src/lzio.c', 'src/lauxlib.c', 'src/lbaselib.c', 'src/lmathlib.c',
  'src/lstrlib.c', 'src/ltablib.c', 'src/lutf8lib.c',
)
lua_lib = static_library('lua', lua_sources, native: true,
    include_directories: include_directories('src'), install: false)
lua_dep = declare_dependency(link_with: lua_lib,
    include_directories: include_directories('src'),
    dependencies: meson.get_compiler('c', native: true).find_library('m', required: false))
