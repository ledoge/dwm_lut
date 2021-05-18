#include <aclapi.h>
#include <stdbool.h>
#include <stdio.h>
#include <tlhelp32.h>
#include <windows.h>

#define BASEPATH "%SYSTEMROOT%\\Temp\\"
#define DLL_NAME "dwm_lut.dll"
#define LUT_NAME "lut.cube"
#define LUT_FOLDER "luts"

void ClearPermissions(char *filePath) {
    HANDLE hFile = CreateFileA(filePath, READ_CONTROL | WRITE_DAC, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, NULL);
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

bool FileExists(char* path) {
    DWORD attributes = GetFileAttributesA(path);
    return (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY));
}

bool FolderExists(char* path) {
    DWORD attributes = GetFileAttributesA(path);
    return (attributes != INVALID_FILE_ATTRIBUTES && attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool CopyFolder(char* source, char* dest) {
    SHFILEOPSTRUCTA s = {};
    s.wFunc = FO_COPY;
    s.fFlags = FOF_NO_UI;
    s.pTo = dest;

    char from[MAX_PATH];
    strcpy(from, source);
    strcat(from, "\\*");
    s.pFrom = from;

    return !SHFileOperation(&s);
}

void ClearAllPermissions(char* folder) {
    WIN32_FIND_DATAA findData;

    char path[MAX_PATH];
    strcpy(path, folder);
    strcat(path, "\\*");

    HANDLE hFind = FindFirstFileA(path, &findData);
    do
    {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            char filePath[MAX_PATH];
            strcpy(filePath, folder);
            strcat(filePath, "\\");
            strcat(filePath, findData.cFileName);

            ClearPermissions(filePath);
        }
    }
    while (FindNextFile(hFind, &findData) != 0);

    ClearPermissions(folder);
}

bool DeleteFolder(char* path) {
    SHFILEOPSTRUCTA s = {};
    s.wFunc = FO_DELETE;
    s.fFlags = FOF_NO_UI;
    s.pFrom = path;

    return !SHFileOperation(&s);
}

int main(int argc, char *argv[]) {
    if (argc == 2) {
        bool doUnload = !strcmp("off", argv[1]);
        bool doInject = !strcmp("on", argv[1]);

        bool didUnload = false;
        bool didInject = false;
        bool singleLutMode = true;

        if (doUnload || doInject) {
            char basePath[MAX_PATH];
            ExpandEnvironmentStringsA(BASEPATH, basePath, sizeof(basePath));

            char dllPath[MAX_PATH];
            char lutPath[MAX_PATH];
            char lutFolderPath[MAX_PATH];

            strcpy(dllPath, basePath);
            strcat(dllPath, DLL_NAME);

            strcpy(lutPath, basePath);
            strcat(lutPath, LUT_NAME);

            strcpy(lutFolderPath, basePath);
            strcat(lutFolderPath, LUT_FOLDER);

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
                if (FileExists(LUT_NAME)) {
                    if (!CopyFileA(LUT_NAME, lutPath, FALSE)) {
                        fprintf(stderr, "Failed to copy " LUT_NAME " file.\n");
                        return 1;
                    }
                    ClearPermissions(lutPath);
                }
                else if (FolderExists(LUT_FOLDER)){
                    singleLutMode = false;
                    if (!CopyFolder(LUT_FOLDER, lutFolderPath)) {
                        fprintf(stderr, "Failed to copy " LUT_FOLDER " folder.\n");
                        return 1;
                    }
                    ClearAllPermissions(lutFolderPath);
                }
                else {
                    fprintf(stderr, "No " LUT_NAME " file or " LUT_FOLDER " folder found.\n");
                    return 1;
                }

                if (!CopyFileA(DLL_NAME, dllPath, FALSE)) {
                    fprintf(stderr, "Failed to copy " DLL_NAME ".\n");
                    return 1;
                }
                ClearPermissions(dllPath);

                if (Process32First(processSnapshot, &processEntry) == TRUE) {
                    while (Process32Next(processSnapshot, &processEntry) == TRUE) {
                        if (!strcmp(processEntry.szExeFile, "dwm.exe")) {
                            HANDLE dwm = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processEntry.th32ProcessID);

                            LPVOID address = VirtualAllocEx(dwm, NULL, strlen(dllPath), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                            WriteProcessMemory(dwm, address, dllPath, strlen(dllPath), NULL);
                            HANDLE thread = CreateRemoteThread(dwm, NULL, 0, (LPTHREAD_START_ROUTINE) LoadLibraryA, address, 0, NULL);
                            WaitForSingleObject(thread, INFINITE);

                            DWORD exitCode;
                            GetExitCodeThread(thread, &exitCode);
                            if (exitCode == 0) {
                                fprintf(stderr, "Failed to load or initialize DLL.\n");
                            }
                            else {
                                didInject |= true;
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
                } else {
                    fprintf(stderr, "LUT does not seem to be loaded - did nothing.\n");
                    return 1;
                }
            }
            else if (doInject) {
                if (singleLutMode) {
                    DeleteFileA(lutPath);
                }
                else {
                    DeleteFolder(lutFolderPath);
                }
                if (!didInject) {
                    DeleteFileA(dllPath);
                }
            }

            return 0;
        }
    }
    fprintf(stderr, "Usage: dwm_lut.exe on|off\n");
    return 1;
}
