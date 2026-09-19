#include "aim.h"
#include "hack.h"

#include <Windows.h>
#include <cmath>

namespace aim {
    bool  g_enabled   = false;
    int   g_key       = VK_RBUTTON;
    float g_fov       = 30.0f;
    float g_smooth    = 3.0f;
    float g_maxDist   = 150.0f;
    float g_aimHeight = 0.0f;
    float g_eyeH      = 1.6f;
    bool  g_hideStaged = true;
    bool  g_turnBody   = false;
    uintptr_t g_target = 0;

    static constexpr float kPi = 3.14159265f;

    static float NormAngle(float a)
    {
        while (a > 180.0f) a -= 360.0f;
        while (a < -180.0f) a += 360.0f;
        return a;
    }

    static float ClampF(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    static bool AimPoint(uintptr_t e, float out[3])
    {
        float ep[3];
        if (!hack::GetEntityPos(e, ep)) return false;

        float m[9], mn[3], mx[3];
        bool hasM = hack::GetEntityMatrix(e, m);
        bool hasBox = hack::GetEntityBox(e, mn, mx);
        if (hasM && hasBox) {
            float cx = (mn[0] + mx[0]) * 0.5f;
            float cy = (mn[1] + mx[1]) * 0.5f;
            float cz = (mn[2] + mx[2]) * 0.5f;
            out[0] = ep[0] + m[0] * cx + m[3] * cy + m[6] * cz;
            out[1] = ep[1] + m[1] * cx + m[4] * cy + m[7] * cz;
            out[2] = ep[2] + m[2] * cx + m[5] * cy + m[8] * cz;
        } else {
            out[0] = ep[0];
            out[1] = ep[1] + 1.0f; 
            out[2] = ep[2];
        }
        out[1] += g_aimHeight;
        return true;
    }

    void Tick()
    {
        g_target = 0;
        if (!g_enabled) return;
        if (g_key != 0 && !(GetAsyncKeyState(g_key) & 0x8000)) return;

        uintptr_t player = hack::GetLocalPlayer();
        if (!player) return;

        float eye[3];
        if (!hack::GetEntityPos(player, eye)) return;
        eye[1] += g_eyeH;

        float curYaw = 0, curPitch = 0;
        if (!hack::GetViewAngles(curYaw, curPitch)) return;

      
        float hr = curYaw * kPi / 180.0f, pr = curPitch * kPi / 180.0f;
        float ch = cosf(hr), sh = sinf(hr), cp = cosf(pr), sp = sinf(pr);
        float fwd[3] = { -sh * cp, sp, -ch * cp };

        float cosLim = cosf(ClampF(g_fov, 1.0f, 180.0f) * 0.5f * kPi / 180.0f);

        int n = hack::GetEntityCount();
        uintptr_t best = 0;
        float bestCos = cosLim, bestDist = 1e9f;
        float bestYaw = 0, bestPitch = 0;

        for (int i = 0; i < n; ++i) {
            uintptr_t e = hack::GetEntity(i);
            if (!e || e == player) continue;
            unsigned fl = hack::GetEntityFlags(e);
            if (!(fl & 8)) continue; // ENF_ALIVE
            if (fl & 4) continue;
            if (!hack::IsEnemy(e)) continue;
            if (g_hideStaged && !hack::GetEnemyTarget(e)) continue;
            if (hack::GetEntityHp(e) <= 0.0f) continue;

            float tp[3];
            if (!AimPoint(e, tp)) continue;

            float dx = tp[0] - eye[0], dy = tp[1] - eye[1], dz = tp[2] - eye[2];
            float dist = sqrtf(dx * dx + dy * dy + dz * dz);
            if (dist < 0.5f) continue;
            if (g_maxDist > 0.0f && dist > g_maxDist) continue;

            float inv = 1.0f / dist;
            float cosA = (dx * fwd[0] + dy * fwd[1] + dz * fwd[2]) * inv;
            if (cosA < cosLim) continue; 
            if (cosA > bestCos + 1e-6f || (fabsf(cosA - bestCos) < 1e-6f && dist < bestDist)) {
                float yaw = atan2f(-dx, -dz) * 180.0f / kPi;
                float pitch = asinf(ClampF(dy * inv, -1.0f, 1.0f)) * 180.0f / kPi;
                best = e;
                bestCos = cosA;
                bestDist = dist;
                bestYaw = yaw;
                bestPitch = pitch;
            }
        }

        if (!best) return;
        g_target = best;

        float smooth = g_smooth < 1.0f ? 1.0f : g_smooth;
        float dYaw = NormAngle(bestYaw - curYaw);
        float dPitch = ClampF(bestPitch, -89.0f, 89.0f) - ClampF(curPitch, -89.0f, 89.0f);
        float newYaw = curYaw + dYaw / smooth;
        float newPitch = ClampF(ClampF(curPitch, -89.0f, 89.0f) + dPitch / smooth, -89.0f, 89.0f);

        if (g_turnBody)
            hack::SetViewAnglesBody(newYaw, newPitch);
        else
            hack::SetViewAngles(newYaw, newPitch);
    }
}
