// This program is meant to work similarly to `xxd -i`.
/* Example output:
 * unsigned const char lc3os_obj[] = {
 *   0x00, 0x00, 0x04, 0x95, 0x04, 0x95, 0x04, 0x95, 0x04, 0x95, 0x04, 0x95,
 *   0x04, 0x95, 0x04, 0x95, 0x04, 0x95, 0x04, 0x95, 0x04, 0x95, 0x04, 0x95,
 *   0x04, 0x95, 0x04, 0x95, 0x04, 0x95, 0x04, 0x95, 0x04, 0x95, 0x04, 0x95,
 *   ...
 * };
 * unsigned const int lc3os_obj_len = 2532;
 */
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

// Constants
const char data_prefix[] = "unsigned const char ";
// To know how large it is (not including the '\0' byte in size)
const size_t data_prefix_len = sizeof(data_prefix) - 1;

const char data_len_prefix[] = "unsigned const int ";
const size_t data_len_prefix_len = sizeof(data_len_prefix) - 1;

// Where the text file is read in
char inbuf[4096];
// Size of the read string
size_t read_size;
size_t file_size;
bool file_fully_read = false;
// I buffer the output here.
char outbuf[4096];
// Size of the write (to check for validity)
size_t write_size;

char *infile_path = NULL;
size_t infile_path_len = 0;
FILE *infile;
char *outfile_path = NULL;
size_t outfile_path_len = 0;
FILE *outfile;

// This string is variable-translated name of input file
char *infile_var_name = NULL;
size_t infile_var_name_len = 0;

void usage(void) {
    fprintf(stderr, "Usage: generate-header INFILE OUTFILE\n");
}

static void generate_infile_var_name(void) {
    int start_index = 0;
    for (int index = 0; index <= infile_path_len; index++) {
        if (infile_path[index] == '/') {
            // Path separator (on all major OSes)
            start_index = index + 1;
        }
#ifdef _WIN32
        else if (infile_path[index] == '\\') {
            // Path separator on Windows
            start_index = index + 1;
        }
#endif
    }
    strcpy(infile_var_name, infile_path + start_index);
    int end_index = infile_path_len - start_index;

    for (int index = 0; index < end_index; index++) {
        char test_char = infile_var_name[index];
        // Test for allowed ASCII characters
        if ((test_char >= 0x30 && test_char <= 0x39) /* 0-9 */
            || (test_char >= 0x41 && test_char <= 0x5a) /* A-Z */
            || (test_char >= 0x61 && test_char <= 0x7a) /* a-z */
            || test_char == '_') {
            // Acceptable character
            ;
        } else if (test_char == '\0') {
            // What happened here?
            fprintf(stderr, "Bounds exceeded at index %i, end_index = %i\n", index, end_index);
        } else {
            // Not matching, correct it
            infile_var_name[index] = '_';
        }
    }
    infile_var_name_len = end_index;
}


int main(int argl, char **args) {
    // generate-header INFILE OUTFILE
    if (argl < 3) {
        fprintf(stderr, "Not enough arguments specified.\n");
    }

    infile_path = args[1];
    outfile_path = args[2];

    // Determine path sizes for convenience ('\0' byte not included in count)
    infile_path_len = strlen(infile_path);
    outfile_path_len = strlen(outfile_path);

    infile = fopen(infile_path, "r");
    if (!infile) {
        perror("Failed to open input file");
        return 2;
    }
    outfile = fopen(outfile_path, "w");
    if (!infile) {
        perror("Failed to open output file");
        return 2;
    }

    strcpy(outbuf, data_prefix);

    // Can hold filename's length
    infile_var_name = calloc(infile_path_len + 1, 1);

    generate_infile_var_name();

//    fprintf(stderr, "Variable name is \"%s\"\n", infile_var_name);

    strcat(outbuf, infile_var_name);

    strcat(outbuf, "[] = {\n");

    write_size = fwrite(outbuf, 1, data_prefix_len + infile_var_name_len + 7, outfile);

    write_size = 0;
    memset(outbuf, 0, data_prefix_len + infile_var_name_len + 7);

    int outbuf_index = 0;
    // I print 12 bytes per line.
    int outbuf_byte_count = 12;

//    return 0;

    // Note: I use strcpy() here to avoid penalty of determining string size.
    do {
        // fread(void *buf, size_t unit_size, size_t array_size, FILE *file)
        // -> number of units read (bytes if unit_size = 1)
        read_size = fread(inbuf, 1, 4096, infile);
        file_size += read_size;

//        fprintf(stderr, "Read in %zi bytes, outbuf index = %i\n", read_size, outbuf_index);

        // Note: This is to avoid off-by-one errors.
        for (int index = 0; index < read_size; index++) {
            if (outbuf_byte_count == 12) {
                strcpy(outbuf + outbuf_index, "  ");
                outbuf_index += 2;
            }
            if (outbuf_index + 6 > 4096) {
                // Not enough room to fit in in the buffer.
                write_size = fwrite(outbuf, 1, outbuf_index, outfile);
//                fprintf(stderr, "Flushed %zi bytes\n", write_size);
                memset(outbuf, 0, outbuf_index);
                outbuf_index = 0;
            }
            int stringsize = 0;
            if (inbuf[index] != '\0') {
                // It may vary in size. I need 6 bytes to include '\0' terminator.
                // The 04 size specifier makes it fixed-length.
                stringsize = snprintf(outbuf + outbuf_index, 6, "%#04hhx,", inbuf[index]);
            } else {
                // It just prints "0" when input is 0. Work around it.
                stringsize = 5;
                strcpy(outbuf + outbuf_index, "0x00,");
            }
//            fprintf(stderr, "Integer wrote %i bytes\n", stringsize);
            outbuf_index += stringsize;
            outbuf_byte_count--;
            if (outbuf_byte_count == 0) {
                // Wrap to newline
                outbuf[outbuf_index] = '\n';
                outbuf_index++;
                outbuf_byte_count = 12;
            } else {
                // Add space
                outbuf[outbuf_index] = ' ';
                outbuf_index++;
            }
        }
        // Now the file block is read, read in some more in the next iteration..

        // Clear out old data.
        memset(inbuf, 0, read_size);

        // Returns nonzero if EOF
        file_fully_read = feof(infile);
    } while (!file_fully_read);

    // Flush outbuf
    if (outbuf_index > 0) {
        outbuf_index--;
        // Replace comma with a newline to be compatible with xxd header dump.
        outbuf[outbuf_index - 1] = '\n';
        write_size = fwrite(outbuf, 1, outbuf_index, outfile);
//        fprintf(stderr, "Flushed %zi bytes\n", write_size);
    }

    // Now write out footer and other information.
    // inbuf should be cleared.
    outbuf_index = 0;
    strcpy(outbuf, "};\n");
    strcat(outbuf, data_len_prefix);
    strcat(outbuf, infile_var_name);
    strcat(outbuf, "_len = ");
    outbuf_index = 3 + data_len_prefix_len + infile_var_name_len + 7;
    outbuf_index += snprintf(outbuf + outbuf_index,
                             16, "%zi;\n", file_size);

    write_size = fwrite(outbuf, 1, outbuf_index, outfile);
    fflush(outfile);
    fclose(outfile);
}
