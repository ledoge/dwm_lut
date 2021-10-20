# About
This tool applies 3D LUTs to the Windows desktop by hooking into DWM, utilizing tetrahedral interpolation and ordered dithering. Right now it should work on any 20H2 or 21H1 build of Windows 10, and also the current build of Windows 11, and I'll try to update it whenever a new version breaks it.

# Usage
Use DisplayCAL or similar to generate the 65x65x65 .cube LUT files you want to apply, run `DwmLutGUI.exe`, assign them to monitors and then click Apply.

LUTs cannot be applied in HDR mode.

Note: DirectFlip and MPO get force disabled on monitors with an active LUT. These features are designed to improve performance for some windowed applications by allowing them to bypass DWM (and therefore also the LUT). This ensures that LUTs gets applied to all applications (except exclusive fullscreen ones).

# Compiling
Using MSYS2's mingw64 environment: Install `mingw-w64-x86_64-MinHook` and run
```
gcc dwm_lut.c -O3 -shared -static -s -lMinHook -ld3dcompiler -luuid -Wl,--exclude-all-symbols -o dwm_lut.dll
```

to generate the DLL.

As for the GUI, just open the project in Visual Studio and compile a x64 Release build.
