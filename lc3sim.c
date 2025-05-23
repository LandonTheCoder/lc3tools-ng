/* tab:8
 *
 * lc3sim.c - the main source file for the LC-3 simulator
 *
 * "Copyright (c) 2003 by Steven S. Lumetta."
 * Copyright (c) 2025 by LandonTheCoder.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written 
 * agreement is hereby granted, provided that the above copyright notice
 * and the following two paragraphs appear in all copies of this software,
 * that the files COPYING and NO_WARRANTY are included verbatim with
 * any distribution, and that the contents of the file README are included
 * verbatim as part of a file named README with any distribution.
 * 
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE TO ANY PARTY FOR DIRECT, 
 * INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT 
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE AUTHOR 
 * HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * THE AUTHOR SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 * A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" 
 * BASIS, AND THE AUTHOR NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, 
 * UPDATES, ENHANCEMENTS, OR MODIFICATIONS."
 *
 * Author:	    Steve Lumetta
 * Version:	    1
 * Creation Date:   18 October 2003
 * Filename:	    lc3sim.c
 * History:
 *	SSL	1	18 October 2003
 *		Copyright notices and Gnu Public License marker added.
 */

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
// For the gui_mode boolean
#include <stdbool.h>
#include <stdint.h>
// Used to set SIGINT (Ctrl-C) handler
#include <signal.h>
#include <strings.h>
// Used for poll() on input to simulator and LC-3
#include <sys/poll.h>
// Used in run_until_stopped()
#include <sys/termios.h>
#include <sys/types.h>
// Used for socket(), connect() in launch_gui_connection()
#include <sys/socket.h>
// Defines sockaddr_in?
#include <netinet/in.h>
#include <time.h>
#include <unistd.h>

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "lc3sim.h"
#include "symbol.h"

#ifdef LC3SIM_INCBIN
#include "lc3os-obj.h"
#include "lc3os-sym.h"
#endif

/* Disassembly format specification. */
#define OPCODE_WIDTH 6 

/* NOTE: hardcoded in scanfs! */
#define MAX_CMD_WORD_LEN    41    /* command word limit + 1 */
#define MAX_FILE_NAME_LEN  251    /* file name limit + 1    */
#define MAX_LABEL_LEN       81    /* label limit + 1        */

#define MAX_SCRIPT_DEPTH    10    /* prevent infinite recursion in scripts */
#define MAX_FINISH_DEPTH 10000000 /* avoid waiting to finish subroutine    */
                                  /* that recurses infinitely              */

#define TOO_MANY_ARGS     "WARNING: Ignoring excess arguments."
#define BAD_ADDRESS       \
        "Addresses must be labels or values in the range x0000 to xFFFF."

/*
 * Types of breakpoints.  Currently only user breakpoints are
 * handled in this manner; the system breakpoint used for the
 * "next" command is specified by sys_bpt_addr.
 */
typedef enum bpt_type_t bpt_type_t;
enum bpt_type_t {BPT_NONE, BPT_USER};

// Internal function pre-declarations
static char * simple_readline(const char *prompt);

static void show_state_if_stop_visible(void);
static void disassemble_one(int addr);
static void disassemble(int addr_s, int addr_e);

// Declare the implementations of the simulator commands
static void cmd_break(const char *args);
static void cmd_continue(const char *args);
static void cmd_dump(const char *args);
static void cmd_execute(const char *args);
static void cmd_file(const char *args);
static void cmd_finish(const char *args);
static void cmd_help(const char *args);
static void cmd_list(const char *args);
static void cmd_memory(const char *args);
static void cmd_next(const char *args);
static void cmd_option(const char *args);
static void cmd_printregs(const char *args);
static void cmd_quit(const char *args);
static void cmd_register(const char *args);
static void cmd_reset(const char *args);
static void cmd_step(const char *args);
static void cmd_translate(const char *args);
static void cmd_lc3_stop(const char *args);

typedef enum cmd_flag_t cmd_flag_t;
enum cmd_flag_t {
    CMD_FLAG_NONE       = 0,
    CMD_FLAG_REPEATABLE = 1, /* pressing ENTER repeats command  */
    CMD_FLAG_LIST_TYPE  = 2, /* pressing ENTER shows more       */
    CMD_FLAG_GUI_ONLY   = 4  /* only valid in GUI mode          */
};

typedef void (*command_func_t)(const char *);

typedef struct command_t command_t;
struct command_t {
    // Command string
    char *command;
    // Shortest allowed abbreviation (usually 1)
    int min_len;
    // Command's implementing function
    command_func_t cmd_func;
    // Flags for command's properties
    cmd_flag_t flags;
};

// Command definitions
static const struct command_t command[] = {
    {"break",     1, cmd_break,     CMD_FLAG_NONE      },
    {"continue",  1, cmd_continue,  CMD_FLAG_REPEATABLE},
    {"dump",      1, cmd_dump,      CMD_FLAG_LIST_TYPE },
    {"execute",   1, cmd_execute,   CMD_FLAG_NONE      },
    {"file",      1, cmd_file,      CMD_FLAG_NONE      },
    {"finish",    3, cmd_finish,    CMD_FLAG_REPEATABLE},
    {"help",      1, cmd_help,      CMD_FLAG_NONE      },
    {"list",      1, cmd_list,      CMD_FLAG_LIST_TYPE },
    {"memory",    1, cmd_memory,    CMD_FLAG_NONE      },
    {"next",      1, cmd_next,      CMD_FLAG_REPEATABLE},
    {"option",    1, cmd_option,    CMD_FLAG_NONE      },
    {"printregs", 1, cmd_printregs, CMD_FLAG_NONE      },
    {"quit",      4, cmd_quit,      CMD_FLAG_NONE      },
    {"register",  1, cmd_register,  CMD_FLAG_NONE      },
    {"reset",     5, cmd_reset,     CMD_FLAG_NONE      },
    {"step",      1, cmd_step,      CMD_FLAG_REPEATABLE},
    {"translate", 1, cmd_translate, CMD_FLAG_NONE      },
    {"x",         1, cmd_lc3_stop,  CMD_FLAG_GUI_ONLY  },
    {NULL,        0, NULL,          CMD_FLAG_NONE      }
};

// LC-3 state
static int lc3_register[NUM_REGS];
#define REG(i) lc3_register[(i)]
static int lc3_memory[65536];
static bool lc3_show_later[65536];
static bpt_type_t lc3_breakpoints[65536];

/* startup script or file */
static char *start_script = NULL;
static char *start_file = NULL;

static bool should_halt = true;
// Maybe this is also a boolean?
static int last_KBSR_read = 0, last_DSR_read = 0;
static bool gui_mode;
static bool interrupted_at_gui_request = false;
static bool stop_scripts = false;
static bool in_init = false;
static bool have_mem_to_dump = false;
static bool need_a_stop_notice = false;
static int sys_bpt_addr = -1, finish_depth = 0;
static inst_flag_t last_flags;
/* options and script recursion level */
static bool flush_on_start = true;
static bool keep_input_on_stop = true;
static bool rand_device = true;
static bool delay_mem_update = true;
static bool script_uses_stdin = true;
static int script_depth = 0;

/* I/O seen by the LC-3 */
static FILE *lc3in;
static FILE *lc3out;
/* The command channel for the simulator */
static FILE *sim_in;
static char * (*lc3readline)(const char *) = simple_readline;

/* This can be used for a busy-wait mitigation mechanism. */
static unsigned int kbsr_waits = 0;
#ifdef LC3SIM_IDLE
// This data is used for sleeping when waiting for input.
#include <limits.h>
static const struct timespec idle_sleep = {
  .tv_nsec = 500
};
#endif

static const char * const ccodes[8] = {
    "BAD_CC", "POSITIVE", "ZERO", "BAD_CC",
    "NEGATIVE", "BAD_CC", "BAD_CC", "BAD_CC"
};

// Start implementation


// Miscellaneous utility functions


// Halts LC-3 when Ctrl-C is recieved
void halt_lc3(int sig) {
    /* Set the signal handler again, which has the effect of overriding
       the Solaris behavior and making signal look like sigset, which
       is non-standard and non-portable, but the desired behavior. */
    signal(SIGINT, halt_lc3);

    /* has no effect unless LC-3 is running... */
    should_halt = true;

    /* print a stop notice after ^C */
    need_a_stop_notice = true;
}

// Fallback line reader for when readline isn't present
static char * simple_readline(const char *prompt) {
    char buf[200];
    char *strip_nl;
    struct pollfd p;

    /* If we exhaust all commands after being interrupted by the
       GUI, start running again... */
    if (gui_mode) {
        p.fd = fileno(sim_in);
        p.events = POLLIN;
        if ((poll(&p, 1, 0) != 1 || (p.revents & POLLIN) == 0) &&
            interrupted_at_gui_request) {
            /* flag is reset to 0 in cmd_continue */
            return strdup("c");
        }
    }

    /* Prompt and read a line until successful. */
    while (1) {

#if !defined(USE_READLINE)
        if (!gui_mode && script_depth == 0)
            printf("%s", prompt);
#endif
        /* read a line */
        if (fgets(buf, 200, sim_in) != NULL)
            break;

        /* no more input? */
        if (feof(sim_in))
            return NULL;

        /* Otherwise, probably a CTRL-C, so print a blank line and
           (possibly) another prompt, then try again. */
        puts("");
    }

    /* strip carriage returns and linefeeds */
    for (strip_nl = buf + strlen(buf) - 1;
         strip_nl >= buf && (*strip_nl == '\n' || *strip_nl == '\r');
         strip_nl--);
    *++strip_nl = 0;

    return strdup(buf);
}

// A simple getline() implementation
// Note: read_size is same as strlen() result.
static char * lc3s_getline(char *outbuf,
                           int outsize,
                           const char *inbuf,
                           int insize,
                           int *read_size) {
    // Should act like fgets() but on a byte buffer
    // Stores up to size - 1 bytes (and '\0'), but upon a newline, it stores
    // the newline, stops, and stores a '\0' byte after it.
    int index = 0;
    *read_size = 0;
    if (outsize <= 1) {
        // Invalid read
        return NULL;
    }
    if (insize < 1) {
        return NULL;
    }

    int size;
    if (outsize > insize + 1) {
        size = insize + 1;
    } else {
        size = outsize;
    }

    // The last byte is reserved for '\0' byte
    for (index = 0; index < size - 1; index++) {
        // Copy byte
        outbuf[index] = inbuf[index];
        if (inbuf[index] == '\n') {
            // Stop reading
            break;
        }
    }
    // Correct it to index of next valid byte
    index++;
    // Now add '\0' byte
    outbuf[index] = '\0';
    *read_size = index;
    return outbuf;
}

// Clear all console input
static void flush_console_input(void) {
    struct pollfd p;

    /* Check option and script level.  Flushing would consume
       remainder of a script. */
    if (!flush_on_start || script_depth > 0)
        return;

    /* Read a character at a time... */
    p.fd = fileno(lc3in);
    p.events = POLLIN;
    while (poll(&p, 1, 0) == 1 && (p.revents & POLLIN) != 0)
        fgetc(lc3in);
}

// This clears the symbol table for a stretch of memory.
static void squash_symbols(int addr_s, int addr_e) {
    while (addr_s != addr_e) {
        remove_symbol_at_addr(addr_s);
        addr_s = (addr_s + 1) & 0xFFFF;
    }
}

// "Too many arguments" warning
static void warn_too_many_args(void) {
    /* Spaces in entry boxes in the GUI appear as
       extra arguments when handed to the command line;
       we silently ignore them. */
    if (!gui_mode)
        puts(TOO_MANY_ARGS);
}

// Warn when argument is sent to a no-arguments command
static void no_args_allowed(const char *args) {
    if (*args != '\0')
        warn_too_many_args();
}


// Address parsing


// Parse address string to integer
static int parse_address(const char *addr) {
    symbol_t *label;
    char *fmt;
    int value, negated;
    unsigned char trash[2];

    /* default matching order: symbol, hexadecimal */
    /* hexadecimal can optionally be preceded by x or X */
    /* decimal must be preceded by # */

    if (addr[0] == '-') {
        addr++;
        negated = 1;
    } else
        negated = 0;
    if ((label = find_symbol(addr, NULL)) != NULL)
        value = label->addr;
    else {
        if (*addr == '#')
            fmt = "#%d%1s";
        else if (tolower(*addr) == 'x')
            fmt = "x%x%1s";
        else
            fmt = "%x%1s";
        if (sscanf(addr, fmt, &value, trash) != 1 || value > 0xFFFF ||
            ((negated && value < 0) || (!negated && value < -0xFFFF)))
            return -1;
    }
    if (negated)
        value = -value;
    if (value < 0)
        value += 0x10000;
    return value;
}

// Parse address range from string
static int parse_range(const char *args,
                       int *startptr, // Output variable
                       int *endptr, // Output variable
                       int last_end,
                       int scale) {
    char arg1[MAX_LABEL_LEN], arg2[MAX_LABEL_LEN], trash[2];
    int num_args, start, end;

    /* Split and count the arguments. */
    /* 80 == MAX_LABEL_LEN - 1 */
    num_args = sscanf(args, "%80s%80s%1s", arg1, arg2, trash);

    /* If we have no automatic scaling for the range, we
       need both the start and the end to be specified. */
    if (scale < 0 && num_args < 2)
        return -1;

    /* With no arguments, use automatic scaling around the PC. */
    if (num_args < 1) {
        start = (REG(R_PC) + 0x10000 - scale) & 0xFFFF;
        end = (REG(R_PC) + scale) & 0xFFFF;
        goto success;
    }

    /* If the first argument is "more," start from the last stopping
       point.   Note that "more" also requires automatic scaling. */
    if (last_end >= 0 && strcasecmp(arg1, "more") == 0) {
        start = last_end;
        end = (start + 2 * scale) & 0xFFFF;
        if (num_args > 1)
            warn_too_many_args();
        goto success;
    }

    /* Parse the starting address. */
    if ((start = parse_address(arg1)) == -1)
        return -1;

    /* Scale to find the ending address if necessary. */
    if (num_args < 2) {
        end = (start + 2 * scale) & 0xFFFF;
        goto success;
    }

    /* Parse the ending address. */
    if ((end = parse_address(arg2)) == -1)
        return -1;

    /* For ranges, add 1 to specified ending address for inclusion 
       in output. */
    if (scale >= 0)
        end = (end + 1) & 0xFFFF;

    /* Check for superfluous arguments. */
    if (num_args > 2)
        warn_too_many_args();

    /* Store the results and return success. */ 
success:
    *startptr = start;
    *endptr = end;
    return 0;
}


// LC-3 memory access


int read_memory(int addr) {
    struct pollfd p;

    switch (addr) {
        case 0xFE00: /* KBSR */
            if (!last_KBSR_read) {
                p.fd = fileno(lc3in);
                p.events = POLLIN;
                /* Check if input is available */
                if (poll(&p, 1, 0) == 1 && (p.revents & POLLIN) != 0) {
                    /* Adds random delay if random-registers mode is enabled */
                    kbsr_waits = 0;
                    last_KBSR_read = (!rand_device || (random() & 15) == 0);
                } else {
                    /* No input available */
                    if (kbsr_waits < INT_MAX)
                        // Saturate to reduce CPU usage
                        kbsr_waits++;
                    /* Perhaps put a sleep here to reduce CPU usage? */
#ifdef LC3SIM_IDLE
                    if (kbsr_waits > 250) {
                        // We have checked enough for input that it likely
                        // won't be coming soon.
                        nanosleep(&idle_sleep, NULL);
                    }
#endif
                }
            }
            return (last_KBSR_read ? 0x8000 : 0x0000);
        case 0xFE02: /* KBDR */
            if (last_KBSR_read && (lc3_memory[0xFE02] = fgetc(lc3in)) == -1) {
                /* Should not happen in GUI mode. */
                /* FIXME: This won't show up correctly in GUI.
                   Exit is likely to be detected first, and error message
                   given (LC-3 sim. died), followed by message below 
                   (read past end), then Tcl/Tk error caused by bad
                   window access after sim died.  Confusing sequence
                   if it occurs. */
                if (gui_mode)
                    puts("ERR {LC-3 read past end of input stream.}");
                else
                    puts("LC-3 read past end of input stream.");
                exit(3);
            }
            last_KBSR_read = 0;
            return lc3_memory[0xFE02];
        case 0xFE04: /* DSR */
            if (!last_DSR_read) {
                /* Simulate hardware delay when random-registers mode is
                 * enabled. (Otherwise, it is always ready.)
                 */
                last_DSR_read = (!rand_device || (random() & 15) == 0);
            }
            return (last_DSR_read ? 0x8000 : 0x0000);
        case 0xFE06: /* DDR */
            return 0x0000;
        case 0xFFFE: return 0x8000;   /* MCR */
    }
    return lc3_memory[addr];
}

void write_memory(int addr, int value) {
    switch (addr) {
        case 0xFE00: /* KBSR */
        case 0xFE02: /* KBDR */
        case 0xFE04: /* DSR */
            return;
        case 0xFE06: /* DDR */
            if (last_DSR_read == 0)
                return;
            fprintf(lc3out, "%c", value);
            fflush(lc3out);
            last_DSR_read = 0;
            return;
        case 0xFFFE: /* MCR */
            if ((value & 0x8000) == 0)
                should_halt = true;
            return;
    }
    /* No need to write/update GUI if the same value is already in memory. */
    if (value != lc3_memory[addr]) {
        lc3_memory[addr] = value;
        if (gui_mode) {
            if (!delay_mem_update)
                disassemble_one(addr);
            else {
                lc3_show_later[addr] = true;
                have_mem_to_dump = true; /* a hint */
            }
        }
    }
}


// Code loading and initialization
// Note: I think this might have issues on big-endian systems.


static int read_obj_file(const char *filename, int *startp, int *endp) {
    FILE *f;
    int start, addr;
    unsigned char buf[2];

    if ((f = fopen(filename, "r")) == NULL)
        return -1;
    if (fread(buf, 2, 1, f) != 1) {
        fclose(f);
        return -1;
    }
    addr = start = (buf[0] << 8) | buf[1];
    while (fread(buf, 2, 1, f) == 1) {
        write_memory(addr, (buf[0] << 8) | buf[1]);
        addr = (addr + 1) & 0xFFFF;
    }
    fclose(f);
    squash_symbols(start, addr);
    *startp = start;
    *endp = addr;

    return 0;
}

static int read_sym_file(const char *filename) {
    FILE *f;
    int adding = 0;
    char buf[100];
    char sym[81];
    int addr;

    if ((f = fopen(filename, "r")) == NULL)
        return -1;
    while (fgets(buf, 100, f) != NULL) {
        if (!adding) {
            if (sscanf(buf, "%*s%*s%80s", sym) == 1 &&
                strcmp(sym, "------------") == 0)
                adding = 1;
            continue;
        }
        if (sscanf(buf, "%*s%80s%x", sym, &addr) != 2)
            break;
        add_symbol(sym, addr, 1);
    }
    fclose(f);
    return 0;
}

static int read_obj_mem(const unsigned char *in_mem,
                        size_t memsz,
                        int *startp,
                        int *endp) {
    int start, addr;
    unsigned char buf[2];

    buf[0] = in_mem[0];
    buf[1] = in_mem[1];

    addr = start = (buf[0] << 8) | buf[1];
    // First 2 bytes are starting address
    // Index 2 is first valid instruction
    for (int index = 2; index < memsz; index += 2) {
        buf[0] = in_mem[index];
        buf[1] = in_mem[index + 1];
        write_memory(addr, (buf[0] << 8) | buf[1]);
        addr = (addr + 1) & 0xFFFF;
    }
    squash_symbols(start, addr);
    *startp = start;
    *endp = addr;

    return 0;
}

static int read_sym_mem(const unsigned char *in_mem, size_t memsz) {
    int adding = 0;
    char buf[100];
    char sym[81];
    int addr;
    int read_size, index = 0;

    while (lc3s_getline(buf, 100,
                        (const char *)(in_mem + index),
                        memsz - index, &read_size) != NULL) {
        index += read_size;
        read_size = 0;
        if (!adding) {
            if (sscanf(buf, "%*s%*s%80s", sym) == 1 &&
                strcmp(sym, "------------") == 0)
                adding = 1;
            continue;
        }
        if (sscanf(buf, "%*s%80s%x", sym, &addr) != 2)
            break;
        add_symbol(sym, addr, 1);
    }
    return 0;
}


// Execute an instruction
// Return value is whether to continue running
static bool execute_instruction(void) {
    /* Fetch the instruction. */
    REG(R_IR) = read_memory(REG(R_PC));
    REG(R_PC) = (REG(R_PC) + 1) & 0xFFFF;

    /* Try to execute it. */

#define ADD_FLAGS(value) (last_flags |= (value))
#define DEF_INST(name,format,mask,match,flags,code) \
    if ((REG(R_IR) & (mask)) == (match)) {         \
        last_flags = (flags);                       \
        code;                                       \
        goto executed;                              \
    }
#define DEF_P_OP(name,format,mask,match)
#include "lc3.def"
#undef DEF_P_OP
#undef DEF_INST
#undef ADD_FLAGS

    // This runs if instruction was invalid. Otherwise, see "executed".
    REG(R_PC) = (REG(R_PC) - 1) & 0xFFFF;
    if (gui_mode)
        printf("ERR {Illegal instruction at x%04X!}\n", REG (R_PC));
    else
        printf("Illegal instruction at x%04X!\n", REG (R_PC));
    return false;

executed:
    /* Check for user breakpoints. */
    if (lc3_breakpoints[REG(R_PC)] == BPT_USER) {
        if (!gui_mode)
            printf("The LC-3 hit a breakpoint...\n");
        return false;
    }

    /* Check for system breakpoint (associated with "next" command). */
    if (REG(R_PC) == sys_bpt_addr)
        return false;

    if (finish_depth > 0) {
        if ((last_flags & FLG_SUBROUTINE) && 
            ++finish_depth == MAX_FINISH_DEPTH) {
            if (gui_mode)
                puts("ERR {Stopping due to possibly infinite "
                     "recursion.}");
            else
                puts("Stopping due to possibly infinite recursion.");
            finish_depth = 0;
            return false;
        } else if ((last_flags & FLG_RETURN) && --finish_depth == 0) {
            /* Done with finish command; stop execution. */
            return false;
        }
    }

    /* Check for GUI needs. */
    if (!in_init && gui_mode) {
        struct pollfd p;

        p.fd = fileno(sim_in);
        p.events = POLLIN;
        if (poll(&p, 1, 0) == 1 && (p.revents & POLLIN) != 0) {
            interrupted_at_gui_request = true;
            return false;
        }
    }

    return true;
}


// Disassembly utilities


// Used in disassembly
static void print_operands(int addr, int inst, format_t fmt) {
    bool found = false;
    int tgt;

    if (fmt & FMT_R1) {
        printf("%sR%d", (found ? "," : ""), F_DR(inst));
        found = 1;
    }
    if (fmt & FMT_R2) {
        printf("%sR%d", (found ? "," : ""), F_SR1(inst));
        found = 1;
    }
    if (fmt & FMT_R3) {
        printf("%sR%d", (found ? "," : ""), F_SR2(inst));
        found = 1;
    }
    if (fmt & FMT_IMM5) {
        printf("%s#%d", (found ? "," : ""), F_imm5(inst));
        found = 1;
    }
    if (fmt & FMT_IMM6) {
        printf("%s#%d", (found ? "," : ""), F_imm6(inst));
        found = 1;
    }
    if (fmt & FMT_VEC8) {
        printf("%sx%02X", (found ? "," : ""), F_vec8(inst));
        found = 1;
    }
    if (fmt & FMT_ASC8) {
        printf("%s", (found ? "," : ""));
        found = 1;
        switch (F_vec8(inst)) {
            case  7: printf("'\\a'"); break;
            case  8: printf("'\\b'"); break;
            case  9: printf("'\\t'"); break;
            case 10: printf("'\\n'"); break;
            case 11: printf("'\\v'"); break;
            case 12: printf("'\\f'"); break;
            case 13: printf("'\\r'"); break;
            case 27: printf("'\\e'"); break;
            case 34: printf("'\\\"'"); break;
            case 44: printf("'\\''"); break;
            case 92: printf("'\\\\'"); break;
            default:
                if (isprint(F_vec8(inst)))
                    printf("'%c'", F_vec8(inst));
                else
                    printf("x%02X", F_vec8(inst));
                break;
        }
    }
    if (fmt & FMT_IMM9) {
        printf("%s", (found ? "," : ""));
        found = 1;
        tgt = (addr + 1 + F_imm9(inst)) & 0xFFFF;
        if (lc3_sym_names[tgt] != NULL)
            printf("%s", lc3_sym_names[tgt]->name);
        else
            printf("x%04X", tgt);
    }
    if (fmt & FMT_IMM11) {
        printf("%s", (found ? "," : ""));
        found = 1;
        tgt = (addr + 1 + F_imm11(inst)) & 0xFFFF;
        if (lc3_sym_names[tgt] != NULL)
            printf("%s", lc3_sym_names[tgt]->name);
        else
            printf("x%04X", tgt);
    }
    if (fmt & FMT_IMM16) {
        printf("%s", (found ? "," : ""));
        found = 1;
        if (lc3_sym_names[inst] != NULL)
            printf("%s", lc3_sym_names[inst]->name);
        else
            printf("x%04X", inst);
    }
}

// Disassemble a single instruction
static void disassemble_one(int addr) {
    static const char* const dis_cc[8] = {
        "", "P", "Z", "ZP", "N", "NP", "NZ", "NZP"
    };
    int inst = read_memory(addr);

    /* GUI prefix */
    if (gui_mode)
        printf("CODE%c%5d",
               (!in_init && addr == lc3_register[R_PC] ? 'P' : ' '),
               addr + 1);

    /* Try to find a label. */
    if (lc3_sym_names[addr] != NULL)
        printf("%c %16.16s x%04X x%04X ",
               (lc3_breakpoints[addr] == BPT_USER ? 'B' : ' '),
               lc3_sym_names[addr]->name, addr, inst);
    else
        printf("%c %17sx%04X x%04X ", 
               (lc3_breakpoints[addr] == BPT_USER ? 'B' : ' '),
               "", addr, inst);

    /* Try to disassemble it. */

#define DEF_INST(name,format,mask,match,flags,code)                        \
    if ((inst & (mask)) == (match)) {                                      \
        if ((format) & FMT_CC)                                             \
            printf("%s%-*s", #name, (int)(OPCODE_WIDTH - strlen(#name)),   \
                   dis_cc[F_CC(inst) >> 9]);                               \
        else                                                               \
            printf("%-*s", OPCODE_WIDTH, #name);                           \
        print_operands(addr, inst, (format));                              \
        goto printed;                                                      \
    }
#define DEF_P_OP(name,format,mask,match) \
    DEF_INST(name,format,mask,match,FLG_NONE,{})
#include "lc3.def"
#undef DEF_P_OP
#undef DEF_INST

    // This executes if it is not a recognized instruction.
    printf("%-*s", OPCODE_WIDTH, "???");

printed:
    puts("");
}

// Disassemble a range of memory
static void disassemble(int addr_s, int addr_e) {
    do {
        disassemble_one(addr_s);
        addr_s = (addr_s + 1) & 0xFFFF;
    } while (addr_s != addr_e);
}


// State dumping


// Dumps register values
static void print_registers(void) {
    int regnum;

    if (!gui_mode) {
        printf("PC=x%04X IR=x%04X PSR=x%04X (%s)\n", REG(R_PC), REG(R_IR),
               REG(R_PSR), ccodes[(REG(R_PSR) >> 9) & 7]);
        for (regnum = 0; regnum < R_PC; regnum++)
            printf("R%d=x%04X ", regnum, REG(regnum));
        puts("");
        disassemble_one(REG(R_PC));
    } else {
        for (regnum = 0; regnum < NUM_REGS; regnum++)
            printf("REG R%d x%04X\n", regnum, REG(regnum));
        /* regnum is now NUM_REGS */
        printf("REG R%d %s\n", regnum, ccodes[(REG(R_PSR) >> 9) & 7]);
    }
}

// This sends memory updates when delayed memory dump was configured.
static void dump_delayed_mem_updates(void) {
    int addr;

    if (!have_mem_to_dump)
        return;
    have_mem_to_dump = false;

    /* FIXME: Could use a hash table here, but hint is probably enough. */
    for (addr = 0; addr < 65536; addr++) {
        if (lc3_show_later[addr]) {
            disassemble_one(addr);
            lc3_show_later[addr] = false;
        }
    }
}

// Prints state when not interrupted by GUI 
static void show_state_if_stop_visible(void) {
    /*
     * If the GUI has interrupted the simulator (e.g., to set or clear
     * a breakpoint), print nothing. The simulator restarts automatically
     * unless a new file is loaded, in which case cmd_file performs the
     * updates. 
     */
    if (interrupted_at_gui_request)
        return;

    if (gui_mode && delay_mem_update)
        dump_delayed_mem_updates();
    print_registers();
}

// Hex dump a range of memory
static void dump_memory(int addr_s, int addr_e) {
    int start, addr, i;
    int a[12];

    // Adjust for potential effects of parse_range()
    if (addr_s >= addr_e)
        addr_e += 0x10000;
    for (start = (addr_s / 12) * 12; start < addr_e; start += 12) {
        // Hex dump, with 12 words (24 bytes) per row
        printf("%04X: ", start & 0xFFFF);
        for (i = 0, addr = start; i < 12; i++, addr++) {
            // Display hex portion
            if (addr >= addr_s && addr < addr_e)
                printf("%04X ", (a[i] = read_memory(addr & 0xFFFF)));
            else
                // If address is out of range, print blanks
                printf("     ");
        }
        printf(" ");
        for (i = 0, addr = start; i < 12; i++, addr++) {
            // Display printable characters
            if (addr >= addr_s && addr < addr_e)
                printf("%c", (a[i] < 0x100 && isprint(a[i])) ? a[i] : '.');
            else
                printf(" ");
        }
        puts("");
    }
}


// Breakpoint management


// Clears breakpoint at specified LC-3 address
static void clear_breakpoint(int addr) {
    if (lc3_breakpoints[addr] != BPT_USER) {
        if (!gui_mode)
            printf("No such breakpoint was set.\n");
    } else {
        if (gui_mode)
            printf("BCLEAR %d\n", addr + 1);
        else
            printf("Cleared breakpoint at x%04X.\n", addr);
    }
    lc3_breakpoints[addr] = BPT_NONE;
}

// Clears all breakpoints
static void clear_all_breakpoints(void) {
    /*
     * If other breakpoint types are to be supported,
     * this code needs to avoid clobbering non-user
     * breakpoints.
     */
    memset(lc3_breakpoints, 0, sizeof(lc3_breakpoints));
}

// Print user-set breakpoints on console
static void list_breakpoints(void) {
    bool found = false;

    /* A bit hokey, but no big deal for this few. */
    for (int i = 0; i < 65536; i++) {
        if (lc3_breakpoints[i] == BPT_USER) {
            if (!found) {
                printf("The following instructions are set as "
                       "breakpoints:\n");
                found = true;
            }
            disassemble_one(i);
        }
    }

    if (!found)
        printf("No breakpoints are set.\n");
}

// Set user breakpoint on LC-3 address
static void set_breakpoint(int addr) {
    if (lc3_breakpoints[addr] == BPT_USER) {
        if (!gui_mode)
            printf("That breakpoint is already set.\n");
    } else {
        lc3_breakpoints[addr] = BPT_USER;
        if (gui_mode)
            printf("BREAK %d\n", addr + 1);
        else
            printf("Set breakpoint at x%04X.\n", addr);
    }
}


// Program loops


// The simulator's command loop
static void command_loop(void) {
    int cword_len;
    char *cmd = NULL;
    char *start;
    char *last_cmd = NULL;
    char cword[MAX_CMD_WORD_LEN];
    const command_t *a_command;

    while (!stop_scripts && (cmd = lc3readline("(lc3sim) ")) != NULL) {
        /* Skip white space. */
        for (start = cmd; isspace(*start); start++);
        if (*start == '\0') {
            /* An empty line repeats the last command, if allowed. */
            free(cmd);
            if ((cmd = last_cmd) == NULL)
                continue;
            /* Skip white space. */
            for (start = cmd; isspace(*start); start++);
        } else if (last_cmd != NULL)
            free(last_cmd);
        last_cmd = NULL;

        /* Should never fail; just ignore the command if it does. */
        /* 40 below == MAX_CMD_WORD_LEN - 1 */
        if (sscanf(start, "%40s", cword) != 1) {
            free(cmd);
            break;
        }

        /* Record command word length, then point to arguments. */
        cword_len = strlen(cword);
        for (start += cword_len; isspace(*start); start++);

        /* Match command word to list of commands. */
        a_command = command;
        while (1) {
            if (a_command->command == NULL) {
                /* No match found--complain! */
                free(cmd);
                printf("Unknown command.  Type 'h' for help.\n");
                break;
            }

            /* Try to match a_command. */
            if (strncasecmp(cword, a_command->command, cword_len) == 0 &&
                cword_len >= a_command->min_len &&
                (gui_mode || (a_command->flags & CMD_FLAG_GUI_ONLY) == 0)) {

                /* Execute the command. */
                (*a_command->cmd_func)(start);

                /* Handle list type and repeatable commands. */
                if (a_command->flags & CMD_FLAG_LIST_TYPE) {
                    char buf[MAX_CMD_WORD_LEN + 5];

                    strcpy(buf, cword);
                    strcat(buf, " more");
                    last_cmd = strdup(buf);
                } else if (a_command->flags & CMD_FLAG_REPEATABLE &&
                           script_depth == 0) {
                    last_cmd = cmd;
                } else {
                    free(cmd);
                }
                break;
            }
            a_command++;
        }
    }
}

// Runs LC-3 until stopping condition occurs (like breakpoint, halt, error).
static void run_until_stopped(void) {
    struct termios tio;
    int old_lflag, old_min, old_time;
    bool tty_fail;

    should_halt = false;
    if (gui_mode) {
        /* removes PC marker in GUI */
        printf("CONT\n");
        tty_fail = true;
    } else if (!isatty(fileno(lc3in)) || 
               tcgetattr(fileno(lc3in), &tio) != 0)
        tty_fail = true;
    else {
        tty_fail = false;
        // Store the old console state
        old_lflag = tio.c_lflag;
        old_min = tio.c_cc[VMIN];
        old_time = tio.c_cc[VTIME];
        // Prepare console for LC-3's character-based mode
        // Disable line-based reading and character echoing
        tio.c_lflag &= ~(ICANON | ECHO);
        // Minimum read size is 1
        tio.c_cc[VMIN] = 1;
        // Read timeout is 0
        tio.c_cc[VTIME] = 0;
        (void)tcsetattr(fileno(lc3in), TCSANOW, &tio);
    }

    while (!should_halt && execute_instruction());

    if (!tty_fail) {
        // Restore console state after LC-3 finishes
        tio.c_lflag = old_lflag;
        tio.c_cc[VMIN] = old_min;
        tio.c_cc[VTIME] = old_time;
        (void)tcsetattr(fileno(lc3in), TCSANOW, &tio);
        /*
         * Discard any remaining input if requested.  This flush occurs
         * when the LC-3 stops, in which case any remaining input
         * to the console will be treated as simulator commands if it
         * is not discarded.
         *
         * However, discarding can interfere with command sequences that
         * include moderately long execution periods.
         *
         * As with gdb, not discarding is the default, since typing in
         * a bunch of random junk that happens to look like valid
         * commands happens less frequently than the case above, although
         * I myself have been bitten a few times in gdb by pressing
         * return once too often after issuing a repeatable command.
         */
        if (!keep_input_on_stop)
            (void)tcflush(fileno(lc3in), TCIFLUSH);
    }

    /* stopped by CTRL-C?  Check if we need a stop notice... */
    if (need_a_stop_notice) {
        printf("\nLC-3 stopped.\n\n");
        need_a_stop_notice = false;
    }

    /* 
     * If stopped for any reason other than interruption by GUI,
     * clear system breakpoint and terminate any "finish" command.
     */
    if (!interrupted_at_gui_request) {
        sys_bpt_addr = -1;
        finish_depth = 0;
    }

    /* Dump memory and registers if necessary. */
    show_state_if_stop_visible();
}


// Resets LC-3
static void init_machine(void) {
    int os_start, os_end;

    in_init = true;

    memset(lc3_register, 0, sizeof(lc3_register));
    REG(R_PSR) = (2L << 9); /* set to condition ZERO */
    memset(lc3_memory, 0, sizeof(lc3_memory));
    memset(lc3_show_later, 0, sizeof(lc3_show_later));
    memset(lc3_sym_names, 0, sizeof(lc3_sym_names));
    memset(lc3_sym_hash, 0, sizeof(lc3_sym_hash));
    clear_all_breakpoints();

#ifdef LC3SIM_INCBIN
    // Data is built into binary, so it can be position-independent
    if (read_obj_mem(lc3os_obj, lc3os_obj_len, &os_start, &os_end) == -1) {
        if (gui_mode)
            puts("ERR {Failed to read LC-3 OS code.}");
        else
            puts("Failed to read LC-3 OS code.");
        show_state_if_stop_visible();
    } else {
        if (read_sym_mem(lc3os_sym, lc3os_sym_len) == -1) {
            if (gui_mode)
                puts("ERR {Failed to read LC-3 OS symbols.}");
            else
                puts("Failed to read LC-3 OS symbols.");
        }
        if (gui_mode) /* load new code into GUI display */
            disassemble(os_start, os_end);
        REG(R_PC) = 0x0200;
        run_until_stopped();
    }
#else
    if (read_obj_file(INSTALL_DIR "/lc3os.obj", &os_start, &os_end) == -1) {
        if (gui_mode)
            puts("ERR {Failed to read LC-3 OS code.}");
        else
            puts("Failed to read LC-3 OS code.");
        show_state_if_stop_visible();
    } else {
        if (read_sym_file(INSTALL_DIR "/lc3os.sym") == -1) {
            if (gui_mode)
                puts("ERR {Failed to read LC-3 OS symbols.}");
            else
                puts("Failed to read LC-3 OS symbols.");
        }
        if (gui_mode) /* load new code into GUI display */
            disassemble(os_start, os_end);
        REG(R_PC) = 0x0200;
        run_until_stopped();
    }
#endif

    in_init = false;

    if (start_script != NULL)
        cmd_execute(start_script);
    else if (start_file != NULL)
        cmd_file(start_file);
}


// GUI-specific functions


// Only called in GUI mode: prints value of a register to GUI
static void print_register(int which) {
    printf("REG R%d x%04X\n", which, REG (which));
    /* condition codes are not stored outside of PSR */
    if (which == R_PSR)
        printf("REG R%d %s\n", NUM_REGS, ccodes[(REG(R_PSR) >> 9) & 7]);
    /* change focus in GUI */
    printf("TOCODE\n");
}

// Accepts a port on stdin, and connects to localhost:port to communicate with GUI
static int launch_gui_connection(void) {
    unsigned short port;
    // fd for connection to server
    int fd;
    // Address for server connection
    struct sockaddr_in addr;

    /* wait for the GUI to tell us the port for the LC-3 console socket */
    if (fscanf(sim_in, "%hd", &port) != 1)
        return -1;

    /* don't buffer output to GUI */
    if (setvbuf(stdout, NULL, _IONBF, 0) == -1)
        return -1;

    /* create a TCP socket */
    if ((fd = socket(PF_INET, SOCK_STREAM, 0)) == -1)
        return -1;

    /* Set up parameters */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    /* Connect to the given port */
    if (connect(fd, (struct sockaddr*)&addr,
                sizeof(struct sockaddr_in)) == -1) {
        close(fd);
        return -1;
    }

    /* use it for LC-3 keyboard and display I/O */
    if ((lc3in = fdopen(fd, "r")) == NULL ||
        (lc3out = fdopen(fd, "w")) == NULL ||
        setvbuf(lc3out, NULL, _IONBF, 0) == -1) {
        close(fd);
        return -1;
    }

    return 0;
}

// Dump state for the GUI.
static void gui_stop_and_dump(void) {
    /* Do not restart simulator automatically. */
    interrupted_at_gui_request = false;

    /* Clear any breakpoint from an executing "next" command. */
    sys_bpt_addr = -1;

    /* Clear any "finish" command state. */
    finish_depth = 0;

    /* Tell the GUI about any changes to memory or registers. */
    dump_delayed_mem_updates();
    print_registers();
}


// Implementation for simulator commands


// The "help" command
static void cmd_help(const char *args) {
    printf("file <file>           -- file load (also sets PC to start of "
           "file)\n\n");

    printf("break ...             -- breakpoint management\n\n");

    printf("continue              -- continue execution\n");
    printf("finish                -- execute to end of current subroutine\n");
    printf("next                  -- execute next instruction (full "
           "subroutine/trap)\n");
    printf("step                  -- execute one step (into "
           "subroutine/trap)\n\n");

    printf("list ...              -- list instructions at the PC, an "
           "address, a label\n");
    printf("dump ...              -- dump memory at the PC, an address, "
           "a label\n");
    printf("translate <addr>      -- show the value of a label and print the "
           "contents\n");
    printf("printregs             -- print registers and current "
           "instruction\n\n");

    printf("memory <addr> <val>   -- set the value held in a memory "
           "location\n");
    printf("register <reg> <val>  -- set a register to a value\n\n");


    printf("execute <file name>   -- execute a script file\n\n");

    printf("reset                 -- reset LC-3 and reload last file\n\n");

    printf("quit                  -- quit the simulator\n\n");

    printf("help                  -- print this help\n\n");

    printf("All commands except quit can be abbreviated.\n");
}

// The "break" command (manages breakpoints)
static void cmd_break(const char *args) {
    char opt[11], addr_str[MAX_LABEL_LEN], trash[2];
    int num_args, opt_len, addr;

    /* 80 == MAX_LABEL_LEN - 1 */
    num_args = sscanf(args, "%10s%80s%1s", opt, addr_str, trash);

    if (num_args > 0) {
        opt_len = strlen(opt);
        if (strncasecmp(opt, "list", opt_len) == 0) {
            if (num_args > 1)
                warn_too_many_args();
            list_breakpoints();
            return;
        }
        if (num_args > 1) {
            if (num_args > 2)
                warn_too_many_args();
            addr = parse_address(addr_str);
            if (strncasecmp(opt, "clear", opt_len) == 0) {
                if (strcasecmp(addr_str, "all") == 0) {
                    clear_all_breakpoints();
                    if (!gui_mode)
                        printf("Cleared all breakpoints.\n");
                    return;
                }
                if (addr != -1)
                    clear_breakpoint(addr);
                else
                    puts(BAD_ADDRESS);
                return;
            } else if (strncasecmp(opt, "set", opt_len) == 0) {
                if (addr != -1)
                    set_breakpoint(addr);
                else
                    puts(BAD_ADDRESS);
                return;
            }
        }
    }

    // Print help for command
    printf("breakpoint options include:\n");
    printf("  break clear <addr>|all -- clear one or all breakpoints\n");
    printf("  break list             -- list all breakpoints\n");
    printf("  break set <addr>       -- set a breakpoint\n");
}

// The "continue" command
static void cmd_continue(const char *args) {
    no_args_allowed(args);
    if (interrupted_at_gui_request)
        interrupted_at_gui_request = false;
    else
        flush_console_input();
    run_until_stopped();
}

// The "dump" command (perform hex dump)
static void cmd_dump(const char *args) {
    static int last_end = 0;
    int start, end;

    if (parse_range(args, &start, &end, last_end, 48) == 0) {
        dump_memory(start, end);
        last_end = end;
        return;
    }

    // Print help for command
    printf("dump options include:\n");
    printf("  dump               -- dump memory around PC\n");
    printf("  dump <addr>        -- dump memory starting from an "
           "address or label\n");
    printf("  dump <addr> <addr> -- dump a range of memory\n");
    printf("  dump more          -- continue previous dump (or press "
           "<Enter>)\n");
}

// The "execute" command (run a simulator script)
static void cmd_execute(const char *args) {
    FILE *previous_input;
    FILE *script;

    if (script_depth == MAX_SCRIPT_DEPTH) {
        /* Safer to exit than to bury a warning arbitrarily deep. */
        printf("Cannot execute more than %d levels of scripts!\n",
               MAX_SCRIPT_DEPTH);
        stop_scripts = true;
        return;
    }

    if ((script = fopen(args, "r")) == NULL) {
        printf("Cannot open script file \"%s\".\n", args);
        stop_scripts = true;
        return;
    }

    script_depth++;
    previous_input = sim_in;
    sim_in = script;
    if (!script_uses_stdin)
        lc3in = script;
#if defined(USE_READLINE)
    lc3readline = simple_readline;
#endif
    command_loop();
    sim_in = previous_input;
    if (--script_depth == 0) {
        if (gui_mode) {
            lc3in = lc3out;
        } else {
            lc3in = stdin;
#if defined(USE_READLINE)
            lc3readline = readline;
#endif
        }
        stop_scripts = false;
    } else if (!script_uses_stdin) {
        /* executing previous script level--take LC-3 console input 
           from script */
        lc3in = previous_input;
    }
    fclose(script);
}

// The "file" command (load an LC-3 object file)
static void cmd_file(const char *args) {
    /* extra 4 chars in buf for ".obj" possibly added later */ 
    char buf[MAX_FILE_NAME_LEN + 4];
    char *ext;
    int len, start, end;
    bool warn = false;

    len = strlen(args);
    if (len == 0 || len > MAX_FILE_NAME_LEN - 1) {
        if (gui_mode)
            printf("ERR {Could not parse file name!}\n");
        else
            printf("syntax: file <file to load>\n");
        return;
    }
    strcpy(buf, args);
    /* FIXME: Need to use portable path element separator characters
       rather than assuming use of '/'. */
    if ((ext = strrchr(buf, '.')) == NULL || strchr(ext, '/') != NULL) {
        ext = buf + len;
        strcat(buf, ".obj");
    } else {
        if (!gui_mode && strcasecmp(ext, ".sym") == 0) {
            if (read_sym_file(buf))
                printf("Failed to read symbols from \"%s.\"\n", buf);
            else
                printf("Read symbols from \"%s.\"\n", buf);
            return;
        }
        if (strcasecmp(ext, ".obj") != 0) {
            if (gui_mode)
                printf("ERR {Only .obj files can be loaded.}\n");
            else
                printf("Only .obj or .sym files can be loaded.\n");
            return;
        }
    }
    if (read_obj_file(buf, &start, &end) == -1) {
        if (gui_mode)
            printf("ERR {Failed to load \"%s.\"}\n", buf);
        else
            printf("Failed to load \"%s.\"\n", buf);
        return;
    }
    /* Success: reload same file next time machine is reset. */
    if (start_file != NULL)
        free(start_file);
    start_file = strdup(buf);

    strcpy(ext, ".sym");
    if (read_sym_file(buf))
        warn = true;
    REG(R_PC) = start;

    /* GUI requires printing of new PC to reorient code display to line */
    if (gui_mode) {
        /* load new code into GUI display */
        disassemble(start, end);
        /* change focus in GUI */
        printf("TOCODE\n");
        print_register(R_PC);
        if (warn)
            printf("ERR {WARNING: No symbols are available.}\n");
    } else {
        strcpy(ext, ".obj");
        printf("Loaded \"%s\" and set PC to x%04X\n", buf, start);
        if (warn)
            printf("WARNING: No symbols are available.\n");
    }

    /* Should not immediately start, even if we stopped simulator to
     * load file.  We do need to update registers and dump delayed
     * memory changes in that case, though.  Similarly, loading a
     * file forces the simulator to forget completion of an executing
     * "next" command.
     */
    if (interrupted_at_gui_request)
        gui_stop_and_dump();
}

// The "finish" command (run to end of current subroutine)
static void cmd_finish(const char *args) {
    no_args_allowed(args);
    flush_console_input();
    finish_depth = 1;
    run_until_stopped();
}

// The "list" command (lists instructions in specified areas of memory)
static void cmd_list(const char *args) {
    static int last_end = 0;
    int start, end;

    if (parse_range(args, &start, &end, last_end, 10) == 0) {
        disassemble(start, end);
        last_end = end;
        return;
    }

    printf("list options include:\n");
    printf("  list               -- list instructions around PC\n");
    printf("  list <addr>        -- list instructions starting from an "
           "address or label\n");
    printf("  list <addr> <addr> -- list a range of instructions\n");
    printf("  list more          -- continue previous listing (or press "
           "<Enter>)\n");
}

// The "memory" command (writes data in regions of memory)
static void cmd_memory(const char *args) {
    int addr, value;

    if (parse_range(args, &addr, &value, -1, -1) == 0) {
        write_memory(addr, value);
        if (gui_mode) {
            printf("TRANS x%04X x%04X\n", addr, value);
            disassemble_one(addr);
        } else
            printf("Wrote x%04X to address x%04X.\n", value, addr);
    } else {
        if (gui_mode) {
            /* Address is provided by the GUI, so only the value can
               be bad in this case. */
            printf("ERR {No address or label corresponding to the "
                   "desired value exists.}\n");
        } else
            printf("syntax: memory <addr> <value>\n");
    }
}

// The "option" command (changes simulator settings)
static void cmd_option(const char *args) {
    char opt[11], onoff[6], trash[2];
    int num_args, opt_len;
    bool oval;

    num_args = sscanf(args, "%10s%5s%1s", opt, onoff, trash);
    if (num_args >= 2) {
        opt_len = strlen(opt);
        if (strcasecmp(onoff, "on") == 0)
            oval = true;
        else if (strcasecmp(onoff, "off") == 0)
            oval = false;
        else
            goto show_syntax;
        if (num_args > 2)
            warn_too_many_args();
        if (strncasecmp(opt, "flush", opt_len) == 0) {
            flush_on_start = oval;
            if (!gui_mode)
                printf("Will %sflush the console input when starting.\n",
                       oval ? "" : "not ");
            return;
        }
        if (strncasecmp(opt, "keep", opt_len) == 0) {
            keep_input_on_stop = oval;
            if (!gui_mode)
                printf("Will %skeep remaining input when the LC-3 stops.\n", 
                       oval ? "" : "not ");
            return;
        }
        if (strncasecmp(opt, "device", opt_len) == 0) {
            rand_device = oval;
            if (!gui_mode)
                printf("Will %srandomize device interactions.\n",
                       oval ? "" : "not ");
            return;
        }
        /* GUI-only option: Delay memory updates to GUI until LC-3 stops? */
        if (gui_mode && strncasecmp(opt, "delay", opt_len) == 0) {
            /* Make sure that if the option is turned off while the GUI
               thinks that the processor is running, state is dumped
               immediately. */
            if (delay_mem_update && oval == false)
                dump_delayed_mem_updates();
            delay_mem_update = oval;
            return;
        }
        /* Use stdin for LC-3 console input while running script? */
        if (strncasecmp(opt, "stdin", opt_len) == 0) {
            script_uses_stdin = oval;
            if (!gui_mode)
                printf("Will %suse stdin for LC-3 console input during "
                       "script execution.\n", oval ? "" : "not ");
            if (script_depth > 0) {
                if (!oval)
                    lc3in = sim_in;
                else if (!gui_mode)
                    lc3in = stdin;
                else
                    lc3in = lc3out;
            }
            return;
        }
    }

show_syntax:
    printf("syntax: option <option> on|off\n   options include:\n");
    printf("      device -- simulate random device (keyboard/display)"
           "timing\n");
    printf("      flush  -- flush console input each time LC-3 starts\n");
    printf("      keep   -- keep remaining input when the LC-3 stops\n");
    printf("      stdin  -- use stdin for LC-3 console input during script "
           "execution\n");
    printf("NOTE: all options are ON by default\n");
}

// The "next" instruction (execute 1 LC-3 instruction)
static void cmd_next(const char *args) {
    int next_pc = (REG(R_PC) + 1) & 0xFFFF;

    no_args_allowed(args);
    flush_console_input();

    /* Note that we might hit a breakpoint immediately. */
    if (execute_instruction()) {
        if ((last_flags & FLG_SUBROUTINE) != 0) {
            /*
             * Mark system breakpoint. This approach allows the GUI
             * to interrupt the simulator without the simulator
             * forgetting about the completion of this command (i.e.,
             * next). Nesting of such commands is not supported,
             * and should not be possible to issue with the GUI.
             */
            sys_bpt_addr = next_pc;
            run_until_stopped();
            return;
        }
    }

    /* Dump memory and registers if necessary. */
    show_state_if_stop_visible();
}

// The "printregs" command
static void cmd_printregs(const char *args) {
    no_args_allowed(args);
    print_registers();
}

// The "quit" command
static void cmd_quit(const char *args) {
    no_args_allowed(args);
    exit(0);
}

// The "register" command
static void cmd_register(const char *args) {
    static const char * const rname[NUM_REGS + 1] = {
        "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",
        "PC", "IR", "PSR", "CC"
    };
    static const char * const cc_val[4] = {
        "POSITIVE", "ZERO", "", "NEGATIVE"
    };
    char arg1[MAX_LABEL_LEN], arg2[MAX_LABEL_LEN], trash[2];
    int num_args, rnum, value, len;

    /* 80 == MAX_LABEL_LEN - 1 */
    num_args = sscanf(args, "%80s%80s%1s", arg1, arg2, trash);
    if (num_args < 2) {
        /* should never happen in GUI mode */
        printf("syntax: register <reg> <value>\n");
        return;
    }

    /* Determine which register is to be set. */
    for (rnum = 0; ; rnum++) {
        if (rnum == NUM_REGS + 1) {
            /* No match (should never happen in GUI mode). */
            puts("Registers are R0...R7, PC, IR, PSR, and CC.");
            return;
        }
        if (strcasecmp(rname[rnum], arg1) == 0)
            break;
    }

    /* Condition codes are a special case. */
    if (rnum == NUM_REGS) {
        len = strlen(arg2);
        for (value = 0; value < 4; value++) {
            if (strncasecmp(arg2, cc_val[value], len) == 0) {
                REG(R_PSR) &= ~0x0E00;
                REG(R_PSR) |= ((value + 1) << 9);
                if (gui_mode)
                    /* printing PSR prints both PSR and CC */
                    print_register(R_PSR);
                else
                    printf("Set CC to %s.\n", cc_val[value]);
                return;
            }
        }
        if (gui_mode)
            printf("ERR {CC can only be set to NEGATIVE, ZERO, or "
                   "POSITIVE.}\n");
        else
            printf("CC can only be set to NEGATIVE, ZERO, or "
                   "POSITIVE.\n");
        return;
    }

    /* Parse the value and set the register, or complain if it's a bad
       value. */
    if ((value = parse_address(arg2)) != -1) {
        REG(rnum) = value;
        if (gui_mode)
            print_register(rnum);
        else
            printf("Set %s to x%04X.\n", rname[rnum], value);
    } else if (gui_mode)
        printf("ERR {No address or label corresponding to the "
               "desired value exists.}\n");
    else
        puts("No address or label corresponding to the "
             "desired value exists.");
}

// The "reset" command
static void cmd_reset(const char *args) {
    int addr;

    if (script_depth > 0) {
        /* Should never be executing a script in GUI mode, but check... */
        if (!gui_mode)
            puts("Cannot reset the LC-3 from within a script.");
        else
            puts("ERR {Cannot reset the LC-3 from within a script.}");
        return;
    }
    no_args_allowed(args);

    /*
     * If in GUI mode, we need to write over all memory with zeroes
     * rather than just setting (so that disassembly info gets sent
     * to GUI).
     */
    if (gui_mode) {
        interrupted_at_gui_request = false;
        for (addr = 0; addr < 65536; addr++)
            write_memory(addr, 0);
        gui_stop_and_dump();
    }

    /* various bits of state to reset */
    last_KBSR_read = 0;
    last_DSR_read = 0;
    have_mem_to_dump = false;
    need_a_stop_notice = false;
    sys_bpt_addr = -1;
    finish_depth = 0;

    init_machine();

    /* change focus in GUI, and turn off delay cursor */
    if (gui_mode)
        printf("TOCODE\n");
}

// The "step" command ("step into" an instruction like a debugger)
static void cmd_step(const char *args) {
    no_args_allowed(args);
    flush_console_input();
    execute_instruction();
    /* Dump memory and registers if necessary. */
    show_state_if_stop_visible();
}

// The "translate" command (read value at memory address)
static void cmd_translate(const char *args) {
    char arg1[81], trash[2];
    int num_args, value;

    /* 80 == MAX_LABEL_LEN - 1 */
    if ((num_args = sscanf(args, "%80s%1s", arg1, trash)) > 1)
        warn_too_many_args();

    if (num_args < 1) {
        puts("syntax: translate <addr>");
        return;
    }

    /* Try to translate the value. */
    if ((value = parse_address(arg1)) == -1) {
        if (gui_mode)
            printf("ERR {No such address or label exists.}\n");
        else
            puts(BAD_ADDRESS);
        return;
    }

    if (gui_mode)
        printf("TRANS x%04X x%04X\n", value, read_memory(value));
    else
        printf("Address x%04X has value x%04x.\n", value,
               read_memory(value));
}

// The GUI's "stop" command
static void cmd_lc3_stop(const char *args) {
    /* GUI only, so no need to warn about args. */
    /* Stop execution and dump state. */
    gui_stop_and_dump();
}


// The main program


int main(int argc, char **argv) {
    /* check for -gui argument */
    sim_in = stdin;
    if (argc > 1 && strcmp (argv[1], "-gui") == 0) {
        if (launch_gui_connection() != 0) {
            printf("failed to connect to GUI\n");
            return 1;
        }
        /* skip the -gui argument in later parsing */
        argc--;
        argv++;
        gui_mode = 1;
    } else {
        lc3out = stdout;
        lc3in = stdin;
        gui_mode = 0;
#ifdef USE_READLINE
        lc3readline = readline;
#endif
    }

    /* used to simulate random device timing behavior */
    srandom(time(NULL));

    /* used to halt LC-3 when CTRL-C pressed */
    signal(SIGINT, halt_lc3);

    /* load any object, symbol, or script files requested on command line */
    if (argc == 3 && strcmp(argv[1], "-s") == 0) {
        start_script = argv[2];
        init_machine(); /* also executes script */
        return 0;
    } else if (argc == 2 && strcmp(argv[1], "-h") != 0) {
        start_file = strdup(argv[1]);
        init_machine(); /* also loads file */
    } else if (argc != 1) {
        /* argv[0] may not be valid if -gui entered */
        printf("syntax: lc3sim [<object file>|<symbol file>]\n");
        printf("        lc3sim [-s <script file>]\n");
        printf("        lc3sim -h\n");
        return 0;
    } else
        init_machine();

    command_loop();

    puts("");
    return 0;
}
