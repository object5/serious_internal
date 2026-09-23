#include "hack.h"
#include "overlay.h"
#include "esp.h"
#include "aim.h"
#include "dinput.h"
#include "wininput.h"
#include "crash.h"

static HMODULE g_hMod = nullptr;

static DWORD WINAPI MainThread(LPVOID)
{
    crash::Init(); 
    bool menuOk = overlay::Init();
    bool espOk = esp::Init();
    dinput::Init();
    wininput::Init();

    OutputDebugStringA("[serious_internal] loaded\n");

    for (;;) {
        Sleep(5);
        crash::WatchdogCheck();

        if ((GetAsyncKeyState(VK_INSERT) & 1) && menuOk)
            overlay::Toggle();

        if ((GetAsyncKeyState(VK_END) & 1) || overlay::ShouldUnload())
            break;

        if (GetAsyncKeyState(VK_F5) & 1)
            hack::SetGodMode(!hack::IsGodMode());

        if (GetAsyncKeyState(VK_F7) & 1)
            hack::SetRapidFire(!hack::IsRapidFire());


        if (!overlay::IsVisible()) {
            hack::RapidFireTick();
            aim::Tick();
        }

        if (hack::IsGodMode()) {
            float hp = hack::GetHealth();
            if (hp > 0.f && hp < 200.f) hack::SetHealth(200.f);
        }

        if (GetAsyncKeyState(VK_F6) & 1) hack::SetHealth(200.f);
    }

    wininput::Shutdown();
    dinput::Shutdown();
    esp::Shutdown();
    overlay::Shutdown();
    FreeLibraryAndExitThread(g_hMod, 0);

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_hMod = hModule;

        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }

    return TRUE;
}
