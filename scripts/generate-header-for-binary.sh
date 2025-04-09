#! /bin/sh
# I must have 2 parameters (infile, outfile)
if [ "$#" -lt 2 ]; then
    echo "Not enough parameters specified."
    echo "Usage: $0 INFILE OUTFILE"
    exit 1
fi

infile="$1"
outfile="$2"
# Note: I can substitute in something else for generating the header.
xxd -i "$infile" |sed -e 's/unsigned/unsigned const/g' > "$outfile"
