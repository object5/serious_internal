#pragma once

#include <Windows.h>

namespace overlay {
    bool Init();
    void Shutdown();
    void Toggle();
    void SetShow(bool s); 
    bool IsVisible();
    bool ShouldUnload();
    void RequestUnload();


    void OnSwapBuffers(void* hdc);

    void* HookedHwnd();
}
