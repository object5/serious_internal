#include "dinput.h"
#include "overlay.h"

#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>
#include <objbase.h>
#include <cstdio>


namespace dinput {

    static constexpr int kSlotGetDeviceState = 9;
    static constexpr int kSlotGetDeviceData = 10;
    static constexpr int kMaxTables = 8;

    typedef HRESULT(WINAPI* GetStateFn)(IDirectInputDevice8A*, DWORD, LPVOID);
    typedef HRESULT(WINAPI* GetDataFn)(IDirectInputDevice8A*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
    typedef HRESULT(WINAPI* DICreateFn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    typedef HRESULT(WINAPI* DI7CreateFn)(HINSTANCE, DWORD, LPDIRECTINPUTA*, LPUNKNOWN);

    struct VtPatch { void** vt; void* oState; void* oData; };
    static VtPatch g_vt[kMaxTables];
    static int  g_vtN = 0;
    static int  g_v8N = 0;
    static int  g_v7N = 0;
    static bool g_v8done = false;
    static bool g_v7done = false;
    static bool g_comInit = false;
    static bool g_comOurs = false;

    static void* FindOrig(void** vt, int slot)
    {
        for (int i = 0; i < g_vtN; ++i)
            if (g_vt[i].vt == vt)
                return slot == kSlotGetDeviceState ? g_vt[i].oState : g_vt[i].oData;
        return nullptr;
    }

    static HRESULT WINAPI HkGetState(IDirectInputDevice8A* self, DWORD cb, LPVOID pv)
    {
        GetStateFn o = (GetStateFn)FindOrig(*(void***)self, kSlotGetDeviceState);
        HRESULT hr = o ? o(self, cb, pv) : E_FAIL;
        if (SUCCEEDED(hr) && pv && cb && overlay::IsVisible())
            memset(pv, 0, cb);
        return hr;
    }

    static HRESULT WINAPI HkGetData(IDirectInputDevice8A* self, DWORD cb, LPDIDEVICEOBJECTDATA d, LPDWORD n, DWORD f)
    {
        if (overlay::IsVisible()) {
            if (n) *n = 0;
            return DI_OK;
        }
        GetDataFn o = (GetDataFn)FindOrig(*(void***)self, kSlotGetDeviceData);
        return o ? o(self, cb, d, n, f) : E_FAIL;
    }

    static bool PatchTable(void** vt, bool v7)
    {
        if (!vt) return false;
        for (int i = 0; i < g_vtN; ++i)
            if (g_vt[i].vt == vt) return true; 
        if (g_vtN >= kMaxTables) return false;
        DWORD old = 0;
        if (!VirtualProtect(&vt[kSlotGetDeviceState], sizeof(void*) * 2, PAGE_READWRITE, &old))
            return false;
        g_vt[g_vtN].vt = vt;
        g_vt[g_vtN].oState = vt[kSlotGetDeviceState];
        g_vt[g_vtN].oData = vt[kSlotGetDeviceData];
        vt[kSlotGetDeviceState] = (void*)&HkGetState;
        vt[kSlotGetDeviceData] = (void*)&HkGetData;
        DWORD tmp = 0;
        VirtualProtect(&vt[kSlotGetDeviceState], sizeof(void*) * 2, old, &tmp);
        ++g_vtN;
        if (v7) ++g_v7N; else ++g_v8N;
        return true;
    }


    static void Harvest(IDirectInput8A* di, bool v7)
    {
        static const GUID* const guids[] = { &GUID_SysMouse, &GUID_SysKeyboard, &GUID_Joystick };
        for (int i = 0; i < 3; ++i) {
            IDirectInputDevice8A* dev = nullptr;
            if (SUCCEEDED(di->CreateDevice(*guids[i], &dev, nullptr)) && dev) {
                PatchTable(*(void***)dev, v7);
                dev->Release();
            }
        }
    }

    static void TryV8()
    {
        if (g_v8done) return;
        HMODULE hDI = GetModuleHandleA("dinput8.dll");
        if (!hDI) return; 

        DICreateFn pCreate = (DICreateFn)GetProcAddress(hDI, "DirectInput8CreateA");
        if (pCreate) {
            IDirectInput8A* di = nullptr;
            if (SUCCEEDED(pCreate(GetModuleHandleA(nullptr), DIRECTINPUT_VERSION,
                                 IID_IDirectInput8A, (void**)&di, nullptr)) && di) {
                Harvest(di, false);
                di->Release();
            }
        } else {
           
            static bool triedCOM = false;
            if (!triedCOM) {
                triedCOM = true;
                if (!g_comInit) {
                    HRESULT cr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                    g_comInit = true;
                    g_comOurs = (cr == S_OK || cr == S_FALSE); 
                }
                IDirectInput8A* di = nullptr;
                if (SUCCEEDED(CoCreateInstance(CLSID_DirectInput8, nullptr, CLSCTX_INPROC_SERVER,
                                               IID_IDirectInput8A, (void**)&di)) && di) {
                    Harvest(di, false);
                    di->Release();
                }
            }
        }
      
        static HMODULE sys8 = nullptr;
        if (g_v8N == 0 && !sys8) {
            char sys[MAX_PATH] = {}, full[MAX_PATH] = {};
            GetSystemDirectoryA(sys, sizeof sys - 1);
            sprintf_s(full, "%s\\dinput8.dll", sys);
            sys8 = LoadLibraryA(full);
        }
        if (g_v8N == 0 && sys8) {
            DICreateFn pS = (DICreateFn)GetProcAddress(sys8, "DirectInput8CreateA");
            if (pS) {
                IDirectInput8A* di = nullptr;
                if (SUCCEEDED(pS(GetModuleHandleA(nullptr), DIRECTINPUT_VERSION,
                                 IID_IDirectInput8A, (void**)&di, nullptr)) && di) {
                    Harvest(di, false);
                    di->Release();
                }
            }
        }
        if (g_v8N > 0) g_v8done = true;
    }

    static void TryV7()
    {
        if (g_v7done) return;
        HMODULE h7 = GetModuleHandleA("dinput.dll");
        if (!h7) return;

        DI7CreateFn pCreate = (DI7CreateFn)GetProcAddress(h7, "DirectInputCreateA");
        if (pCreate) {
            LPDIRECTINPUTA di = nullptr;
          
            if (SUCCEEDED(pCreate(GetModuleHandleA(nullptr), 0x0700, &di, nullptr)) && di) {
                
                static const GUID* const guids[] = { &GUID_SysMouse, &GUID_SysKeyboard, &GUID_Joystick };
                for (int i = 0; i < 3; ++i) {
                    LPDIRECTINPUTDEVICEA dev = nullptr;
                    if (SUCCEEDED(di->CreateDevice(*guids[i], &dev, nullptr)) && dev) {
                        PatchTable(*(void***)dev, true);
                        dev->Release();
                    }
                }
                di->Release();
            }
        }
       
        static bool triedCOM7 = false;
        if (g_v7N == 0 && !triedCOM7) {
            triedCOM7 = true;
            if (!g_comInit) {
                HRESULT cr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                g_comInit = true;
                g_comOurs = (cr == S_OK || cr == S_FALSE);
            }
            LPDIRECTINPUT7A d7 = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_DirectInput, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_IDirectInput7A, (void**)&d7)) && d7) {
                static const GUID* const guids7[] = { &GUID_SysMouse, &GUID_SysKeyboard, &GUID_Joystick };
                for (int i = 0; i < 3; ++i) {
                    LPDIRECTINPUTDEVICEA dev = nullptr;
                    if (SUCCEEDED(d7->CreateDevice(*guids7[i], &dev, nullptr)) && dev) {
                        PatchTable(*(void***)dev, true);
                        dev->Release();
                    }
                }
                d7->Release();
            }
        }
        static HMODULE sys7 = nullptr;
        if (g_v7N == 0 && !sys7) {
            char sys[MAX_PATH] = {}, full[MAX_PATH] = {};
            GetSystemDirectoryA(sys, sizeof sys - 1);
            sprintf_s(full, "%s\\dinput.dll", sys);
            sys7 = LoadLibraryA(full);
        }
        if (g_v7N == 0 && sys7) {
            DI7CreateFn pS = (DI7CreateFn)GetProcAddress(sys7, "DirectInputCreateA");
            if (pS) {
                LPDIRECTINPUTA d2 = nullptr;
                if (SUCCEEDED(pS(GetModuleHandleA(nullptr), 0x0700, &d2, nullptr)) && d2) {
                    static const GUID* const guids2[] = { &GUID_SysMouse, &GUID_SysKeyboard, &GUID_Joystick };
                    for (int i = 0; i < 3; ++i) {
                        LPDIRECTINPUTDEVICEA dev = nullptr;
                        if (SUCCEEDED(d2->CreateDevice(*guids2[i], &dev, nullptr)) && dev) {
                            PatchTable(*(void***)dev, true);
                            dev->Release();
                        }
                    }
                    d2->Release();
                }
            }
        }
        if (g_v7N > 0) g_v7done = true;
    }

    void Poll()
    {
        TryV8();
        TryV7();
    }

    bool Init()
    {
        Poll();
        return g_vtN > 0;
    }

    void Shutdown()
    {

        g_comInit = g_comOurs = false;
        for (int i = 0; i < g_vtN; ++i) {
            DWORD old = 0;
            if (VirtualProtect(&g_vt[i].vt[kSlotGetDeviceState], sizeof(void*) * 2, PAGE_READWRITE, &old)) {
                g_vt[i].vt[kSlotGetDeviceState] = g_vt[i].oState;
                g_vt[i].vt[kSlotGetDeviceData] = g_vt[i].oData;
                DWORD tmp = 0;
                VirtualProtect(&g_vt[i].vt[kSlotGetDeviceState], sizeof(void*) * 2, old, &tmp);
            }
        }
        g_vtN = g_v8N = g_v7N = 0;
    }
}
