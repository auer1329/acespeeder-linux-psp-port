# Ace Speeder — reverse-engineering notes

Binary: `game/AceSpeeder.exe` (MSVC 6, i386, 2000-07-12). Game code is 0x401000–0x43da00;
everything from 0x43ed6c up is the MSVC runtime. Pipeline: `re/run.sh` (see scripts/).
Names live in `re/names.txt`; annotated decompilation in `re/decomp/` (made by `re/run.sh`, not in git).

## Platform APIs
* Direct3D Retained Mode through **IDirect3DRM2** (`g_RM2_main`), the author's `direct3z.cpp` wrapper ("D3Z").
  640x480x16, double buffered, viewport back clip = 5000 (0x459c4000).
* 2D HUD/menus: DirectDraw `Blt` with source colour key from bitmaps loaded via `LoadImage` (Sprite class).
* Input: DirectInput keyboard + optional joystick.
* SFX: PDS.dll (12 WAVs), music: kbgm32.dll (8 MIDIs).

## Main loop (`WinMain_` / `MainFrame`)
Fixed timestep: `Timer_Step` advances 33 ms per logic tick, at most 5 ticks per rendered frame,
then renders once. Logic rate ≈ 30.3 Hz.
Each tick: `g_inputPrev = g_input`, clear, read joystick + keyboard, then dispatch on `g_gameState`:

| state | logic | render (when obj state == 10) |
|---|---|---|
| 100 | load save data (0x451438), all object states := 20, → 900 | – |
| 200 | `FUN_0041f300(0x45a658)` (menu?) | `FUN_00422990` |
| 300 | `FUN_00415280(0x4513e0)` | `FUN_00416570` |
| 500 | `FUN_00411530(0x4513e0)` (race) | inline in MainFrame |
| 700 | `FUN_0040f310(0x44f40c)` | `FUN_0040f570` |
| 800 | quit: free everything, PostQuitMessage | – |
| 900 | `FUN_00419f80(0x452da0)` (title?) | `FUN_0041bbf0` |
ESC (`g_input[7]`) at any time → 800.

## Input struct (`g_input`, 14 dwords, 1 = held)
0 up, 1 down, 2 right, 3 left, 4 A (Space/Enter/KP-Enter, even joy buttons),
5 B (Shift, odd joy buttons), 7 Esc, 8–11 F1–F4. Arrows and numpad 8/2/4/6 both map to directions.
Joystick axes: |value| > 50 of range ±?.

## Sound ids (`Sound_Play(g_sound, id)`)
0 null, 1 jump, 2 explos, 3 crash, 4 select, 5 cansel, 6 raptim, 7 decide, 8 engine (looped, pitch via SetFreq),
9 warn_tim, 10 warn_pow, 11 set.

## Music ids (`Music_Play(g_music, id)`)
0 BlackJackMayol, 1 PhutaMusicA, 2 DeadendCorner, 3 PhutaMusicB, 4 MagicalParty, 5 PhutaMusicC,
6 AquaGunShoot, 7 DeepBlueMayol.

## Math
Mat3 is row-major 9 floats; Vec3 3 floats; passed by value (hence in_stack_ noise).
`crt_rand` is the MSVC LCG (seeded with timeGetTime at start) — replays must not depend on it
or must store seed (TODO check).
The x87 code keeps intermediates in 80-bit; the port uses float/double, so tiny drift is expected.

## Courses
Tracks are not files: ~70 functions at 0x424b10–0x43d370 fill control-point tables
(position, width, cross-section profile id per point) and call the builders
`FUN_0040e6c0` (cross-section profile → 8 (x,y) points), `FUN_0040ea60` (per-edge surface types),
`FUN_0040ebb0`, `FUN_0040b3f0`, and `FUN_00419810` (place pickups/boards).
6 stages × 6 courses (= 36 `replay_S_C.dat`).
