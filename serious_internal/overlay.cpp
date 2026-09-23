#include "overlay.h"
#include "hack.h"
#include "esp.h"
#include "aim.h"
#include "dinput.h"
#include "wininput.h"
#include "crash.h"
#include "menu.h"

#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <cstring>

#include <GL/gl.h>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_opengl2.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace overlay {
    static bool    g_show = false;
    static bool    g_unload = false;
    static bool    g_running = false;


    struct Hooked { HWND hwnd; WNDPROC orig; };
    static Hooked  g_hooks[24];
    static int     g_hookN = 0;
    static HWND    g_hookedHwnd = nullptr; 
    static HWND    g_win32Hwnd = nullptr; 
    static bool    g_glInit = false;
    static HGLRC   g_glCtx = nullptr;
    static bool    g_inOverlay = false;
    static int     g_rescanTick = 0;
    static int     g_vpW = 0, g_vpH = 0;
    static LARGE_INTEGER g_qfreq{};
    static LARGE_INTEGER g_qprev{};

    static bool IsBtnMsg(UINT msg)
    {
        switch (msg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
            return true;
        default:
            return false;
        }
    }

    static bool IsMoveMsg(UINT msg)
    {
        return msg == WM_MOUSEMOVE || msg == WM_NCMOUSEMOVE;
    }

    static bool IsKeyMsg(UINT msg)
    {
        switch (msg) {
        case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        case WM_CHAR: case WM_DEADCHAR: case WM_SYSCHAR: case WM_SYSDEADCHAR:
        case WM_INPUT:
            return true;
        default:
            return false;
        }
    }

    static bool IsInputMsg(UINT msg)
    {
        return IsBtnMsg(msg) || IsMoveMsg(msg) || IsKeyMsg(msg) || msg == WM_SETCURSOR;
    }


    static LPARAM NormMouse(HWND from, UINT msg, LPARAM lp)
    {
        if (!g_win32Hwnd || from == g_win32Hwnd)
            return lp;
        if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
            POINT pt{ (LONG)(short)LOWORD(lp), (LONG)(short)HIWORD(lp) };
            if (ScreenToClient(g_win32Hwnd, &pt))
                return MAKELPARAM(pt.x, pt.y);
            return lp;
        }
        POINT pt{ (LONG)(short)LOWORD(lp), (LONG)(short)HIWORD(lp) };
        if (MapWindowPoints(from, g_win32Hwnd, &pt, 1))
            return MAKELPARAM(pt.x, pt.y);
        return lp;
    }

    static WNDPROC OrigFor(HWND hwnd)
    {
        for (int i = 0; i < g_hookN; ++i)
            if (g_hooks[i].hwnd == hwnd) return g_hooks[i].orig;
        return nullptr;
    }

    static LRESULT ChainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        WNDPROC orig = OrigFor(hwnd);
        bool uni = IsWindowUnicode(hwnd) != FALSE;
        if (orig)
            return uni ? CallWindowProcW(orig, hwnd, msg, wp, lp)
                       : CallWindowProcA(orig, hwnd, msg, wp, lp);
        return uni ? DefWindowProcW(hwnd, msg, wp, lp)
                   : DefWindowProcA(hwnd, msg, wp, lp);
    }

    static LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        LPARAM hlp = (IsBtnMsg(msg) || IsMoveMsg(msg)) ? NormMouse(hwnd, msg, lp) : lp;
        if (g_glInit)
            ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, hlp);

        if (g_show && IsInputMsg(msg))
            return 1;

        return ChainProc(hwnd, msg, wp, lp);
    }

    static void HookOne(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd))
            return;
        for (int i = 0; i < g_hookN; ++i)
            if (g_hooks[i].hwnd == hwnd) return;
        if (g_hookN >= 24)
            return;
        LONG_PTR cur = GetWindowLongPtrA(hwnd, GWLP_WNDPROC);
        if (!cur || cur == (LONG_PTR)&HookedWndProc)
            return;
        if (!SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)&HookedWndProc))
            return;
        g_hooks[g_hookN].hwnd = hwnd;
        g_hooks[g_hookN].orig = (WNDPROC)cur;
        ++g_hookN;
        if (!g_hookedHwnd) g_hookedHwnd = hwnd;
    }

    static BOOL CALLBACK EnumChildCb(HWND hwnd, LPARAM)
    {
        HookOne(hwnd);
        return TRUE;
    }

    static BOOL CALLBACK EnumTopCb(HWND hwnd, LPARAM)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId()) return TRUE;
        if (!IsWindowVisible(hwnd)) return TRUE;
        HookOne(hwnd);
        EnumChildWindows(hwnd, EnumChildCb, 0);
        return TRUE;
    }

    static void RescanWindows()
    {
        for (int i = 0; i < g_hookN;) {
            HWND dead = g_hooks[i].hwnd;
            if (!IsWindow(dead)) {
                g_hooks[i] = g_hooks[--g_hookN];
                if (g_hookedHwnd == dead) g_hookedHwnd = nullptr;
            } else ++i;
        }
        EnumWindows(EnumTopCb, 0);
    }

    static bool IsHooked(HWND hwnd)
    {
        for (int i = 0; i < g_hookN; ++i)
            if (g_hooks[i].hwnd == hwnd) return true;
        return false;
    }

    static void EnsureCursor()
    {
        ClipCursor(nullptr);
        CURSORINFO ci{};
        ci.cbSize = sizeof(ci);
        if (GetCursorInfo(&ci) && !(ci.flags & CURSOR_SHOWING))
            wininput::ForceShowCursor();
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
    }

    void OnSwapBuffers(void* hdc)
    {
        if (!g_running || g_inOverlay)
            return;
        crash::WatchdogTick();
        HGLRC ctx = wglGetCurrentContext();
        if (!ctx)
            return;

        g_inOverlay = true;

        HWND hwnd = hdc ? WindowFromDC((HDC)hdc) : nullptr;
        if (!hwnd)
            hwnd = GetForegroundWindow();

        if (hwnd && !IsHooked(hwnd))
            RescanWindows();
        else if (++g_rescanTick >= 240) {
            g_rescanTick = 0;
            RescanWindows();
        }
        if (hwnd) HookOne(hwnd);

        GLint vp[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_VIEWPORT, vp);
        int W = vp[2], H = vp[3];
        if (W <= 0 || H <= 0) { g_inOverlay = false; return; }

        dinput::Poll(); 

        if (!g_glInit) {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            ImGui::StyleColorsDark();
            menu::InitFonts(); 
            ImGui_ImplWin32_Init(hwnd ? (void*)hwnd : (void*)GetDesktopWindow());
            ImGui_ImplOpenGL2_Init();
            g_glCtx = ctx;
            g_win32Hwnd = hwnd;
            g_glInit = true;
            QueryPerformanceFrequency(&g_qfreq);
            QueryPerformanceCounter(&g_qprev);
        } else {
            if (ctx != g_glCtx) {
                ImGui_ImplOpenGL2_DestroyDeviceObjects();
                g_glCtx = ctx;
            }
            if (hwnd && hwnd != g_win32Hwnd) {
                ImGui_ImplWin32_Shutdown();
                ImGui_ImplWin32_Init((void*)hwnd);
                ImGui_ImplOpenGL2_DestroyDeviceObjects();
                g_win32Hwnd = hwnd;
                g_glCtx = ctx;
            }
        }

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)W, (float)H);

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        if (g_qfreq.QuadPart > 0) {
            double dt = (double)(now.QuadPart - g_qprev.QuadPart) / (double)g_qfreq.QuadPart;
            if (dt < 0.001) dt = 0.001;
            if (dt > 0.1) dt = 0.1;
            io.DeltaTime = (float)dt;
        }
        g_qprev = now;

        if (g_vpW && (W != g_vpW || H != g_vpH))
            ImGui_ImplOpenGL2_DestroyDeviceObjects();
        g_vpW = W; g_vpH = H;

        if (!g_show) { g_inOverlay = false; return; }

        EnsureCursor();


        GLint prevMode = GL_MODELVIEW;
        glGetIntegerv(GL_MATRIX_MODE, &prevMode);
        glPushAttrib(GL_CURRENT_BIT);
        glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplWin32_NewFrame();

        {
            ImGuiIO& pio = ImGui::GetIO();
            HWND mapWnd = g_win32Hwnd ? g_win32Hwnd : hwnd;
            POINT pt{};
            if (mapWnd && GetCursorPos(&pt) && ScreenToClient(mapWnd, &pt))
                pio.AddMousePosEvent((float)pt.x, (float)pt.y);
            pio.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
            pio.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
            pio.AddMouseButtonEvent(2, (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
            pio.AddMouseButtonEvent(3, (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0);
            pio.AddMouseButtonEvent(4, (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0);
        }
        ImGui::NewFrame();
        menu::Draw();
        ImGui::Render();
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

        glPopClientAttrib();
        glPopAttrib();
        glMatrixMode((GLenum)prevMode);

        g_inOverlay = false;
    }

    bool Init()
    {
        g_running = true;
        return true; 
    }

    void Shutdown()
    {
        g_running = false;
        if (g_show) SetShow(false); 
        for (int i = 0; i < g_hookN; ++i) {
            if (IsWindow(g_hooks[i].hwnd) && g_hooks[i].orig)
                SetWindowLongPtrA(g_hooks[i].hwnd, GWLP_WNDPROC, (LONG_PTR)g_hooks[i].orig);
        }
        g_hookN = 0;
        g_hookedHwnd = nullptr;
        if (g_glInit) {
            ImGui::GetIO().DisplaySize = ImVec2(0, 0);
            ImGui_ImplOpenGL2_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            menu::ResetFonts();
            g_glInit = false;
            g_glCtx = nullptr;
            g_win32Hwnd = nullptr;
            g_vpW = g_vpH = 0;
        }
    }

    void Toggle() { SetShow(!g_show); }
    void SetShow(bool s)
    {
        if (s == g_show) return;
        g_show = s;
        if (s) wininput::OnMenuOpened();
        else wininput::OnMenuClosed();
    }
    bool IsVisible() { return g_show; }
    bool ShouldUnload() { return g_unload; }
    void RequestUnload() { g_unload = true; }
    void* HookedHwnd() { return g_hookedHwnd; }
}
