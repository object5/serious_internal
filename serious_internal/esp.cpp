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
    int   g_hookMode = 0;
    float g_boxH = 2.0f;
    float g_boxW = 0.57f;
    float g_boxHS = 0.8f;
    int   g_tgtOk = 0;
    int   g_tgtAll = 0;

    typedef BOOL(WINAPI* SwapBuffersFn)(HDC);
    static SwapBuffersFn oWgl = nullptr;
    static SwapBuffersFn oGdi = nullptr;
    static bool g_inRender = false;

    struct Vec { float x, y, z; };
    static float Dot(const Vec& a, const Vec& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    static Vec Norm(Vec v)
    {
        float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        if (l > 1e-6f) { v.x /= l; v.y /= l; v.z /= l; }
        return v;
    }

    struct RawCam { Vec eye, fwd, right, up; };
    static RawCam sPrevR{}, sCurrR{};
    static double sTPrev = 0, sTCurr = 0, sPeriod = 0.05;
    static bool sTickInit = false, sHasPrev = false;

    static double NowSec()
    {
        static double freq = 0;
        if (!freq) {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            freq = (double)f.QuadPart;
        }
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return (double)c.QuadPart / freq;
    }

    static bool SameCam(const RawCam& a, const RawCam& b)
    {
        return memcmp(&a, &b, sizeof a) == 0;
    }

    static void TickCamera(const Vec& eye, const Vec& fwd, const Vec& right, const Vec& up,
        Vec& oEye, Vec& oFwd, Vec& oRight, Vec& oUp)
    {
        RawCam raw{ eye, fwd, right, up };
        double now = NowSec();
        if (!sTickInit) {
            sCurrR = raw; sTCurr = now;
            sTickInit = true;
        } else if (!SameCam(raw, sCurrR)) {
            double dx = raw.eye.x - sCurrR.eye.x;
            double dy = raw.eye.y - sCurrR.eye.y;
            double dz = raw.eye.z - sCurrR.eye.z;
            double ang = acosf(fmaxf(-1.0f, fminf(1.0f,
                Dot(raw.fwd, sCurrR.fwd))));
            double dt = now - sTCurr;
            if (dx * dx + dy * dy + dz * dz > 25.0 || ang > 0.5 || dt <= 0.0) {
                sCurrR = raw; sTCurr = now;
            } else {
                sPrevR = sCurrR; sTPrev = sTCurr;
                sCurrR = raw; sTCurr = now;
                sHasPrev = true;
                if (dt > 0.008 && dt < 0.25)
                    sPeriod = sPeriod * 0.7 + dt * 0.3;
            }
        }
        double f = 1.0;
        if (sHasPrev) {
            f = (now - sTCurr) / sPeriod;
            if (f < 0.0) f = 0.0;
            if (f > 1.0 || now - sTCurr > 2.0 * sPeriod) f = 1.0;
        } else {
            oEye = sCurrR.eye; oFwd = sCurrR.fwd; oRight = sCurrR.right; oUp = sCurrR.up;
            return;
        }
        float t = (float)f;
        oEye.x = (float)(sPrevR.eye.x + (sCurrR.eye.x - sPrevR.eye.x) * t);
        oEye.y = (float)(sPrevR.eye.y + (sCurrR.eye.y - sPrevR.eye.y) * t);
        oEye.z = (float)(sPrevR.eye.z + (sCurrR.eye.z - sPrevR.eye.z) * t);
        Vec lf{
            (float)(sPrevR.fwd.x + (sCurrR.fwd.x - sPrevR.fwd.x) * t),
            (float)(sPrevR.fwd.y + (sCurrR.fwd.y - sPrevR.fwd.y) * t),
            (float)(sPrevR.fwd.z + (sCurrR.fwd.z - sPrevR.fwd.z) * t) };
        Vec lr{
            (float)(sPrevR.right.x + (sCurrR.right.x - sPrevR.right.x) * t),
            (float)(sPrevR.right.y + (sCurrR.right.y - sPrevR.right.y) * t),
            (float)(sPrevR.right.z + (sCurrR.right.z - sPrevR.right.z) * t) };
        oFwd = Norm(lf);
        Vec r{ lr.x - oFwd.x * Dot(lr, oFwd),
               lr.y - oFwd.y * Dot(lr, oFwd),
               lr.z - oFwd.z * Dot(lr, oFwd) };
        oRight = Norm(r);
        oUp = { oRight.y * oFwd.z - oRight.z * oFwd.y,
                oRight.z * oFwd.x - oRight.x * oFwd.z,
                oRight.x * oFwd.y - oRight.y * oFwd.x };
    }

    struct EntLerp { uintptr_t e = 0; Vec prev{}, curr{}; double tPrev = 0, tCurr = 0, period = 0.05; bool init = false; };
    static EntLerp sEnt[1024];

    static Vec LerpEntPos(uintptr_t e, const Vec& raw)
    {
        double now = NowSec();
        EntLerp& s = sEnt[(e >> 4) & 1023]; // entpos history 
        if (!s.init || s.e != e) {
            s.e = e; s.prev = raw; s.curr = raw;
            s.tPrev = now; s.tCurr = now; s.period = 0.05;  s.init = true; // 20hz period
            return raw;
        }
        float dx = raw.x - s.curr.x, dy = raw.y - s.curr.y, dz = raw.z - s.curr.z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 1e-10f) { // пять метров за раз если двинулся то это реюз адреса или респавн яхз
            double dt = now - s.tCurr;
            if (d2 > 25.0f || dt <= 0.0) {
                s.prev = raw; s.curr = raw; s.tPrev = now; s.tCurr = now;
                return raw;
            }
            s.prev = s.curr; s.tPrev = s.tCurr;
            s.curr = raw; s.tCurr = now;
            if (dt > 0.008 && dt < 0.25)
                s.period = s.period * 0.7 + dt * 0.3;
        }
        double f = (now - s.tCurr) / s.period;
        if (f < 0.0) f = 0.0;
        if (f > 1.0 || now - s.tCurr > 2.0 * s.period) f = 1.0;
        float t = (float)f;
        return { s.prev.x + (s.curr.x - s.prev.x) * t,
                 s.prev.y + (s.curr.y - s.prev.y) * t,
                 s.prev.z + (s.curr.z - s.prev.z) * t }; // линейная интерполяция от прошлой к настоящей
    }

    template <typename T>
    static __forceinline bool readMem(uintptr_t addr, T& v)
    {
        if (!addr) return false;
        __try {
            memcpy(&v, (const void*)addr, sizeof v);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            v = T{};
            return false;
        }
    }

    static __forceinline bool readBuf(uintptr_t addr, void* buf, SIZE_T size)
    {
        if (!addr || !buf || !size) return false;
        __try {
            memcpy(buf, (const void*)addr, size);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool PlayerBasis(uintptr_t player, Vec& fwd, Vec& right, Vec& up)
    {
        float m[9];
        if (!player || !readBuf(player + 0x3C, m, sizeof m))
            return false;
        right = { m[0], m[3], m[6] };
        up    = { m[1], m[4], m[7] };
        fwd   = { -m[2], -m[5], -m[8] };
        return true;
    }

    static bool ViewBasis(uintptr_t player, Vec& fwd, Vec& right, Vec& up)
    {
        float yaw = 0, pitch = 0;
#ifdef _WIN64
        float bodyH = 0, relH = 0;
        if (!readMem(player + 0x30, bodyH)) return false;
        if (!readMem(player + 0x3CC, relH)) return false;
        if (!readMem(player + 0x3D0, pitch)) return false;
        yaw = bodyH + relH;
#else
        float bodyH = 0, relH = 0;
        if (!readMem(player + 0x2C, bodyH)) return false;
        if (!readMem(player + 0x35C, relH)) return false;
        if (!readMem(player + 0x360, pitch)) return false;
        yaw = bodyH + relH;
#endif
        const float r = 3.14159265f / 180.0f;
        float h = yaw * r, p = pitch * r;
        float ch = cosf(h), sh = sinf(h), cp = cosf(p), sp = sinf(p);
        fwd   = { -sh * cp, sp, -ch * cp };
        right = { ch, 0.0f, -sh };
        up.x = right.y * fwd.z - right.z * fwd.y;
        up.y = right.z * fwd.x - right.x * fwd.z;
        up.z = right.x * fwd.y - right.y * fwd.x;
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
        float focal = ((float)W * 0.5f) / tanf(fovDeg * 0.5f * 3.14159265f / 180.0f);
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
        g_tgtOk = 0;
        g_tgtAll = 0;
        int n = hack::GetEntityCount();
        for (int i = 0; i < n && g_cacheN < 1024; ++i) {
            uintptr_t e = hack::GetEntity(i);
            if (!e) continue;
            unsigned fl = hack::GetEntityFlags(e);
            if (!(fl & 8)) continue;
            if (fl & 4) continue;
            if (!hack::IsEnemy(e)) continue;
            g_cache[g_cacheN++] = e;
            ++g_tgtAll;
            if (hack::GetEnemyTarget(e)) ++g_tgtOk;
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
            if (!ViewBasis(player, fwd, right, up)
                && !PlayerBasis(player, fwd, right, up)) { g_inRender = false; return; }
            Vec sEye, sFwd, sRight, sUp;
            TickCamera(eye, fwd, right, up, sEye, sFwd, sRight, sUp);
            eye = sEye; fwd = sFwd; right = sRight; up = sUp;

            GLint vp[4] = { 0, 0, 0, 0 };
            glGetIntegerv(GL_VIEWPORT, vp);
            int W = vp[2], H = vp[3];

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

                unsigned long long now = GetTickCount64();
                int curN = hack::GetEntityCount();
                static int lastN = -1;
                if (curN != lastN || now - g_cacheTick > 1000) {
                    RebuildCache();
                    g_cacheTick = now;
                    lastN = curN;
                }
                struct Cand { uintptr_t e; float x1, y1, x2, y2, fd; };
                static Cand cands[1024];
                int candN = 0;
                for (int i = 0; i < g_cacheN && candN < 1024; ++i) {
                    uintptr_t e = g_cache[i];
                    if (!e || e == player) continue;
                    if (hack::GetEntityHp(e) <= 0.0f) continue;
                    if (g_hideStaged && !hack::GetEnemyTarget(e)) continue;
                    float ep[3];
                    if (!hack::GetEntityPos(e, ep)) continue;
                    {
                        Vec sm = LerpEntPos(e, { ep[0], ep[1], ep[2] });
                        ep[0] = sm.x; ep[1] = sm.y; ep[2] = sm.z;
                    }
                    float m[9];
                    bool hasM = hack::GetEntityMatrix(e, m);
                    float mn[3], mx[3];
                    bool hasBox = hack::GetEntityBox(e, mn, mx);
                    float lx[2], ly[2], lz[2];
                    if (hasBox) {
                        lx[0] = mn[0]; lx[1] = mx[0];
                        ly[0] = mn[1]; ly[1] = mx[1];
                        lz[0] = mn[2]; lz[1] = mx[2];
                    } else {
                        float half = g_boxH * 0.5f;
                        lx[0] = lx[1] = 0.0f;
                        ly[0] = -half; ly[1] = half;
                        lz[0] = lz[1] = 0.0f;
                    }
                    float ax0 = 1e9f, ay0 = 1e9f, ax1 = -1e9f, ay1 = -1e9f;
                    float cd = 0.0f;
                    int okN = 0;
                    for (int cx = 0; cx < 2; ++cx)
                    for (int cy = 0; cy < 2; ++cy)
                    for (int cz = 0; cz < 2; ++cz) {
                        Vec w;
                        if (hasM) {
                            w = { ep[0] + m[0] * lx[cx] + m[3] * ly[cy] + m[6] * lz[cz],
                                  ep[1] + m[1] * lx[cx] + m[4] * ly[cy] + m[7] * lz[cz],
                                  ep[2] + m[2] * lx[cx] + m[5] * ly[cy] + m[8] * lz[cz] };
                        } else {
                            w = { ep[0] + lx[cx], ep[1] + ly[cy], ep[2] + lz[cz] };
                        }
                        float sx, sy, dd;
                        if (!Project(eye, fwd, right, up, w, W, H, g_fov, sx, sy, dd)) continue;
                        if (sx < ax0) ax0 = sx; if (sx > ax1) ax1 = sx;
                        if (sy < ay0) ay0 = sy; if (sy > ay1) ay1 = sy;
                        cd += dd; ++okN;
                    }
                    if (!okN) continue;
                    cd /= okN;
                    if (cd < 1.0f) continue;
                    if (g_maxDist > 0.0f && cd > g_maxDist) continue;
                    float rectH = ay1 - ay0;
                    if (!hasBox && rectH > 0.0f) {
                        float cxm = (ax0 + ax1) * 0.5f;
                        ax0 = cxm - rectH * 0.5f; ax1 = cxm + rectH * 0.5f;
                    }
                    if (rectH < 4.0f || rectH > (float)H) continue;
                    {
                        float cym = (ay0 + ay1) * 0.5f;
                        float h = rectH * g_boxHS;
                        ay0 = cym - h * 0.5f; ay1 = cym + h * 0.5f;
                        rectH = h;
                    }
                    {
                        float cxm = (ax0 + ax1) * 0.5f;
                        float maxW = rectH * g_boxW;
                        if (ax1 - ax0 > maxW) { ax0 = cxm - maxW * 0.5f; ax1 = cxm + maxW * 0.5f; }
                    }
                    Cand& c = cands[candN];
                    c.e = e;
                    c.x1 = ax0; c.y1 = ay0; c.x2 = ax1; c.y2 = ay1; c.fd = cd;
                    ++candN;
                }
                for (int i = 0; i < candN; ++i) {
                    const Cand& c = cands[i];
                    float cxm = ClampF((c.x1 + c.x2) * 0.5f, 0.0f, (float)W);
                    float x1 = ClampF(c.x1, -50.0f, (float)W + 50.0f);
                    float x2 = ClampF(c.x2, -50.0f, (float)W + 50.0f);
                    float y1 = ClampF(c.y1, -50.0f, (float)H + 50.0f);
                    float y2 = ClampF(c.y2, -50.0f, (float)H + 50.0f);
                    if (c.fd < 12.0f) glColor3f(1.0f, 0.0f, 0.0f);
                    else glColor3f(0.0f, 1.0f, 0.0f);
                    glBegin(GL_LINES);
                    Line(x1, y1, x2, y1);
                    Line(x2, y1, x2, y2);
                    Line(x2, y2, x1, y2);
                    Line(x1, y2, x1, y1);
                    Line((float)W * 0.5f, (float)H, cxm, y2);
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

    static BOOL WINAPI HkWgl(HDC hdc) { if (g_hookMode == 0 || g_hookMode == 1) RenderESP(); return oWgl(hdc); }
    static BOOL WINAPI HkGdi(HDC hdc) { if (g_hookMode == 0 || g_hookMode == 2) RenderESP(); return oGdi(hdc); }

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
