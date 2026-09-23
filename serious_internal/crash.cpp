#include "crash.h"
#include "overlay.h"

#include <Windows.h>
#include <dbghelp.h>
#include <cstdio>

namespace crash {

    typedef BOOL(WINAPI* MDWDFn)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
        PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
        PMINIDUMP_CALLBACK_INFORMATION);

    static volatile LONGLONG g_lastTick = 0;
    static bool g_hangDumped = false;

    static void WriteDump(const char* name, EXCEPTION_POINTERS* ep)
    {
        char dir[MAX_PATH] = {}, path[MAX_PATH] = {};
        GetTempPathA(sizeof dir - 1, dir);
        sprintf_s(path, "%s%s", dir, name);


        HMODULE hDbg = GetModuleHandleA("dbghelp.dll");
        if (!hDbg) hDbg = LoadLibraryA("dbghelp.dll");
        if (!hDbg) return;
        MDWDFn pDump = (MDWDFn)GetProcAddress(hDbg, "MiniDumpWriteDump");
        if (!pDump) return;

        HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return;

        if (ep) {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;
            pDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                  MiniDumpNormal, &mei, nullptr, nullptr);
        } else {
            
            pDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                  MiniDumpNormal, nullptr, nullptr, nullptr);
        }
        CloseHandle(f);
    }

    static LONG WINAPI Filter(EXCEPTION_POINTERS* ep)
    {
        WriteDump("serious_internal_crash.dmp", ep);

        char dir[MAX_PATH] = {}, msg[512] = {};
        GetTempPathA(sizeof dir - 1, dir);
        sprintf_s(msg, "serious_internal crashed.\nSend this file:\n%sserious_internal_crash.dmp", dir);
        MessageBoxA(nullptr, msg, "serious_internal", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
        return EXCEPTION_EXECUTE_HANDLER; 
    }

    void Init()
    {
        SetUnhandledExceptionFilter(Filter);
    }

    void WatchdogTick()
    {
        InterlockedExchange64(&g_lastTick, (LONGLONG)GetTickCount64());
    }

    void WatchdogCheck()
    {
        if (g_hangDumped) return;
        LONGLONG last = InterlockedCompareExchange64(&g_lastTick, 0, 0);
        if (!last) return; 
        ULONGLONG now = GetTickCount64();
       
        if (now - (ULONGLONG)last > 30000) {
            
            HWND w = (HWND)overlay::HookedHwnd();
            if (w && IsIconic(w)) {
                InterlockedExchange64(&g_lastTick, (LONGLONG)now);
                return;
            }
            g_hangDumped = true; 
            WriteDump("serious_internal_hang.dmp", nullptr);
            OutputDebugStringA("[serious_internal] HANG: no SwapBuffers for 30s, hang dump written\n");
        }
    }
}
