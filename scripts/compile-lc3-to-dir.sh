#! /bin/sh
infile="$1"
outdir="$2"
lc3as="$3"
echo "infile \"$infile\""
echo "outdir \"$outdir\""
echo "lc3as \"$lc3as\""
# lc3as cannot generate to a specified output directory, so move the input to the output directory
cp "$infile" "$outdir"

"${lc3as}" "${outdir}/"`basename "$infile"`
