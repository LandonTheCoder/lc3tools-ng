#! /bin/sh
infile="$1"
outfile="$2"
# Note: I can substitute in something else for generating the header.
xxd -i "$infile" |sed -e 's/unsigned/unsigned const/g' > "$outfile"
