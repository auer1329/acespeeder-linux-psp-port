#!/bin/sh
# Pre-render the game's MIDI music to Ogg Vorbis (the PSP has no MIDI synth).
# The trailing silence is cut so the loop has no gap; the level is normalised.
# Usage: tools/midi2ogg.sh [game dir] [output dir] [soundfont]
set -e
GAME=${1:-../game}
OUT=${2:-data}
SF=${3:-/usr/share/sounds/sf2/FluidR3_GM.sf2}
mkdir -p "$OUT"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
for mid in "$GAME"/*.mid; do
	name=$(basename "$mid" .mid)
	fluidsynth -q -n -i -F "$TMP/raw.wav" -r 44100 -g 0.3 "$SF" "$mid" > /dev/null
	python3 "$(dirname "$0")/wavtrim.py" "$TMP/raw.wav" "$TMP/$name.wav"
	oggenc -Q -q 3 -o "$OUT/$name.ogg" "$TMP/$name.wav"
	echo "$name.ogg"
done
