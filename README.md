# About
This is a program for applying 3D LUTs to the Windows desktop by hooking into DWM. Right now it just applies the same LUT to all monitors. It should (?) work on any 20H2 or 21H1 build of Windows 10, but it might not. I'll do my best to update it whenever it stops working.

# Usage
Use DisplayCAL to generate a 65x65x65 PNG LUT with filename `lut.png` and put it in the same folder as the extracted release. Run `Enable LUT.bat` to enable the LUT and `Disable LUT.bat` to disable it again.

If you don't have `d3dx11_43.dll` in System32, you'll have to install the [DirectX End-User Runtime](https://www.microsoft.com/en-us/download/details.aspx?id=35).

Note: While the LUT is enabled, DirectFlip and MPO are force disabled. These features are designed to improve performance for some windowed applications by allowing them bypass DWM (and therefore also the LUT). This ensures that the LUT gets applied to all applications (except exclusive fullscreen ones).

# Compiling
Using MSYS2's mingw64 environment: Install `mingw-w64-x86_64-MinHook` and then run

```bash
gcc dwm_lut.c -O3 -shared -static -s -lMinHook -ld3dcompiler -ld3dx11 -luuid -Wl,--exclude-all-symbols -o dwm_lut.dll
windres dwm_lut.rc dwm_lut_res.o
gcc dwm_lut_injector.c dwm_lut_res.o -O3 -s -o dwm_lut.exe
```
