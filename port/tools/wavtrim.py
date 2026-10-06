#!/usr/bin/env python3
"""Normalise a 16-bit WAV to 90% peak and cut the silence after the last note,
so the music loops without a gap. Usage: wavtrim.py in.wav out.wav"""
import array
import sys
import wave

src = wave.open(sys.argv[1])
ch, rate = src.getnchannels(), src.getframerate()
a = array.array("h", src.readframes(src.getnframes()))
peak = max(1, max(abs(x) for x in a))
end = len(a)
while end > 0 and abs(a[end - 1]) < peak * 0.01:
    end -= 1
end = min(len(a), end - end % ch + int(0.05 * rate) * ch)  # keep a short release
scale = 0.9 * 32767 / peak
out = array.array("h", (int(x * scale) for x in a[:end]))
dst = wave.open(sys.argv[2], "wb")
dst.setnchannels(ch)
dst.setsampwidth(2)
dst.setframerate(rate)
dst.writeframes(out.tobytes())
