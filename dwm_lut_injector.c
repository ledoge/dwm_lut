#include <aclapi.h>
#include <stdbool.h>
#include <stdio.h>
#include <tlhelp32.h>
#include <windows.h>

#define DLL_NAME "dwm_lut.dll"
#define LUT_NAME "lut.png"

void ClearPermissions(char *filePath) {
    HANDLE hFile = CreateFileA(filePath, READ_CONTROL | WRITE_DAC, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    SetSecurityInfo(hFile, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL);
    CloseHandle(hFile);
}

void GetDebugPrivilege() {
    HANDLE hToken;
    OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken);
    LUID luid;
    LookupPrivilegeValueA(NULL, SE_DEBUG_NAME, &luid);

    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), (PTOKEN_PRIVILEGES) NULL, (PDWORD) NULL);

    CloseHandle(hToken);
}

int main(int argc, char *argv[]) {
    if (argc == 2) {
        bool doUnload = !strcmp("off", argv[1]);
        bool doInject = !strcmp("on", argv[1]);

        bool didUnload = false;

        if (doUnload || doInject) {
            char basePath[MAX_PATH];
            ExpandEnvironmentStringsA("%SYSTEMROOT%\\Temp\\", basePath, sizeof(basePath));

            char dllPath[MAX_PATH];
            char lutPath[MAX_PATH];

            strcpy(dllPath, basePath);
            strcat(dllPath, DLL_NAME);

            strcpy(lutPath, basePath);
            strcat(lutPath, LUT_NAME);

            GetDebugPrivilege();

            PROCESSENTRY32 processEntry;
            processEntry.dwSize = sizeof(PROCESSENTRY32);

            HANDLE processSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

            if (Process32First(processSnapshot, &processEntry) == TRUE) {
                while (Process32Next(processSnapshot, &processEntry) == TRUE) {
                    if (!strcmp(processEntry.szExeFile, "dwm.exe")) {
                        HANDLE dwm = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processEntry.th32ProcessID);

                        MODULEENTRY32 moduleEntry;
                        moduleEntry.dwSize = sizeof(MODULEENTRY32);

                        HANDLE moduleSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, processEntry.th32ProcessID);
                        if (Module32First(moduleSnapshot, &moduleEntry) == TRUE) {
                            while (Module32Next(moduleSnapshot, &moduleEntry) == TRUE) {
                                if (!strcmp(moduleEntry.szModule, DLL_NAME)) {
                                    HANDLE thread = CreateRemoteThread(dwm, NULL, 0, (LPTHREAD_START_ROUTINE) FreeLibrary, moduleEntry.modBaseAddr, 0, NULL);
                                    WaitForSingleObject(thread, INFINITE);
                                    CloseHandle(thread);

                                    didUnload = true;
                                }
                            }
                        }
                        CloseHandle(moduleSnapshot);
                        CloseHandle(dwm);
                    }
                }
            }
            if (doInject) {
                if (Process32First(processSnapshot, &processEntry) == TRUE) {
                    while (Process32Next(processSnapshot, &processEntry) == TRUE) {
                        if (!strcmp(processEntry.szExeFile, "dwm.exe")) {
                            HANDLE dwm = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processEntry.th32ProcessID);

                            if (!CopyFileA(DLL_NAME, dllPath, FALSE)) {
                                fprintf(stderr, "Failed to copy " DLL_NAME " - does it exist?\n");
                                return 1;
                            }
                            ClearPermissions(dllPath);

                            if (!CopyFileA(LUT_NAME, lutPath, FALSE)) {
                                fprintf(stderr, "Failed to copy " LUT_NAME " - does it exist?\n");
                                return 1;
                            }
                            ClearPermissions(lutPath);

                            LPVOID address = VirtualAllocEx(dwm, NULL, strlen(dllPath), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                            WriteProcessMemory(dwm, address, dllPath, strlen(dllPath), NULL);
                            HANDLE thread = CreateRemoteThread(dwm, NULL, 0, (LPTHREAD_START_ROUTINE) LoadLibraryA, address, 0, NULL);
                            WaitForSingleObject(thread, INFINITE);

                            DWORD exitCode;
                            GetExitCodeThread(thread, &exitCode);
                            if (exitCode == 0) {
                                fprintf(stderr, "LoadLibrary returned 0 - did dwmcore.dll get updated?");
                            }

                            CloseHandle(thread);
                            VirtualFreeEx(dwm, address, 0, MEM_RELEASE);

                            CloseHandle(dwm);
                        }
                    }
                }
            }
            CloseHandle(processSnapshot);

            if (doUnload) {
                if (didUnload) {
                    DeleteFileA(dllPath);
                    DeleteFileA(lutPath);
                } else {
                    fprintf(stderr, "LUT seems to be unloaded already - did nothing.\n");
                    return 1;
                }
            }

            return 0;
        }
    }
    fprintf(stderr, "Usage: dwm_lut.exe on|off\n");
    return 1;
}
