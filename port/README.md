# AceSpeeder! port

The documentation (building, project structure, where the outputs go,
installing on a PSP, controls) is in the [README at the repository root](../README.md).

Quick reference, from this directory:

```sh
export PATH=$HOME/pspdev/bin:$PATH
tools/make_dist.sh                       # PSP: dist/PSP/GAME/AceSpeeder/

cmake -S . -B build && cmake --build build
ACESPEEDER_DATA=data:../game build/acespeeder   # Linux
```
