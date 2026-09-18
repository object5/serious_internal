#pragma once

#include <Windows.h>

namespace menu {
    bool Init();
    void Shutdown();
    void Toggle();
    bool IsVisible();
    bool ShouldUnload();
}
