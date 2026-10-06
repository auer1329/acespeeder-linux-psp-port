#!/bin/sh
# Rebuild the Ghidra project from scratch, apply fixes + names.txt,
# then export and annotate the decompilation into decomp/.
set -e
cd "$(dirname "$0")"
GHIDRA=${GHIDRA:-$HOME/ghidra_12.1.4_PUBLIC}
rm -rf decomp_raw decomp ghidra_project && mkdir ghidra_project
"$GHIDRA/support/analyzeHeadless" ghidra_project AceSpeeder -import ../game/AceSpeeder.exe \
	-scriptPath scripts \
	-postScript TypeRdataConsts.java \
	-postScript FixComCalls.java "$PWD/com_nargs.txt" \
	-postScript ApplySigs.java "$PWD/sigs.txt" "$PWD/types.h" \
	-postScript ApplyNames.java "$PWD/names.txt" \
	-postScript CommitParams.java \
	-postScript ExportDecomp.java "$PWD/decomp_raw" > ghidra_headless.log 2>&1 || { tail -20 ghidra_headless.log; exit 1; }
grep -E "(TypeRdataConsts|FixComCalls|ApplyNames|ApplySigs|CommitParams):" ghidra_headless.log | sed 's/.*> //;s/ (GhidraScript).*//'
python3 scripts/annotate.py decomp_raw decomp
echo "exported $(ls decomp | wc -l) files"
