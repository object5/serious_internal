#include "hack.h"
#include "menu.h"
#include "esp.h"

static HMODULE g_hMod = nullptr;

static DWORD WINAPI MainThread(LPVOID)
{
    bool menuOk = menu::Init();
    bool espOk = esp::Init();

    OutputDebugStringA("[serious_internal] loaded\n");

    for (;;) {
        Sleep(50);

        if ((GetAsyncKeyState(VK_INSERT) & 1) && menuOk)
            menu::Toggle();

        if ((GetAsyncKeyState(VK_END) & 1) || menu::ShouldUnload())
            break;

        if (GetAsyncKeyState(VK_F5) & 1)
            hack::SetGodMode(!hack::IsGodMode());

        if (hack::IsGodMode()) {
            float hp = hack::GetHealth();
            if (hp > 0.f && hp < 200.f) hack::SetHealth(200.f);
        }

        if (GetAsyncKeyState(VK_F6) & 1) hack::SetHealth(200.f);
    }

    esp::Shutdown();
    menu::Shutdown();
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
