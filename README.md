# About
This tool applies 3D LUTs to the Windows desktop by hooking into DWM, utilizing tetrahedral interpolation and ordered dithering. It should (?) work on any 20H2 or 21H1 build of Windows 10, but it might not. I'll do my best to update it whenever it stops working.

# Usage
## Single LUT for all monitors
Use DisplayCAL or similar to generate a 65x65x65 .cube LUT with filename `lut.cube` and put it in the same folder as the extracted release. Run `Enable LUT.bat` to enable the LUT and `Disable LUT.bat` to disable it again.

## One LUT per monitor
Run `list_monitors.exe`, which prints some basic information about the active displays. Use the provided coordinates to figure out which monitor is which and rename your LUT files accordingly, using the provided filenames. Make sure that there is no `lut.cube` file, create a `luts` folder instead and put the LUT files in there. Run `Enable LUT.bat` and it should work.

LUTs cannot be applied in HDR mode.

Note: DirectFlip and MPO get force disabled on monitors with an active LUT. These features are designed to improve performance for some windowed applications by allowing them to bypass DWM (and therefore also the LUT). This ensures that the LUT gets applied to all applications (except exclusive fullscreen ones).

# Compiling
Using MSYS2's mingw64 environment: Install `mingw-w64-x86_64-MinHook` and run the included `build.sh` script.
