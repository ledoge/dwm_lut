# About
This tool applies 3D LUTs to the Windows desktop by hooking into DWM, utilizing tetrahedral interpolation and ordered dithering. Right now it just applies the same LUT to all monitors. It should (?) work on any 20H2 or 21H1 build of Windows 10, but it might not. I'll do my best to update it whenever it stops working.

# Usage
Use DisplayCAL or similar to generate a 65x65x65 .cube LUT with filename `lut.cube` and put it in the same folder as the extracted release. Run `Enable LUT.bat` to enable the LUT and `Disable LUT.bat` to disable it again.

Note: While the LUT is enabled, DirectFlip and MPO are force disabled. These features are designed to improve performance for some windowed applications by allowing them bypass DWM (and therefore also the LUT). This ensures that the LUT gets applied to all applications (except exclusive fullscreen ones).

# Compiling
Using MSYS2's mingw64 environment: Install `mingw-w64-x86_64-MinHook` and run the included `build.sh` script.
