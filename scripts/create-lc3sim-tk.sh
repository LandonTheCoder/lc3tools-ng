#! /bin/sh
usage() {
    echo "Usage: $0 INFILE OUTFILE"
}

# 2 parameters specifed (INFILE, OUTFILE)
if [ "$#" -lt 2 ]; then
    echo "Not enough parameters specified."
    usage
    exit 1
fi
infile="$1"
outfile="$2"


# Based on the makefile
sed \
  -e 's/@@CODE_FONT@@/{{Lucida Console} 11 bold}/g' \
  -e 's/@@BUTTON_FONT@@/{{Lucida Console} 10 normal}/g' \
  -e 's/@@CONSOLE_FONT@@/{{Lucida Console} 10 bold}/g' \
  -e "s/@@WISH@@/${WISH:-wish}/g" \
  -e 's/@@LC3_SIM@@/"lc3sim"/g' \
  "$infile" > "$outfile"

# Fix permissions on the output file
chmod -v u+x "$outfile"
