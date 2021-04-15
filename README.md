# About
This is a DLL for applying 3D LUTs to the Windows Desktop by hooking into DWM. Right now it just applies the same LUT to all monitors. It should (?) work on any 20H2 build of Windows 10, but it might not. For what it's worth, it definitely works when `dwmcore.dll` in `C:\Windows\System32` is version 10.0.19041.844 (CRC32 `CB031CC3`).

# Usage
Use DisplayCAL to generate a 65x65x65 PNG LUT with filename `lut.png` and put it in the same folder as the extracted release. Run `Enable LUT.bat` to enable the LUT and `Disable LUT.bat` to disable it again.

If you don't have `d3dx11_43.dll` in System32, you'll have to install the [DirectX End-User Runtime](https://www.microsoft.com/en-us/download/details.aspx?id=35).

Also: If your system supports it, Windows will randomly decide to draw some content as overlays using MPO, which completely bypasses DWM and therefore also the LUT. You can disable MPO by following the instructions [here](https://nvidia.custhelp.com/app/answers/detail/a_id/5157).

# Compiling
Using MSYS2: Install `mingw-w64-x86_64-MinHook` and then run

```bash
gcc dwm_lut.c -O3 -shared -static -s -lMinHook -ld3dcompiler -ld3dx11 -luuid -Wl,--exclude-all-symbols -o dwm_lut.dll
windres dwm_lut.rc dwm_lut_res.o
gcc dwm_lut_injector.c dwm_lut_res.o -O3 -s -o dwm_lut.exe
```
