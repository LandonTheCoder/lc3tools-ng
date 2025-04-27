project('lc3tools-ng', 'c', version: '13.0', license: 'GPL-2.0-only')

cc = meson.get_compiler('c')

use_xxd = get_option('xxd')
use_readline = get_option('readline')
# Prefer readline over libedit, I'm using a readline compatibility layer for
# libedit anyways
use_libedit = get_option('libedit').disable_auto_if(use_readline.enabled())
# Enforce mutually-exclusive readline/libedit (this errors if both are enabled)
use_readline = use_readline.require(not use_libedit.enabled(),
                                    error_message: 'Readline cannot be enabled at the same time as libedit')
enable_idle = get_option('idle_sleep')
hardcode_wish_path = get_option('hardcode_wish_path')

flex = find_program('flex', '/usr/bin/flex', required: true)
sed = find_program('sed', '/usr/bin/sed', '/bin/sed', required: true)
wish = find_program('wish', '/usr/bin/wish', '/sw/bin/wish', required: true)
# Needed to generate OS-inclusion headers (to include it in the binary)
xxd = find_program('xxd', '/usr/bin/xxd', '/bin/xxd', '/usr/local/bin/xxd',
                   required: use_xxd)

# Dependencies for lc3sim
lc3sim_deps = []
# Optional features for lc3sim
lc3sim_options = []

readline = dependency('readline', required: use_readline)
# Indicates to build with readline support
if readline.found()
    lc3sim_options += '-DUSE_READLINE=1'
    lc3sim_deps += readline
    summary('readline', true, bool_yn: true)
    summary('libedit', false, bool_yn: true)
else
    summary('readline', false, bool_yn: true)
    libedit = dependency('libedit', required: use_libedit)
    if libedit.found()
        lc3sim_options += '-DUSE_READLINE=1'
        lc3sim_deps += libedit
        summary('libedit', true, bool_yn: true)
    else
        summary('libedit', false, bool_yn: true)
    endif
endif

# Test for needed functions
# These keep it from running on Windows.
cc.check_header('sys/poll.h', required: true)
cc.has_header_symbol('sys/socket.h', 'socket', required: true)
cc.has_header_symbol('sys/socket.h', 'connect', required: true)
# Defines for IPv4 networking
cc.check_header('netinet/in.h', required: true)
# It requires these 3 functions from termios currently
cc.has_header_symbol('sys/termios.h', 'tcgetattr', required: true)
cc.has_header_symbol('sys/termios.h', 'tcsetattr', required: true)
cc.has_header_symbol('sys/termios.h', 'tcflush', required: true)

# Check for srandom(), random()
# random()/srandom() are easily replaced by rand()/srand(), though.
cc.has_header_symbol('stdlib.h', 'srandom', required: true)

if cc.has_header_symbol('time.h', 'nanosleep', required: enable_idle)
    lc3sim_options += '-DLC3SIM_IDLE'
    summary('idle_sleep', true, bool_yn: true)
else
    summary('idle_sleep', false, bool_yn: true)
endif

# This must be done after add_project_arguments() call.
if not xxd.found()
    summary('xxd_header_gen', false, bool_yn: true)
    # Fallback for `xxd -i`
    header_gen = executable('generate-header',
                            'scripts/generate-header.c',
                            install: false)
else
    summary('use_xxd', true, bool_yn: true)
    header_gen = 'scripts/generate-header-for-binary.sh'
endif

# Path to wish may optionally be hardcoded
if hardcode_wish_path == true
    wish_sub=wish.full_path()
else
    wish_sub='wish'
endif

summary('wish_path', wish_sub)

env = environment()
# This should allow running built programs
env.set('WISH', wish_sub)

# Some OSes were assigned custom fonts.
if host_machine.system() == 'darwin'
    # In the configure script, macOS sometimes gets custom fonts.
    # This may not apply to all builds of Tk on macOS.
    env.set('CODE_FONT', '{LucidaTypewriter 11 normal}')
    env.set('BUTTON_FONT', '{Fixed 10 normal}')
    env.set('CONSOLE_FONT', '{Fixed 10 normal}')
endif

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
                    c_args: ['-DLC3SIM_INCBIN=1',
                             '-DMAP_LOCATION_TO_SYMBOL'] + lc3sim_options,
                    dependencies: lc3sim_deps,
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
