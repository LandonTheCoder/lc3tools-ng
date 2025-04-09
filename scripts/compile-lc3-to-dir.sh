#! /bin/sh
usage() {
    echo "Usage: $0 INFILE OUTDIR LC3AS_BINARY"
}
# I must have 3 parameters ($0 doesn't count)
if [ "$#" -lt 3 ]; then
    echo "Not enough parameters specified."
    usage
    exit 1
fi
infile="$1"
outdir="$2"
lc3as="$3"
echo "infile \"$infile\""
echo "outdir \"$outdir\""
echo "lc3as \"$lc3as\""
# lc3as cannot generate to a specified output directory, so move the input to the output directory
cp "$infile" "$outdir"

"${lc3as}" "${outdir}/"`basename "$infile"`
