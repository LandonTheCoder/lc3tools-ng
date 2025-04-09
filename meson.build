project('lc3tools', 'c', license: 'GPL-2.0-only')

cc = meson.get_compiler('c')

flex = find_program('flex', '/usr/bin/flex', required: true)
sed = find_program('sed', '/usr/bin/sed', required: true)
wish = find_program('wish', '/usr/bin/wish', '/sw/bin/wish', required: true)
# Needed to generate OS-inclusion headers (to include it in the binary)
xxd = find_program('xxd', '/usr/bin/xxd', '/bin/xxd', required: true)

ncurses = dependency('ncurses')
readline = dependency('readline')
# Indicates to build with readline support
add_project_arguments('-DUSE_READLINE=1', language: 'c')

# Test for needed functions
if not cc.has_function('fmemopen')
    error('fmemopen is required')
endif

env = environment()
# This should allow running built programs
env.append('PATH', meson.project_build_root())

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
                            command: ['scripts/generate-header-for-binary.sh', '@INPUT@', '@OUTPUT@'])

lc3os_sym_h = custom_target('lc3os_sym_h',
                            input: lc3os_sym,
                            output: 'lc3os-sym.h',
                            command: ['scripts/generate-header-for-binary.sh', '@INPUT@', '@OUTPUT@'])

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
                          command: ['scripts/create-lc3sim-tk.sh',
                                    '@INPUT@', '@OUTPUT@'])
