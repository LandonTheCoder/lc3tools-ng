# LC-3 Tools #
This is based on the lc3tools for Unix from [McGraw-Hill Education](https://highered.mheducation.com/sites/0072467509/student_view0/lc-3_simulator.html). It adds significant changes to modernize the code, like a new build system.

It contains an assembler for LC-3, `lc3as`, a command-line simulator, `lc3sim`, and a graphical interface around that simulator, `lc3sim-tk`. The original code was written by Steven S. Lumetta, and is accessible unmodified on the `original-code` branch.

In accordance with the original source, the lc3tools-ng software distribution
is free software covered by version 2.0 of the GNU General Public License, and
you are welcome to modify it and/or distribute copies of it under certain
conditions.  The file COPYING (distributed with the tools) specifies those
conditions.  There is absolutely no warranty for the LC-3 tools distribution,
as described in the file NO_WARRANTY (also distributed with the tools).

## Installing ##
To compile this software yourself, you will need the following tools:
 - GCC, or a reasonably recent compatible compiler such as Clang.
 - The Meson Build System.
 - flex (creates parsers for `lc3as` and `lc3convert`)
 - wish from Tcl/Tk (for the `lc3sim-tk` graphical interface)
 - sed (for post-processing)

The following are recommended:
 - `pkg-config`, or a compatible implementation such as `pkgconf`
   - This is used to detect optional library dependencies (readline). If not present, it will not include readline support.
 - The readline library (optional, allows line editing in lc3sim).
   - On Debian/Ubuntu-based Linux distributions, this is available in the `libreadline-dev` package.
 - The `xxd` program (used to include data into an executable, but a fallback is included).

This software is built through the Meson build system. Accordingly, it follows 3 simple steps to build (once dependencies are set up):

```sh
meson setup builddir
meson compile -C builddir
sudo meson install -C builddir
```

You can specify configuration options to Meson (such as install location), if desired. To get optimized executables, specify the `--buildtype=release` option to the `meson setup` command (or `--buildtype=debugoptimized` if you still want debugging symbols). Where I stated "builddir", you can enter any name of a directory you want it to build in.

## Changes ##

This program additionally fixes some issues with detection of dependencies. It would not support building with Clang without build-system modifications, which would block building with the default toolchain on BSDs and modern macOS. It also failed to detect non-static versions of the readline library, and didn't search correct directories for dependencies (e.g. `/usr/lib/x86_64-linux-gnu` for 64-bit libraries on Debian/Ubuntu). This fixes those issues by using modern build tooling.

It also includes the LC-3 operating system into the simulator at build time, instead of loading it from a location determined at build time (usually the build directory).

I also fixed the vast majority of the compiler warnings.

The `lc3sim` code has been massively reorganized, and some variable and argument types have been changed. For example, most status variables are now booleans.

I added support for using libedit in the place of readline. This is not well-tested.

Behavioral changes: `lc3sim-tk` would wait 250ms to display output from the LC-3 if a newline wasn't present, which could cause noticeable input lag. Now it only waits 5ms. It also has optional support for an idle mode when LC-3 is waiting for input (which can be disabled with the "idle\_sleep" feature).

## To-Do ##

### Bug Fixes ###
 - Fix the macOS "hangs when close button is clicked" bug
 - Find more macOS bugs
 - Bug under libedit: tab completion can erroneously escape spaces in a file name
 - Bug with readline/libedit: weird behavior around filename completion when spaces are in a file name

### Porting ###
 - Test on Macs (at least more so)
 - Work on support for using libedit instead of libreadline (using readline compatibility)
 - Have better endian handling. It currently byteswaps unconditionally, which may break when run on big-endian systems. Make macros which convert to and from big endian.
 - Make it run under MSYS2 on Windows systems
   - Stretch goal: make it run under native Windows. This requires replacing usage of a few functions in `lc3sim`:
     - `poll()` (checks simulator input in `simple\_readline()` and `execute\_instruction()`, and LC-3 input in `flush\_console\_input()` and `read\_memory()`)
       - This is complicated to replace because it can `poll()` on console, pipe, and network socket, and Windows only has an implementation for 1 natively. I have to figure out how to emulate it, or change the code more substantially.
     - `socket()` and `connect()` (used in `launch\_gui\_connection()`, support using Winsock APIs instead, see `sys/socket.h` and `netinet/in.h`)
     - `struct termios`, `tcgetattr()`, `tcsetattr()` (used in `run\_until\_stopped()`, there should be a Windows console API for that?)
     - Support alternatives for `srandom()` and `random()` (`srand()` and `rand()` can probably be aliased on Windows)

### Enhancements ###
 - Create a .desktop file for `lc3sim-tk`
   - Create an icon for lc3sim-tk
 - Make it an installable application on macOS (it currently requires building either manually or with Homebrew).
 - Maybe more refactoring to how `lc3sim` integrates with the GUI?
 - Rework command-line argument handling in most programs. For example, support the `--help` option.
 - Maybe rework code to store memory in 16-bit integers instead of default-sized integers? This would save memory, but have limited benefits otherwise (and require some narrowing casts).
