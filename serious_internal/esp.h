#pragma once
#include <cstdint>

namespace esp {
    extern bool  g_enabled;
    extern float g_fov;
    extern float g_eyeH;
    extern int   g_drawn;
    extern float g_maxDist;
    extern bool  g_hideStaged;
    extern int   g_cached;
    extern int   g_hookMode;
    extern float g_boxH;
    extern float g_boxW;
    extern float g_boxHS;
    extern int   g_tgtOk;
    extern int   g_tgtAll;

    bool Init();
    void Shutdown();
}
