#include "wininput.h"
#include "overlay.h"

#include <Windows.h>
#include <intrin.h>

#include "MinHook.h"

namespace wininput {

    typedef SHORT(WINAPI* GASFn)(int);
    typedef SHORT(WINAPI* GKSFn)(int);
    typedef BOOL(WINAPI* GKBFn)(LPBYTE);
    typedef BOOL(WINAPI* GCPFn)(LPPOINT);
    typedef BOOL(WINAPI* SCPFn)(int, int);
    typedef BOOL(WINAPI* ClipFn)(CONST RECT*);
    typedef int(WINAPI* ShowFn)(BOOL);
    typedef void(WINAPI* MEvtFn)(DWORD, DWORD, DWORD, DWORD, ULONG_PTR);
    typedef UINT(WINAPI* SIFn)(UINT, LPINPUT, int);
    typedef HCURSOR(WINAPI* SCurFn)(HCURSOR);
    typedef UINT(WINAPI* GRIDFn)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
    typedef UINT(WINAPI* GRIBFn)(PRAWINPUT, PUINT, UINT);
    typedef BOOL(WINAPI* GCP2Fn)(LPPOINT);
    typedef int(WINAPI* GMMExFn)(UINT, LPVOID, LPVOID, int, DWORD);

    static GASFn oGAS = nullptr;
    static GKSFn oGKS = nullptr;
    static GKBFn oGKB = nullptr;
    static GCPFn oGCP = nullptr;
    static SCPFn oSCP = nullptr;
    static ClipFn oClip = nullptr;
    static ShowFn oShow = nullptr;
    static MEvtFn oMEvt = nullptr;
    static SIFn oSI = nullptr;
    static SCurFn oSCur = nullptr;
    static GRIDFn oGRID = nullptr;
    static GRIBFn oGRIB = nullptr;
    static GCP2Fn oGCP2 = nullptr;
    static GMMExFn oGMMEx = nullptr;

    static void* g_t[14] = {};
    static HMODULE g_self = nullptr;

    static POINT g_frozen{};
    static bool  g_haveFrozen = false;

  
    static LONG    g_hideSwallowed = 0, g_showSwallowed = 0, g_ourShows = 0;
    static HCURSOR g_gameCursor = nullptr;
    static bool    g_haveGameCursor = false;
    static RECT    g_gameClip{};
    static bool    g_haveGameClip = false, g_gameClipNull = false;

    static void SelfAnchor() {}

    
    static bool SelfCaller(void* ret)
    {
        MEMORY_BASIC_INFORMATION mi{};
        if (!ret || !VirtualQuery(ret, &mi, sizeof mi) || !mi.AllocationBase)
            return false;
        return mi.AllocationBase == g_self;
    }

    static __forceinline bool Block(void* ret)
    {
        return overlay::IsVisible() && !SelfCaller(ret);
    }

    static SHORT WINAPI HkGAS(int v)
    {
        void* ra = _ReturnAddress();
        SHORT r = oGAS(v);
        if (Block(ra))
            return 0; 
        return r;
    }

    static SHORT WINAPI HkGKS(int v)
    {
        void* ra = _ReturnAddress();
        SHORT r = oGKS(v);
        if (Block(ra))
            return 0;
        return r;
    }

    static BOOL WINAPI HkGKB(LPBYTE b)
    {
        void* ra = _ReturnAddress();
        BOOL r = oGKB(b);
        if (b && Block(ra))
            memset(b, 0, 256);
        return r;
    }

    static BOOL WINAPI HkGCP(LPPOINT p)
    {
        void* ra = _ReturnAddress();
        BOOL r = oGCP(p);
        if (p && Block(ra)) {
            if (!g_haveFrozen && r) { g_frozen = *p; g_haveFrozen = true; }
            *p = g_frozen;
            return TRUE;
        }

        if (!overlay::IsVisible()) g_haveFrozen = false;
        return r;
    }

    static BOOL WINAPI HkSCP(int x, int y)
    {
        void* ra = _ReturnAddress();
        if (Block(ra))
            return TRUE; 
        return oSCP(x, y);
    }

    static BOOL WINAPI HkClip(CONST RECT* rc)
    {
        void* ra = _ReturnAddress();
        if (Block(ra)) {
            g_gameClipNull = (rc == nullptr);
            if (rc) g_gameClip = *rc;
            g_haveGameClip = true;
            return TRUE;
        }
        return oClip(rc);
    }

    static int WINAPI HkShow(BOOL b)
    {
        void* ra = _ReturnAddress();
        if (Block(ra)) {
            if (b) InterlockedIncrement(&g_showSwallowed); else InterlockedIncrement(&g_hideSwallowed);
            return TRUE; 
        }
        return oShow(b);
    }

    static void WINAPI HkMEvt(DWORD f, DWORD x, DWORD y, DWORD d, ULONG_PTR e)
    {
        void* ra = _ReturnAddress();
        if (Block(ra))
            return;
        oMEvt(f, x, y, d, e);
    }

    static UINT WINAPI HkSI(UINT n, LPINPUT p, int s)
    {
        void* ra = _ReturnAddress();
        if (Block(ra))
            return n; 
        return oSI(n, p, s);
    }

    static HCURSOR WINAPI HkSCur(HCURSOR c)
    {
        void* ra = _ReturnAddress();
        if (Block(ra)) {
            g_gameCursor = c;
            g_haveGameCursor = true;
            return oSCur(LoadCursorA(nullptr, (LPCSTR)IDC_ARROW));
        }
        return oSCur(c);
    }

    static void ScrubRaw(PRAWINPUT ri, UINT have)
    {
        if (!ri) return;
        if (ri->header.dwType == RIM_TYPEMOUSE) {
            if (have < sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE)) return;
            ri->data.mouse.lLastX = 0;
            ri->data.mouse.lLastY = 0;
            ri->data.mouse.usButtonFlags = 0;
            ri->data.mouse.usButtonData = 0;
            ri->data.mouse.ulRawButtons = 0;
        } else if (ri->header.dwType == RIM_TYPEKEYBOARD) {
            if (have < sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD)) return;
            ri->data.keyboard.Flags |= RI_KEY_BREAK; 
        }
    }

    static UINT WINAPI HkGRID(HRAWINPUT h, UINT cmd, LPVOID p, PUINT s, UINT hdr)
    {
        void* ra = _ReturnAddress();
        UINT r = oGRID(h, cmd, p, s, hdr);
        if (p && s && cmd == RID_INPUT && r != (UINT)-1 && r >= sizeof(RAWINPUTHEADER) && Block(ra))
            ScrubRaw((PRAWINPUT)p, r);
        return r;
    }

    static UINT WINAPI HkGRIB(PRAWINPUT p, PUINT s, UINT hdr)
    {
        void* ra = _ReturnAddress();
        UINT n = oGRIB(p, s, hdr);
        if (p && s && n != (UINT)-1 && n > 0 && Block(ra)) {
            PBYTE cur = (PBYTE)p;
            PBYTE end = cur + *s;
            for (UINT i = 0; i < n && cur + sizeof(RAWINPUTHEADER) <= end; ++i) {
                PRAWINPUT ri = (PRAWINPUT)cur;
                if (ri->header.dwSize < sizeof(RAWINPUTHEADER)) break;
                if (cur + ri->header.dwSize > end) break;
                ScrubRaw(ri, ri->header.dwSize);
                cur += ri->header.dwSize;
            }
        }
        return n;
    }

    static BOOL WINAPI HkGCP2(LPPOINT p)
    {
        void* ra = _ReturnAddress();
        BOOL r = oGCP2(p);
        if (p && Block(ra)) {
            if (!g_haveFrozen && r) { g_frozen = *p; g_haveFrozen = true; }
            *p = g_frozen;
            return TRUE;
        }
        if (!overlay::IsVisible()) g_haveFrozen = false;
        return r;
    }

    static int WINAPI HkGMMEx(UINT cb, LPVOID pin, LPVOID pout, int n, DWORD res)
    {
        void* ra = _ReturnAddress();
        if (Block(ra))
            return 0;
        return oGMMEx(cb, pin, pout, n, res);
    }

    bool Init()
    {
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&SelfAnchor, &g_self);
        if (!g_self) return false;

        HMODULE hU = GetModuleHandleA("user32.dll");
        if (!hU) return false;

        struct Hook { const char* name; void* hk; void** o; };
        Hook hs[] = {
            { "GetAsyncKeyState", &HkGAS, (void**)&oGAS },
            { "GetKeyState",      &HkGKS, (void**)&oGKS },
            { "GetKeyboardState", &HkGKB, (void**)&oGKB },
            { "GetCursorPos",     &HkGCP, (void**)&oGCP },
            { "SetCursorPos",     &HkSCP, (void**)&oSCP },
            { "ClipCursor",       &HkClip, (void**)&oClip },
            { "ShowCursor",       &HkShow, (void**)&oShow },
            { "mouse_event",      &HkMEvt, (void**)&oMEvt },
            { "SendInput",        &HkSI,   (void**)&oSI },
            { "SetCursor",        &HkSCur, (void**)&oSCur },
            { "GetRawInputData",  &HkGRID, (void**)&oGRID },
            { "GetRawInputBuffer",&HkGRIB, (void**)&oGRIB },
            { "GetPhysicalCursorPos", &HkGCP2, (void**)&oGCP2 },
            { "GetMouseMovePointsEx", &HkGMMEx, (void**)&oGMMEx },
        };
        int hooked = 0;
        for (int i = 0; i < 14; ++i) {
            g_t[i] = GetProcAddress(hU, hs[i].name);
            if (g_t[i] && MH_CreateHook(g_t[i], hs[i].hk, hs[i].o) == MH_OK
                && MH_EnableHook(g_t[i]) == MH_OK)
                ++hooked;
        }
        return hooked == 14;
    }

    void Shutdown()
    {
        for (int i = 0; i < 14; ++i)
            if (g_t[i]) MH_DisableHook(g_t[i]);
        oGAS = nullptr; oGKS = nullptr; oGKB = nullptr;
        oGCP = nullptr; oSCP = nullptr; oClip = nullptr; oShow = nullptr;
        oMEvt = nullptr; oSI = nullptr; oSCur = nullptr;
        oGRID = nullptr; oGRIB = nullptr;
        oGCP2 = nullptr; oGMMEx = nullptr;
    }

    void ForceShowCursor()
    {
        if (!oShow) return;
        oShow(TRUE);
        InterlockedIncrement(&g_ourShows);
    }

    static void ResetSession()
    {
        g_hideSwallowed = g_showSwallowed = g_ourShows = 0;
        g_haveGameCursor = g_haveGameClip = false;
        g_haveFrozen = false;
    }

    void OnMenuOpened() { ResetSession(); }

    void OnMenuClosed()
    {
        if (oShow) {
            LONG hide = g_hideSwallowed + g_ourShows, show = g_showSwallowed;
            for (LONG i = 0; i < hide; ++i) oShow(FALSE);
            for (LONG i = 0; i < show; ++i) oShow(TRUE);
        }
        if (g_haveGameCursor && oSCur) oSCur(g_gameCursor);
        if (g_haveGameClip && oClip) oClip(g_gameClipNull ? nullptr : &g_gameClip);
        ResetSession();
    }
}
