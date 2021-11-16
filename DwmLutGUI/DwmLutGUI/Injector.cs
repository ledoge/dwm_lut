using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

namespace DwmLutGUI
{
    internal static class Injector
    {
        public static readonly bool NoDebug;

        private static readonly string DllName;
        private static readonly string DllPath;
        private static readonly string LutsPath;
        private static readonly IntPtr LoadlibraryA;
        private static readonly IntPtr FreeLibrary;

        static Injector()
        {
            var basePath = Environment.ExpandEnvironmentVariables("%SYSTEMROOT%\\Temp\\");
            DllName = "dwm_lut.dll";
            DllPath = basePath + DllName;
            LutsPath = basePath + "luts\\";

            var kernel32 = GetModuleHandle("kernel32.dll");
            LoadlibraryA = GetProcAddress(kernel32, "LoadLibraryA");
            FreeLibrary = GetProcAddress(kernel32, "FreeLibrary");

            try
            {
                Process.EnterDebugMode();
            }
            catch (Exception)
            {
#if RELEASE
                MessageBox.Show("Failed to enter debug mode – will not be able to apply LUTs.");
#endif
                NoDebug = true;
            }
        }

        public static bool? GetStatus()
        {
            if (NoDebug) return null;

            var dwmInstances = Process.GetProcessesByName("dwm");
            if (dwmInstances.Length == 0) return null;

            bool? result = false;
            foreach (var dwm in dwmInstances)
            {
                var modules = dwm.Modules;
                foreach (ProcessModule module in modules)
                {
                    if (module.ModuleName == DllName)
                    {
                        result = true;
                    }

                    module.Dispose();
                }

                dwm.Dispose();
            }

            return result;
        }

        public static void Inject(IEnumerable<MonitorData> monitors)
        {
            File.Copy(AppDomain.CurrentDomain.BaseDirectory + DllName, DllPath, true);
            ClearPermissions(DllPath);

            if (Directory.Exists(LutsPath))
            {
                Directory.Delete(LutsPath, true);
            }

            Directory.CreateDirectory(LutsPath);
            ClearPermissions(LutsPath);

            foreach (var monitor in monitors)
            {
                if (!string.IsNullOrEmpty(monitor.SdrLutPath))
                {
                    var path = LutsPath + monitor.Position.Replace(',', '_') + ".cube";
                    File.Copy(monitor.SdrLutPath, path);
                    ClearPermissions(path);
                }

                if (string.IsNullOrEmpty(monitor.HdrLutPath)) continue;
                {
                    var path = LutsPath + monitor.Position.Replace(',', '_') + "_hdr.cube";
                    File.Copy(monitor.HdrLutPath, path);
                    ClearPermissions(path);
                }
            }

            var failed = false;
            var bytes = Encoding.ASCII.GetBytes(DllPath);
            var dwmInstances = Process.GetProcessesByName("dwm");
            foreach (var dwm in dwmInstances)
            {
                var address = VirtualAllocEx(dwm.Handle, IntPtr.Zero, (UIntPtr)bytes.Length,
                    AllocationType.Reserve | AllocationType.Commit, MemoryProtection.ReadWrite);
                WriteProcessMemory(dwm.Handle, address, bytes, (UIntPtr)bytes.Length, out _);
                var thread = CreateRemoteThread(dwm.Handle, IntPtr.Zero, 0, LoadlibraryA, address, 0, out _);
                WaitForSingleObject(thread, uint.MaxValue);

                GetExitCodeThread(thread, out uint exitCode);
                if (exitCode == 0)
                {
                    failed = true;
                }

                CloseHandle(thread);
                VirtualFreeEx(dwm.Handle, address, 0, FreeType.Release);

                dwm.Dispose();
            }

            Directory.Delete(LutsPath, true);

            if (failed)
            {
                throw new Exception(
                    "Failed to load or initialize DLL. This probably means that a LUT file is malformed or that DWM got updated.");
            }
        }

        public static void Uninject()
        {
            var dwmInstances = Process.GetProcessesByName("dwm");
            foreach (var dwm in dwmInstances)
            {
                var modules = dwm.Modules;
                foreach (ProcessModule module in modules)
                {
                    if (module.ModuleName == DllName)
                    {
                        var thread = CreateRemoteThread(dwm.Handle, IntPtr.Zero, 0, FreeLibrary, module.BaseAddress,
                            0, out _);
                        WaitForSingleObject(thread, uint.MaxValue);
                        CloseHandle(thread);
                    }

                    module.Dispose();
                }

                dwm.Dispose();
            }

            File.Delete(DllPath);
        }

        private static void ClearPermissions(string path)
        {
            var hFile = CreateFile(path, DesiredAccess.ReadControl | DesiredAccess.WriteDac, 0, IntPtr.Zero,
                CreationDisposition.OpenExisting,
                FlagsAndAttributes.FileAttributeNormal | FlagsAndAttributes.FileFlagBackupSemantics,
                IntPtr.Zero);
            SetSecurityInfo(hFile, SeObjectType.SeFileObject, SecurityInformation.DaclSecurityInformation, IntPtr.Zero,
                IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
            CloseHandle(hFile);
        }

        [DllImport("kernel32.dll")]
        private static extern IntPtr GetModuleHandle(string lpFileName);

        [DllImport("kernel32.dll")]
        private static extern IntPtr GetProcAddress(IntPtr hModule, string procName);

        [DllImport("kernel32.dll")]
        private static extern IntPtr VirtualAllocEx(IntPtr hProcess, IntPtr lpAddress, UIntPtr dwSize,
            AllocationType flAllocationType, MemoryProtection flProtect);

        [DllImport("kernel32.dll")]
        private static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer,
            UIntPtr nSize,
            out UIntPtr lpNumberOfBytesWritten);

        [DllImport("kernel32.dll")]
        private static extern bool VirtualFreeEx(IntPtr hProcess, IntPtr lpAddress, int dwSize, FreeType dwFreeType);

        [DllImport("kernel32.dll")]
        private static extern IntPtr CreateRemoteThread(IntPtr hProcess, IntPtr lpThreadAttributes, uint dwStackSize,
            IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, out uint lpThreadId);

        [DllImport("kernel32.dll")]
        private static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

        [DllImport("kernel32.dll")]
        private static extern bool GetExitCodeThread(IntPtr hThread, out uint lpExitCode);

        [DllImport("kernel32.dll")]
        private static extern IntPtr CloseHandle(IntPtr hObject);


        [DllImport("kernel32.dll")]
        private static extern IntPtr CreateFile(string lpFileName, DesiredAccess dwDesiredAccess, uint dwShareMode,
            IntPtr lpSecurityAttributes, CreationDisposition dwCreationDisposition,
            FlagsAndAttributes dwFlagsAndAttributes, IntPtr hTemplateFile);

        [DllImport("advapi32.dll")]
        private static extern uint SetSecurityInfo(IntPtr handle, SeObjectType ObjectType,
            SecurityInformation SecurityInfo, IntPtr psidOwner,
            IntPtr psidGroup, IntPtr pDacl, IntPtr pSacl);

        [Flags]
        private enum FreeType
        {
            Release = 0x8000,
        }

        [Flags]
        private enum AllocationType
        {
            Commit = 0x1000,
            Reserve = 0x2000
        }

        [Flags]
        private enum MemoryProtection
        {
            ReadWrite = 0x04
        }

        [Flags]
        private enum DesiredAccess
        {
            ReadControl = 0x20000,
            WriteDac = 0x40000
        }

        private enum CreationDisposition
        {
            OpenExisting = 3
        }

        [Flags]
        private enum FlagsAndAttributes
        {
            FileAttributeNormal = 0x80,
            FileFlagBackupSemantics = 0x2000000
        }

        private enum SeObjectType
        {
            SeFileObject = 1
        }

        private enum SecurityInformation
        {
            DaclSecurityInformation = 0x4
        }
    }
}