#include "menu.h"
#include "hack.h"
#include "esp.h"

#include <d3d11.h>

#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <cstring>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace menu {
    static constexpr int kMenuW = 470;
    static constexpr int kMenuH = 680;

    static bool   g_show = false;
    static bool   g_unload = false;
    static bool   g_running = false;
    static HANDLE g_thread = nullptr;
    static FILE*  g_log = nullptr;

    static HWND   g_menuWnd = nullptr;
    static HWND   g_game = nullptr;

    static ID3D11Device*           g_dev = nullptr;
    static ID3D11DeviceContext*    g_ctx = nullptr;
    static IDXGISwapChain*         g_swap = nullptr;
    static ID3D11RenderTargetView* g_rtv = nullptr;

    struct FindCtx { DWORD pid; HWND hwnd; int area; };
    static BOOL CALLBACK EnumCb(HWND hwnd, LPARAM lp)
    {
        auto* ctx = reinterpret_cast<FindCtx*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != ctx->pid) return TRUE;
        if (hwnd == g_menuWnd) return TRUE;
        if (!IsWindowVisible(hwnd)) return TRUE;
        if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
        RECT r{};
        if (!GetClientRect(hwnd, &r)) return TRUE;
        int area = (r.right - r.left) * (r.bottom - r.top);
        if (area > ctx->area) { ctx->area = area; ctx->hwnd = hwnd; }
        return TRUE;
    }

    static HWND FindGameWindow()
    {
        FindCtx ctx{ GetCurrentProcessId(), nullptr, 0 };
        EnumWindows(EnumCb, reinterpret_cast<LPARAM>(&ctx));
        return ctx.hwnd;
    }

    static LRESULT CALLBACK MenuWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
            return 1;
        if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    static bool CreateDevice()
    {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = kMenuW;
        sd.BufferDesc.Height = kMenuH;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = g_menuWnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        const D3D_FEATURE_LEVEL lvls[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL lvl;
        if (!SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                0, lvls, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &lvl, &g_ctx))) {
            return false;
        }
        ID3D11Texture2D* back = nullptr;
        if (SUCCEEDED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back)))) {
            g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
            back->Release();
        }
        return g_rtv != nullptr;
    }

    static void DrawMenu()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("sam", nullptr, flags);

        ImGui::TextUnformatted("serious sam internal");
        ImGui::SameLine(ImGui::GetWindowWidth() - 60.0f);
        if (ImGui::SmallButton("hide")) g_show = false;

        uintptr_t player = hack::GetLocalPlayer();
        float hp = hack::GetHealth();

        if (!ImGui::BeginTabBar("tabs")) { ImGui::End(); return; }

        if (ImGui::BeginTabItem("Main")) {
            ImGui::Text("player: 0x%p  HP: %.0f", (void*)player, hp);
            if (player == 0)
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "player not found");

            bool god = hack::IsGodMode();
            if (ImGui::Checkbox("godmode", &god))
                hack::SetGodMode(god);

            bool rapid = hack::IsRapidFire();
            if (ImGui::Checkbox("rapid fire", &rapid))
                hack::SetRapidFire(rapid);

            if (ImGui::Button("set 200 hp", ImVec2(-1, 0)))
                hack::SetHealth(200.f);

            bool esp = esp::g_enabled;
            if (ImGui::Checkbox("esp box", &esp)) esp::g_enabled = esp;
            ImGui::SliderFloat("ESP FOV (as in game)", &esp::g_fov, 60.0f, 120.0f, "%.0f");
            ImGui::Checkbox("hide staging (no target)", &esp::g_hideStaged);
            ImGui::Text("drawn: %d cached: %d tgt: %d/%d",
                esp::g_drawn, esp::g_cached, esp::g_tgtOk, esp::g_tgtAll);
            ImGui::RadioButton("hook both", &esp::g_hookMode, 0); ImGui::SameLine();
            ImGui::RadioButton("wgl", &esp::g_hookMode, 1); ImGui::SameLine();
            ImGui::RadioButton("gdi", &esp::g_hookMode, 2);
            if (ImGui::Button("UNLOAD", ImVec2(-1, 0)))
                g_unload = true;
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Entities")) {
            int n = hack::GetEntityCount();
            uintptr_t cont = hack::FindEntityContainer();
            ImGui::Text("entities: %d  list: 0x%p", n, (void*)cont);
            static bool aliveOnly = true;
            ImGui::Checkbox("alive only (ENF_ALIVE)", &aliveOnly);
            static bool enemiesOnly = true;
            ImGui::Checkbox("enemies only (Enemy Base)", &enemiesOnly);
            if (n <= 0) {
                ImGui::TextDisabled("no list: load a level (not menu)");
            } else if (ImGui::BeginChild("ents", ImVec2(0, 200), true)) {
                struct Row { uintptr_t e; int id; float d; float hp; bool tgt; char cls[72]; };
                static Row rows[2048];
                static int total = 0;
                static unsigned long long rowTick = 0;
                unsigned long long now = GetTickCount64();
                if (now - rowTick > 500 || total == 0) {
                    rowTick = now;
                    total = 0;
                    float pp[3] = { 0, 0, 0 };
                    uintptr_t playerEnt = hack::GetLocalPlayer();
                    bool hasP = playerEnt && hack::GetEntityPos(playerEnt, pp);
                    for (int i = 0; i < n && total < 2048; ++i) {
                        uintptr_t e = hack::GetEntity(i);
                        if (!e) continue;
                        if (aliveOnly && !(hack::GetEntityFlags(e) & 8)) continue; // ENF_ALIVE
                        if (enemiesOnly && !hack::IsEnemy(e)) continue;
                        Row& r = rows[total++];
                        r.e = e;
                        r.id = hack::GetEntityId(e);
                        r.hp = hack::GetEntityHp(e);
                        r.tgt = hack::GetEnemyTarget(e) != 0;
                        r.d = -1.f;
                        float ep[3];
                        if (hasP && hack::GetEntityPos(e, ep)) {
                            float dx = ep[0] - pp[0], dy = ep[1] - pp[1], dz = ep[2] - pp[2];
                            r.d = sqrtf(dx * dx + dy * dy + dz * dz);
                        }
                        if (!hack::GetEntityClassName(e, r.cls, sizeof(r.cls)))
                            memcpy(r.cls, "?", 2);
                    }
                    for (int i = 1; i < total; ++i) {
                        Row tmp = rows[i];
                        int j = i - 1;
                        while (j >= 0 && rows[j].d > tmp.d) { rows[j + 1] = rows[j]; --j; }
                        rows[j + 1] = tmp;
                    }
                }
                for (int i = 0; i < total && i < 500; ++i) {
                    const Row& r = rows[i];
                    if (esp::g_hideStaged && !r.tgt) continue; // staging 
                    const char* tag = r.tgt ? "TGT" : "staging";
                    if (r.d >= 0)
                        ImGui::Text("%4d | id %d | hp %.0f | %.0fm | %s | %s", i, r.id, r.hp, r.d, tag, r.cls);
                    else
                        ImGui::Text("%4d | id %d | hp %.0f | ? | %s | %s", i, r.id, r.hp, tag, r.cls);
                }
                ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
        ImGui::End();
    }

    static DWORD WINAPI MenuThread(LPVOID)
    {
        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = MenuWndProc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "serious internal";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExA(&wc);

        g_menuWnd = CreateWindowExA(
            WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            "serious internal", "serious internal",
            WS_POPUP, 0, 0, kMenuW, kMenuH,
            nullptr, nullptr, wc.hInstance, nullptr);
        if (!g_menuWnd) return 1;

        if (!CreateDevice()) return 1;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(g_menuWnd);
        ImGui_ImplDX11_Init(g_dev, g_ctx);

        ShowWindow(g_menuWnd, SW_HIDE);

        MSG msg{};
        bool placed = false;
        while (g_running) {
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { g_running = false; break; }
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            if (!g_running) break;

            if (!g_game || !IsWindow(g_game)) {
                g_game = FindGameWindow();
                placed = false;
                if (g_game) {
                    char title[256]{};
                    GetWindowTextA(g_game, title, sizeof(title));
                }
            }

            bool usable = g_show && g_game && IsWindow(g_game) && !IsIconic(g_game)
                && GetForegroundWindow() == g_game;
            if (!usable) {
                ShowWindow(g_menuWnd, SW_HIDE);
                placed = false;
                Sleep(100);
                continue;
            }

            if (!placed) {
                POINT p{ 16, 16 };
                ClientToScreen(g_game, &p);
                SetWindowPos(g_menuWnd, HWND_TOPMOST, p.x, p.y, kMenuW, kMenuH,
                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
                placed = true;
            }

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            DrawMenu();
            ImGui::EndFrame();
            ImGui::Render();

            const float clear[4] = { 0.06f, 0.06f, 0.08f, 1.0f };
            g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
            g_ctx->ClearRenderTargetView(g_rtv, clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            g_swap->Present(1, 0);
        }

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
        if (g_swap) { g_swap->Release(); g_swap = nullptr; }
        if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
        if (g_dev) { g_dev->Release(); g_dev = nullptr; }
        if (g_menuWnd) { DestroyWindow(g_menuWnd); g_menuWnd = nullptr; }
        UnregisterClassA("serious internal", wc.hInstance);
        return 0;
    }

    bool Init()
    {
        if (g_running) return true;
        g_running = true;
        g_thread = CreateThread(nullptr, 0, MenuThread, nullptr, 0, nullptr);
        return g_thread != nullptr;
    }

    void Shutdown()
    {
        g_running = false;
        if (g_thread) {
            WaitForSingleObject(g_thread, 3000);
            CloseHandle(g_thread);
            g_thread = nullptr;
        }
        if (g_log) { fclose(g_log); g_log = nullptr; }
    }

    void Toggle() { g_show = !g_show; }
    bool IsVisible() { return g_show; }
    bool ShouldUnload() { return g_unload; }
}
