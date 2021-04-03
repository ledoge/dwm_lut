# About
This is a DLL for applying 3D LUTs to the Windows Desktop by hooking into DWM. It doesn't work if you have more than one monitor, and there's probably a bunch of other things that can break it. It should (?) work on any 20H2 build of Windows 10, but it might not. For what it's worth, it definitely works when `dwmcore.dll` in `C:\Windows\System32` is version 10.0.19041.844 (CRC32 `CB031CC3`).

# Usage
Use DisplayCAL to generate a 65x65x65 PNG LUT and move it to `C:\lut.png`. Then, inject `dwm_lut.dll` into `dwm.exe` using the injector of your choice.
[Extreme Injector](https://github.com/master131/ExtremeInjector) works fine after setting the injection method to `Manual Map`. To disable the LUT, simply kill `dwm.exe`and it'll restart automatically.

If you get an error saying `d3dx11_43.dll` is missing, you'll have to install the [DirectX End-User Runtime](https://www.microsoft.com/en-us/download/details.aspx?id=35).

# Compiling
Using MSYS2: Install `mingw-w64-x86_64-MinHook` and then run

`gcc dwm_lut.c -O3 -shared -static -s -lMinHook -ld3dcompiler -ld3dx11 -luuid -Wl,--exclude-all-symbols -o dwm_lut.dll`
