#!/bin/sh

mkdir -p release
gcc dwm_lut.c -O3 -shared -static -s -lMinHook -ld3dcompiler -luuid -Wl,--exclude-all-symbols -o release/dwm_lut.dll
windres dwm_lut.rc dwm_lut_res.o
gcc dwm_lut_injector.c dwm_lut_res.o -O3 -s -o release/dwm_lut.exe
windres list_monitors.rc list_monitors_res.o
gcc list_monitors.c list_monitors_res.o -O3 -s -o release/list_monitors.exe
cp *.bat release/
