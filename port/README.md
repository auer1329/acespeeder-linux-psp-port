# AceSpeeder! for PSP and Linux

A source port of AceSpeeder! (2000, Takahiro Nakatani / Production TN): the game
logic is reimplemented from the original Windows executable, and the graphics,
sound and input go through a small platform layer (SDL2 + OpenGL on Linux,
sceGu on the PSP).

The original game files are not included. You need your own copy of the game
folder (`AceSpeeder.exe` and its data), placed at `../game` relative to this
directory.

## PSP

Requirements: the [PSP SDK](https://github.com/pspdev/pspdev) in `PATH`,
Python 3 with Pillow, and `fluidsynth`, `vorbis-tools` and a General MIDI
soundfont (`/usr/share/sounds/sf2/FluidR3_GM.sf2`) for the music.

    tools/make_dist.sh

This converts the data that needs it (the courses from the executable, the MIDI
music to Ogg Vorbis, the menu icon), builds `EBOOT.PBP` and assembles
`dist/PSP/GAME/AceSpeeder/`. Copy the `PSP` folder to the root of the Memory
Stick, or the `AceSpeeder` folder into PPSSPP's `PSP/GAME`: the game needs all of
its files, not only `EBOOT.PBP` (they may also go in a `data` folder next to it).
If files are missing the game says which. Saves and replays go to
`PSP/GAME/AceSpeeder/save/`.

| PSP | Game |
|---|---|
| D-pad / analog stick | steer, move in the menus |
| Cross, Start | accelerate, confirm |
| Circle, Square | pause, back |
| Triangle | change the camera view |

## Linux

    cmake -S . -B build && cmake --build build
    ACESPEEDER_DATA=data:../game build/acespeeder

`ACESPEEDER_DATA` lists the data folders, separated by `:`; saves go to
`~/.local/share/acespeeder` unless `ACESPEEDER_SAVE` says otherwise. Keys, as in
the original: arrows, Space or Enter (accelerate / confirm), Shift (pause / back),
F1–F4 (camera views); a gamepad works too.

`build/acespeeder <stage> <course> <machine> <mode>` starts straight in a race
(mode 0 grand prix, 1 practice, 2 replay). For debugging, `ACESPEEDER_SHOTS`,
`ACESPEEDER_KEYS`, `ACESPEEDER_RATE` and `ACESPEEDER_STATE` are described at the
top of `src/main.c`.

## Not ported

The attract-mode race that the original sometimes runs after idling on the title
screen (game state 300); the port returns to the title instead.
