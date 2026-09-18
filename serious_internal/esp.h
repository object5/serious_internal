#pragma once

namespace esp {
    extern bool  g_enabled;   
    extern float g_fov;       
    extern float g_eyeH;      
    extern int   g_drawn;    
    extern float g_maxDist;   
    extern bool  g_hideStaged;
    extern int   g_cached;    
    extern int   g_wglCalls;
    extern int   g_gdiCalls;
    extern int   g_vpW, g_vpH;

    bool Init();   
    void Shutdown();
}
