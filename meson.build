project('lc3tools-ng', 'c', version: '13.0', license: 'GPL-2.0-only')

cc = meson.get_compiler('c')

flex = find_program('flex', '/usr/bin/flex', required: true)
sed = find_program('sed', '/usr/bin/sed', '/bin/sed', required: true)
wish = find_program('wish', '/usr/bin/wish', '/sw/bin/wish', required: true)
# Needed to generate OS-inclusion headers (to include it in the binary)
xxd = find_program('xxd', '/usr/bin/xxd', '/bin/xxd', '/usr/local/bin/xxd',
                   required: get_option('xxd'))

readline = dependency('readline', required: get_option('readline'))
# Indicates to build with readline support
if readline.found()
    add_project_arguments('-DUSE_READLINE=1', language: 'c')
endif

# Test for needed functions
# This header is the sole thing keeping it from building on native Windows.
cc.check_header('sys/poll.h', required: true)


# This must be done after add_project_arguments() call.
if not xxd.found()
    message('Building header generator instead of using xxd')
    # Fallback for `xxd -i`
    header_gen = executable('generate-header',
                            'scripts/generate-header.c',
                            install: false)
else
    header_gen = 'scripts/generate-header-for-binary.sh'
endif

# Path to wish may optionally be hardcoded
if get_option('hardcode_wish_path') == true
    message('Hardcoding wish path')
    wish_sub=wish.full_path()
    message(wish_sub)
else
    wish_sub='wish'
endif

env = environment()
# This should allow running built programs
env.set('WISH', wish_sub)

# Build lc3as
lc3_lex = custom_target('lc3_lex',
                        input: 'lc3.f',
                        output: 'lex.lc3.c',
                        command: [flex, '-i', '-Plc3', '-o', '@OUTPUT@', '@INPUT@'])

lc3as = executable('lc3as', lc3_lex, 'symbol.c', install: true)

# Build lc3convert
lc3convert_lex = custom_target('lc3convert_lex',
                               input: 'lc3convert.f',
                               output: 'lex.lc3convert.c',
                               command: [flex, '-i', '-Plc3convert', '-o', '@OUTPUT@', '@INPUT@'])

lc3convert = executable('lc3convert', lc3convert_lex, install: true)

# Now for lc3sim
# Note: lc3as can only generate files in the input directory, so we use a wrapper.
lc3os = custom_target('lc3os',
                      input: 'lc3os.asm',
                      output: ['lc3os.obj', 'lc3os.sym'],
                      depends: lc3as,
                      command: ['scripts/compile-lc3-to-dir.sh',
                                '@INPUT@',
                                '@OUTDIR@',
                                '@BUILD_ROOT@/lc3as'])
# Index retrieves the output file
lc3os_obj = lc3os[0]
lc3os_sym = lc3os[1]

lc3os_obj_h = custom_target('lc3os_obj_h',
                            input: lc3os_obj,
                            output: 'lc3os-obj.h',
                            command: [header_gen, '@INPUT@', '@OUTPUT@'])

lc3os_sym_h = custom_target('lc3os_sym_h',
                            input: lc3os_sym,
                            output: 'lc3os-sym.h',
                            command: [header_gen, '@INPUT@', '@OUTPUT@'])

lc3sim = executable('lc3sim', 'lc3sim.c', 'symbol.c', lc3os_obj_h, lc3os_sym_h,
                    c_args: ['-DLC3SIM_INCBIN=1', '-DMAP_LOCATION_TO_SYMBOL'],
                    dependencies: [readline],
                    install: true)

# Now for lc3sim-tk, this requires substituting paths out.
lc3sim_tk = custom_target('lc3sim_tk',
                          input: 'lc3sim-tk.def',
                          output: 'lc3sim-tk',
                          install: true,
                          install_tag: 'runtime',
                          install_dir: get_option('bindir'),
                          install_mode: 'rwxr-xr-x',
                          env: env,
                          command: ['scripts/create-lc3sim-tk.sh',
                                    '@INPUT@', '@OUTPUT@'])
