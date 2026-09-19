#pragma once
#include <cstdint>

namespace aim {
    extern bool  g_enabled;    
    extern int   g_key;        
    extern float g_fov;        
    extern float g_smooth;    
    extern float g_maxDist;    
    extern float g_aimHeight;  
    extern float g_eyeH;      
    extern bool  g_hideStaged; 
    extern bool  g_turnBody;   
    extern uintptr_t g_target; 

    void Tick(); 
}
