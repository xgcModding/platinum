rapidjson_dep = dependency('rapidjson')

# Native tools with helper libraries
subdir('nitroarc') # libnitroarc
subdir('enumproc') # libenum
subdir('dataproc') # Requires libnitroarc and libenum
subdir('msgenc')   # Requires libdataproc

# Other native tools
subdir('datagen')
subdir('fixrom')
subdir('jsoncnv')
subdir('nitrobtx')
subdir('nitrogfx')
subdir('nitrosfx')
subdir('nitrorom')
subdir('ordergen')
subdir('postconf')
subdir('scripts')
subdir('debug')

copy_exe = find_program('cp')

# Only handheld builds (and the macOS trainer-AI helper) use the
# ARM cross-toolchain. Linux/Windows desktop builds use native tools.
# Do not require ARM programs while configuring a Linux AppImage.
if build_target in ['arm', 'osx']
    arm_none_eabi_gcc_exe = find_program('arm-none-eabi-gcc', native: true)
    arm_none_eabi_objcopy_exe = find_program('arm-none-eabi-objcopy', native: true)
endif

# makelcf generates a Nintendo DS linker control file, not used by the
# desktop executable or its AppImage packaging.
if build_target == 'arm'
    makelcf_exe = find_program('makelcf', native: true)
endif
