#include <stdio.h>
#include <windows.h>

BOOL MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    MONITORINFOEX monitorInfo;
    monitorInfo.cbSize = sizeof(MONITORINFOEX);
    GetMonitorInfo(hMonitor, (MONITORINFO *) &monitorInfo);

    if (!(monitorInfo.dwFlags & DISPLAY_DEVICE_MIRRORING_DRIVER)) {
        RECT* rect = &monitorInfo.rcMonitor;
        printf("%s", monitorInfo.szDevice);
        if (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) {
            printf(" (primary monitor)");
        }
        printf("\n");
        printf("Rectangle: (%d, %d), (%d, %d)\n", rect->left, rect->top, rect->right, rect->bottom);
        printf("Resolution: %dx%d\n", rect->right - rect->left, rect->bottom - rect->top);
        printf("Filename: %d_%d.cube\n", rect->left, rect->top);
        printf("\n");
    }

    return TRUE;
}

int main() {
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
    system("pause");
}
