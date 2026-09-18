#include "pch.h"
#include "esp.h"
#include "hack.h"

#include <Windows.h>
#include <GL/gl.h>
#include <cmath>

#include "MinHook.h"

namespace esp {
    bool  g_enabled = false;
    float g_fov = 90.0f;
    float g_eyeH = 1.6f;
    int   g_drawn = 0;
    float g_maxDist = 150.0f;
    bool  g_hideStaged = true;
    int   g_cached = 0;
    int   g_wglCalls = 0;
    int   g_gdiCalls = 0;
    int   g_vpW = 0, g_vpH = 0;

    typedef BOOL(WINAPI* SwapBuffersFn)(HDC);
    static SwapBuffersFn oWgl = nullptr;
    static SwapBuffersFn oGdi = nullptr;
    static bool g_inRender = false;

    struct Vec { float x, y, z; };
    static float Dot(const Vec& a, const Vec& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }


    template <typename T>
    static bool readMem(uintptr_t addr, T& v)
    {
        if (!addr) return false;
        SIZE_T done = 0;
        return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, &v, sizeof v, &done)
            && done == sizeof v;
    }

    static bool PlayerBasis(uintptr_t player, Vec& fwd, Vec& right, Vec& up)
    {
        float m[9];
        SIZE_T done = 0;
        if (!player || !ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(player + 0x3C),
                m, sizeof m, &done) || done != sizeof m)
            return false;
        right = { m[0], m[3], m[6] };
        up    = { m[1], m[4], m[7] };
        fwd   = { -m[2], -m[5], -m[8] }; 
        return true;
    }

    static bool Project(const Vec& eye, const Vec& fwd, const Vec& right, const Vec& up,
        const Vec& p, int W, int H, float fovDeg, float& sx, float& sy, float& dist)
    {
        Vec d{ p.x - eye.x, p.y - eye.y, p.z - eye.z };
        float z = Dot(d, fwd);
        if (z < 0.5f) return false;
        float x = Dot(d, right);
        float y = Dot(d, up);
        float focal = (H * 0.5f) / tanf(fovDeg * 0.5f * 3.14159265f / 180.0f);
        sx = W * 0.5f + focal * (x / z);
        sy = H * 0.5f - focal * (y / z);
        dist = sqrtf(x * x + y * y + z * z);
        return true;
    }

    static void Line(float x1, float y1, float x2, float y2)
    {
        glVertex2f(x1, y1); glVertex2f(x2, y2);
    }

    static float ClampF(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }


    static uintptr_t g_cache[1024];
    static int g_cacheN = 0;
    static unsigned long long g_cacheTick = 0;

    static void RebuildCache()
    {
        g_cacheN = 0;
        int n = hack::GetEntityCount();
        for (int i = 0; i < n && g_cacheN < 1024; ++i) {
            uintptr_t e = hack::GetEntity(i);
            if (!e) continue;
            unsigned fl = hack::GetEntityFlags(e);
            if (!(fl & 8)) continue;   // ENF_ALIVE
            if (fl & 4) continue;      // ENF_DELETED 
            if (!hack::IsEnemy(e)) continue;
            g_cache[g_cacheN++] = e;
        }
        g_cached = g_cacheN;
    }

    static void RenderESP()
    {
        if (!g_enabled || g_inRender) return;
        if (!wglGetCurrentContext()) return; 
        g_inRender = true;
        g_drawn = 0;

        uintptr_t player = hack::GetLocalPlayer();
        float pp[3];
        if (player && hack::GetEntityPos(player, pp)) {
            Vec eye{ pp[0], pp[1] + g_eyeH, pp[2] };
            Vec fwd, right, up;
            if (!PlayerBasis(player, fwd, right, up)) { g_inRender = false; return; }

            GLint vp[4] = { 0, 0, 0, 0 };
            glGetIntegerv(GL_VIEWPORT, vp);
            int W = vp[2], H = vp[3];
            g_vpW = W; g_vpH = H;

            if (W > 0 && H > 0) {
                glPushAttrib(GL_ALL_ATTRIB_BITS);
                glMatrixMode(GL_PROJECTION);
                glPushMatrix();
                glLoadIdentity();
                glMatrixMode(GL_MODELVIEW);
                glPushMatrix();
                glLoadIdentity();
                glMatrixMode(GL_PROJECTION);
                glOrtho(0.0, (double)W, (double)H, 0.0, -1.0, 1.0);
                glMatrixMode(GL_MODELVIEW);
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_TEXTURE_2D);
                glDisable(GL_LIGHTING);
                glDisable(GL_FOG);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

               
                {
                    Vec tp{ eye.x + fwd.x * 10.0f, eye.y + fwd.y * 10.0f, eye.z + fwd.z * 10.0f };
                    float sx, sy, dist;
                    if (Project(eye, fwd, right, up, tp, W, H, g_fov, sx, sy, dist)) {
                        glColor3f(1.0f, 0.0f, 1.0f);
                        glBegin(GL_LINES);
                        Line(sx - 8, sy, sx + 8, sy);
                        Line(sx, sy - 8, sx, sy + 8);
                        glEnd();
                    }
                }

               
                unsigned long long now = GetTickCount64();
                if (now - g_cacheTick > 300) { RebuildCache(); g_cacheTick = now; }
                for (int i = 0; i < g_cacheN; ++i) {
                    uintptr_t e = g_cache[i];
                    if (!e || e == player) continue;
                    if (hack::GetEntityHp(e) <= 0.0f) continue;
                    if (g_hideStaged && !hack::GetEnemyTarget(e)) continue; 
                    float ep[3];
                    if (!hack::GetEntityPos(e, ep)) continue;
                    Vec feet{ ep[0], ep[1], ep[2] };
                    Vec head{ ep[0], ep[1] + 2.0f, ep[2] };
                    float fx, fy, fd, hx, hy, hd;
                    if (!Project(eye, fwd, right, up, feet, W, H, g_fov, fx, fy, fd)) continue;
                    if (!Project(eye, fwd, right, up, head, W, H, g_fov, hx, hy, hd)) continue;
                    if (fd < 1.0f) continue; 
                    if (g_maxDist > 0.0f && fd > g_maxDist) continue; 
                    float hPx = fy - hy;
                    if (hPx < 4.0f || hPx > (float)H) continue;
                    float wPx = hPx * 0.5f;
                   
                    float x1 = ClampF(fx - wPx, -50.0f, (float)W + 50.0f);
                    float x2 = ClampF(fx + wPx, -50.0f, (float)W + 50.0f);
                    float y1 = ClampF(hy, -50.0f, (float)H + 50.0f);
                    float y2 = ClampF(fy, -50.0f, (float)H + 50.0f);
                    if (fd < 12.0f) glColor3f(1.0f, 0.0f, 0.0f);
                    else glColor3f(0.0f, 1.0f, 0.0f);
                    glBegin(GL_LINES);

                    Line(x1, y1, x2, y1);
                    Line(x2, y1, x2, y2);
                    Line(x2, y2, x1, y2);
                    Line(x1, y2, x1, y1);

                    Line((float)W * 0.5f, (float)H, ClampF(fx, 0.0f, (float)W), y2);
                    glEnd();
                    ++g_drawn;
                }

                glMatrixMode(GL_PROJECTION);
                glPopMatrix();
                glMatrixMode(GL_MODELVIEW);
                glPopMatrix();
                glPopAttrib();
            }
        }
        g_inRender = false;
    }

    static BOOL WINAPI HkWgl(HDC hdc) { ++g_wglCalls; RenderESP(); return oWgl(hdc); }
    static BOOL WINAPI HkGdi(HDC hdc) { ++g_gdiCalls; RenderESP(); return oGdi(hdc); }

    bool Init()
    {
        if (MH_Initialize() != MH_OK) return false;
        bool ok = false;
        HMODULE hGL = GetModuleHandleA("opengl32.dll");
        if (!hGL) hGL = LoadLibraryA("opengl32.dll");
        if (hGL) {
            void* t = reinterpret_cast<void*>(GetProcAddress(hGL, "wglSwapBuffers"));
            if (t && MH_CreateHook(t, reinterpret_cast<void*>(&HkWgl),
                    reinterpret_cast<void**>(&oWgl)) == MH_OK
                && MH_EnableHook(t) == MH_OK) ok = true;
        }
        HMODULE hGdi = GetModuleHandleA("gdi32.dll");
        if (hGdi) {
            void* t = reinterpret_cast<void*>(GetProcAddress(hGdi, "SwapBuffers"));
            if (t && MH_CreateHook(t, reinterpret_cast<void*>(&HkGdi),
                    reinterpret_cast<void**>(&oGdi)) == MH_OK
                && MH_EnableHook(t) == MH_OK) ok = true;
        }
        return ok;
    }

    void Shutdown()
    {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        oWgl = nullptr; oGdi = nullptr;
    }
}
