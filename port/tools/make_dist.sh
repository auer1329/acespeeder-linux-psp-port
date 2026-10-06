#!/bin/sh
# Build the PSP game and assemble dist/PSP/GAME/AceSpeeder/, ready to copy to the
# root of a Memory Stick. Needs the original game folder (for the meshes, bitmaps,
# sounds and replays), the PSP SDK in PATH and, for the music, fluidsynth/oggenc.
# Usage (from port/): tools/make_dist.sh [game dir]
set -e
GAME=${1:-../game}
OUT=dist/PSP/GAME/AceSpeeder

[ -f data/track_1_1.bin ] || ../re/.venv/bin/python ../re/scripts/extract_tracks.py
ls data/*.ogg > /dev/null 2>&1 || tools/midi2ogg.sh "$GAME" data
python3 tools/make_icons.py "$GAME" .
rm -f EBOOT.PBP PARAM.SFO
make -f Makefile.psp clean > /dev/null
make -f Makefile.psp APP=game > /dev/null

rm -rf "$OUT"
mkdir -p "$OUT/save"
cp EBOOT.PBP "$OUT/"
for f in "$GAME"/*.x "$GAME"/*.bmp "$GAME"/*.wav "$GAME"/replay_*.dat "$GAME"/gamedata.dat; do
	cp "$f" "$OUT/"
done
cp data/track_*.bin data/*.ogg "$OUT/"
echo "$OUT: $(ls "$OUT" | wc -l) files, $(du -sh "$OUT" | cut -f1)"
