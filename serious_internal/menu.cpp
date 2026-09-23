#include "menu.h"
#include "hack.h"
#include "esp.h"
#include "aim.h"
#include "overlay.h"

#include "imgui.h"

#include <cstdio>

namespace menu {

    static ImFont* s_mono = nullptr;
    static int     s_activeTab = 0;

    void InitFonts()
    {
        if (s_mono) return;
        ImGuiIO& io = ImGui::GetIO();
        static const char* candidates[] = {
            "c:\\Windows\\Fonts\\consola.ttf",
            "c:\\Windows\\Fonts\\lucon.ttf",
        };
        for (const char* p : candidates) {
            FILE* f = nullptr;
            if (fopen_s(&f, p, "rb") == 0 && f) {
                fclose(f);
                s_mono = io.Fonts->AddFontFromFileTTF(p, 17.0f);
                if (s_mono) break;
            }
        }
    }

    void ResetFonts() { s_mono = nullptr; }

    static ImU32 COL_HEADER_BG()     { return IM_COL32(33, 43, 22, 255); }
    static ImU32 COL_TAB_ACTIVE_BG() { return IM_COL32(22, 26, 14, 255); }
    static ImU32 COL_BORDER()        { return IM_COL32(74, 94, 42, 255); }
    static ImU32 COL_LINE()          { return IM_COL32(58, 76, 34, 255); }
    static ImU32 COL_TEXT_DIM()      { return IM_COL32(88, 106, 56, 255); }
    static ImU32 COL_TEXT_MID()      { return IM_COL32(122, 158, 68, 255); }
    static ImU32 COL_TEXT_BRIGHT()   { return IM_COL32(178, 224, 74, 255); }
    static ImU32 COL_BAR_FILL()      { return IM_COL32(106, 134, 70, 255); }
    static ImU32 COL_BAR_BG()        { return IM_COL32(24, 28, 15, 255); }
    static ImU32 COL_BAR_DOT()       { return IM_COL32(80, 98, 52, 255); }
    static ImU32 COL_BTN_GREEN()     { return IM_COL32(96, 140, 66, 255); }
    static ImU32 COL_BTN_GOLD()      { return IM_COL32(158, 128, 42, 255); }
    static ImU32 COL_BTN_RED()       { return IM_COL32(158, 74, 84, 255); }

    static int ActiveCount()
    {
        int n = 0;
        if (hack::IsGodMode()) n++;
        if (hack::IsRapidFire()) n++;
        if (esp::g_enabled) n++;
        if (aim::g_enabled) n++;
        return n;
    }

    static void Checkbox(const char* label, bool* v)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cur = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        float h = 26.0f;

        bool hovered = ImGui::IsMouseHoveringRect(cur, ImVec2(cur.x + w, cur.y + h));
        if (hovered) {
            dl->AddRectFilled(cur, ImVec2(cur.x + w, cur.y + h), IM_COL32(26, 32, 16, 255));
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        if (hovered && ImGui::IsMouseClicked(0))
            *v = !*v;

        ImU32 col = *v ? COL_TEXT_BRIGHT() : COL_TEXT_DIM();
        char buf[64];
        snprintf(buf, sizeof(buf), "[%c]", *v ? 'x' : '_');
        dl->AddText(ImVec2(cur.x + 2, cur.y + 4), col, buf);
        dl->AddText(ImVec2(cur.x + 36, cur.y + 4), col, label);

        ImGui::Dummy(ImVec2(w, h));
    }

    static void SliderBar(const char* label, int* val, int v_min, int v_max, const char* suffix = "")
    {
        {
            ImVec2 cur = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(cur, COL_TEXT_MID(), label);
            ImVec2 sz = ImGui::CalcTextSize(label);
            char vb[32];
            snprintf(vb, sizeof(vb), "%d%s", *val, suffix);
            dl->AddText(ImVec2(cur.x + sz.x + 8, cur.y), COL_TEXT_BRIGHT(), vb);
            ImGui::Dummy(ImVec2(0, 19));
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cur = ImGui::GetCursorScreenPos();
        float bar_w = 172.0f;
        float bar_h = 19.0f;

        float t = (float)(*val - v_min) / (float)(v_max - v_min);
        if (t < 0) t = 0; if (t > 1) t = 1;

        dl->AddRectFilled(cur, ImVec2(cur.x + bar_w, cur.y + bar_h), COL_BAR_BG());
        for (float y = cur.y + 3; y < cur.y + bar_h - 1; y += 5)
            for (float x = cur.x + 3; x < cur.x + bar_w - 1; x += 5)
                dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 2, y + 2), COL_BAR_DOT());
        float fw = bar_w * t;
        if (fw > 0)
            dl->AddRectFilled(cur, ImVec2(cur.x + fw, cur.y + bar_h), COL_BAR_FILL());

        ImGui::InvisibleButton(label, ImVec2(bar_w, bar_h));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
            ImVec2 mp = ImGui::GetIO().MousePos;
            float nt = (mp.x - cur.x) / bar_w;
            if (nt < 0) nt = 0; if (nt > 1) nt = 1;
            *val = v_min + (int)(nt * (v_max - v_min) + 0.5f);
        }
        ImGui::Dummy(ImVec2(0, 6));
    }

    static void SliderBarF(const char* label, float* val, float v_min, float v_max)
    {
        {
            ImVec2 cur = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(cur, COL_TEXT_MID(), label);
            ImVec2 sz = ImGui::CalcTextSize(label);
            char vb[32];
            snprintf(vb, sizeof(vb), "%.1f", *val);
            dl->AddText(ImVec2(cur.x + sz.x + 8, cur.y), COL_TEXT_BRIGHT(), vb);
            ImGui::Dummy(ImVec2(0, 19));
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cur = ImGui::GetCursorScreenPos();
        float bar_w = 172.0f;
        float bar_h = 19.0f;

        float t = (*val - v_min) / (v_max - v_min);
        if (t < 0) t = 0; if (t > 1) t = 1;

        dl->AddRectFilled(cur, ImVec2(cur.x + bar_w, cur.y + bar_h), COL_BAR_BG());
        for (float y = cur.y + 3; y < cur.y + bar_h - 1; y += 5)
            for (float x = cur.x + 3; x < cur.x + bar_w - 1; x += 5)
                dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 2, y + 2), COL_BAR_DOT());
        float fw = bar_w * t;
        if (fw > 0)
            dl->AddRectFilled(cur, ImVec2(cur.x + fw, cur.y + bar_h), COL_BAR_FILL());

        ImGui::InvisibleButton(label, ImVec2(bar_w, bar_h));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
            ImVec2 mp = ImGui::GetIO().MousePos;
            float nt = (mp.x - cur.x) / bar_w;
            if (nt < 0) nt = 0; if (nt > 1) nt = 1;
            *val = v_min + nt * (v_max - v_min);
        }
        ImGui::Dummy(ImVec2(0, 6));
    }

    static bool Button(const char* label, ImU32 accent)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cur = ImGui::GetCursorScreenPos();
        ImVec2 sz(112, 28);
        bool hovered = ImGui::IsMouseHoveringRect(cur, ImVec2(cur.x + sz.x, cur.y + sz.y));
        ImU32 bg = hovered ? IM_COL32(34, 42, 20, 255) : IM_COL32(20, 24, 13, 255);
        dl->AddRectFilled(cur, ImVec2(cur.x + sz.x, cur.y + sz.y), bg);
        dl->AddRect(cur, ImVec2(cur.x + sz.x, cur.y + sz.y), accent, 0.0f, 0, 1.0f);
        ImVec2 ts = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(cur.x + (sz.x - ts.x) * 0.5f, cur.y + (sz.y - ts.y) * 0.5f), accent, label);
        ImGui::Dummy(sz);
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        return hovered && ImGui::IsMouseClicked(0);
    }

    void Draw()
    {
        if (s_mono)
            ImGui::PushFont(s_mono);

        ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(470, 640), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(430, 500), ImVec2(600, 900));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(74.f/255, 94.f/255, 42.f/255, 1));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(13.f/255, 15.f/255, 8.f/255, 1));

        ImGui::Begin("##cheat_console", nullptr, flags);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 win_pos = ImGui::GetWindowPos();
        ImVec2 win_size = ImGui::GetWindowSize();
        float W = win_size.x;

        {
            ImVec2 p0 = win_pos, p1 = ImVec2(win_pos.x + W, win_pos.y + 38);
            dl->AddRectFilled(p0, p1, COL_HEADER_BG());
            dl->AddLine(ImVec2(p0.x, p1.y), p1, COL_BORDER());
            dl->AddText(ImVec2(p0.x + 12, p0.y + 9), COL_TEXT_BRIGHT(), "(*) CHEAT CONSOLE");

            char cnt[32];
            snprintf(cnt, sizeof(cnt), "[%d CHEATS ON]", ActiveCount());
            ImVec2 ts = ImGui::CalcTextSize(cnt);
            dl->AddText(ImVec2(p1.x - ts.x - 12, p0.y + 9), COL_TEXT_DIM(), cnt);
            ImGui::Dummy(ImVec2(W, 38));
        }

        {
            const char* tabs[] = { "PLAYER", "WEAPONS", "WORLD", "DEBUG" };
            float tab_w = W / 4.0f;
            float tab_h = 30.0f;
            ImVec2 base = ImGui::GetCursorScreenPos();

            for (int i = 0; i < 4; i++) {
                ImVec2 p0(base.x + tab_w * i, base.y);
                ImVec2 p1(p0.x + tab_w, p0.y + tab_h);
                bool active = (s_activeTab == i);
                bool hovered = ImGui::IsMouseHoveringRect(p0, p1);

                if (active)
                    dl->AddRectFilled(p0, p1, COL_TAB_ACTIVE_BG());
                else if (hovered)
                    dl->AddRectFilled(p0, p1, IM_COL32(18, 21, 11, 255));

                ImU32 tc = active ? COL_TEXT_BRIGHT() : COL_TEXT_DIM();
                char tbuf[32];
                if (active) snprintf(tbuf, sizeof(tbuf), ">%s", tabs[i]);
                else snprintf(tbuf, sizeof(tbuf), "%s", tabs[i]);
                ImVec2 ts = ImGui::CalcTextSize(tbuf);
                dl->AddText(ImVec2(p0.x + (tab_w - ts.x) * 0.5f, p0.y + 7), tc, tbuf);

                if (i > 0)
                    dl->AddLine(ImVec2(p0.x, p0.y + 6), ImVec2(p0.x, p1.y - 6), COL_LINE());

                if (hovered && ImGui::IsMouseClicked(0))
                    s_activeTab = i;
            }
            if (ImGui::IsMouseHoveringRect(base, ImVec2(base.x + W, base.y + tab_h)))
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::Dummy(ImVec2(W, tab_h));
            ImVec2 lp = ImGui::GetCursorScreenPos();
            dl->AddLine(lp, ImVec2(lp.x + W, lp.y), COL_LINE());
            ImGui::Dummy(ImVec2(0, 8));
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        float pad = 12.0f;

        auto SectionTitle = [&](const char* t)
        {
            ImVec2 cur = ImGui::GetCursorScreenPos();
            dl->AddText(ImVec2(cur.x + pad, cur.y), COL_TEXT_DIM(), t);
            ImGui::Dummy(ImVec2(0, 20));
        };

        if (s_activeTab == 0)
        {
            SectionTitle("-- FLAGS --");
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
            ImGui::BeginGroup();
            ImGui::PushItemWidth(W - pad * 2);
            {
                bool god = hack::IsGodMode();
                Checkbox("GOD MODE", &god);
                if (god != hack::IsGodMode()) hack::SetGodMode(god);
            }
            ImGui::PopItemWidth();
            ImGui::EndGroup();

            {
                ImVec2 cur = ImGui::GetCursorScreenPos();
                dl->AddLine(ImVec2(cur.x + pad, cur.y + 4), ImVec2(cur.x + W - pad, cur.y + 4), COL_LINE());
                ImGui::Dummy(ImVec2(0, 12));
            }

            SectionTitle("-- STATS --");
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
            ImGui::BeginGroup();
            {
                float liveHp = hack::GetHealth();
                int hp = liveHp < 0.f ? 0 : (int)(liveHp + 0.5f);
                int hpPrev = hp;
                SliderBar("HEALTH", &hp, 0, 200);
                if (hp != hpPrev) hack::SetHealth((float)hp);
            }
            ImGui::EndGroup();

            {
                ImGui::Dummy(ImVec2(0, 2));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
                if (Button("FULL HEAL", COL_BTN_GREEN())) hack::SetHealth(200.f);
                ImGui::SameLine(0, 8);
                if (Button("TOGGLE GOD", COL_BTN_GOLD())) hack::SetGodMode(!hack::IsGodMode());
                ImGui::SameLine(0, 8);
                if (Button("1HP DARE", COL_BTN_RED())) hack::SetHealth(1.f);
                ImGui::Dummy(ImVec2(0, 6));
            }
        }
        else if (s_activeTab == 1)
        {
            SectionTitle("-- FLAGS --");
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
            ImGui::BeginGroup();
            ImGui::PushItemWidth(W - pad * 2);
            {
                bool rapid = hack::IsRapidFire();
                Checkbox("RAPID FIRE", &rapid);
                if (rapid != hack::IsRapidFire()) hack::SetRapidFire(rapid);
            }
            ImGui::PopItemWidth();
            ImGui::EndGroup();

            ImGui::Dummy(ImVec2(0, 8));
            ImVec2 cur = ImGui::GetCursorScreenPos();
            dl->AddText(ImVec2(cur.x + pad, cur.y), COL_TEXT_DIM(), "ammo tops up while rapid is on");
            ImGui::Dummy(ImVec2(0, 300));
        }
        else if (s_activeTab == 2)
        {
            SectionTitle("-- ESP --");
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
            ImGui::BeginGroup();
            ImGui::PushItemWidth(W - pad * 2);
            Checkbox("ESP BOX", &esp::g_enabled);
            Checkbox("HIDE STAGING", &esp::g_hideStaged);
            SliderBarF("ESP FOV", &esp::g_fov, 60.0f, 120.0f);
            SliderBarF("MAX DIST", &esp::g_maxDist, 0.0f, 500.0f);
            ImGui::PopItemWidth();
            ImGui::EndGroup();

            {
                char info[96];
                snprintf(info, sizeof(info), "drawn: %d cached: %d tgt: %d/%d",
                    esp::g_drawn, esp::g_cached, esp::g_tgtOk, esp::g_tgtAll);
                ImVec2 c2 = ImGui::GetCursorScreenPos();
                dl->AddText(ImVec2(c2.x + pad, c2.y), COL_TEXT_DIM(), info);
                ImGui::Dummy(ImVec2(0, 22));
            }

            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
                int want = esp::g_hookMode;
                if (Button("HOOK BOTH", want == 0 ? COL_BTN_GREEN() : COL_LINE())) want = 0;
                ImGui::SameLine(0, 8);
                if (Button("WGL", want == 1 ? COL_BTN_GREEN() : COL_LINE())) want = 1;
                ImGui::SameLine(0, 8);
                if (Button("GDI", want == 2 ? COL_BTN_GREEN() : COL_LINE())) want = 2;
                esp::g_hookMode = want;
                ImGui::Dummy(ImVec2(0, 6));
            }
        }
        else
        {
            SectionTitle("-- AIMBOT --");
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
            ImGui::BeginGroup();
            ImGui::PushItemWidth(W - pad * 2);
            Checkbox("AIMBOT (HOLD KEY)", &aim::g_enabled);
            Checkbox("TURN BODY", &aim::g_turnBody);
            SliderBarF("AIM FOV", &aim::g_fov, 5.0f, 90.0f);
            SliderBarF("SMOOTH", &aim::g_smooth, 1.0f, 20.0f);
            SliderBarF("AIM DIST", &aim::g_maxDist, 0.0f, 500.0f);
            ImGui::PopItemWidth();
            ImGui::EndGroup();

            {
                uintptr_t player = hack::GetLocalPlayer();
                float yaw = 0, pitch = 0;
                hack::GetViewAngles(yaw, pitch);
                char info[128];
                snprintf(info, sizeof(info), "player: 0x%p hp: %.0f ents: %d tgt: 0x%p",
                    (void*)player, hack::GetHealth(), hack::GetEntityCount(), (void*)aim::g_target);
                ImVec2 c2 = ImGui::GetCursorScreenPos();
                dl->AddText(ImVec2(c2.x + pad, c2.y), COL_TEXT_DIM(), info);
                ImGui::Dummy(ImVec2(0, 22));
            }

            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
                if (Button("UNLOAD", COL_BTN_RED())) overlay::RequestUnload();
                ImGui::Dummy(ImVec2(0, 6));
            }
        }
        ImGui::PopStyleVar();

        {
            float footer_h = 30.0f;
            float reserved = ImGui::GetWindowHeight() - footer_h;
            float cy = ImGui::GetCursorPosY();
            if (cy < reserved - 8)
                ImGui::Dummy(ImVec2(0, reserved - 8 - cy));

            ImVec2 fp = ImGui::GetCursorScreenPos();
            fp.y = win_pos.y + win_size.y - footer_h;
            dl->AddLine(ImVec2(win_pos.x, fp.y), ImVec2(win_pos.x + W, fp.y), COL_BORDER());
            dl->AddRectFilled(ImVec2(win_pos.x, fp.y + 1), ImVec2(win_pos.x + W, win_pos.y + win_size.y), IM_COL32(15, 18, 9, 255));

            if (hack::IsGodMode())
                dl->AddText(ImVec2(win_pos.x + 12, fp.y + 7), COL_TEXT_MID(), "*** GOD MODE ***");
            else
                dl->AddText(ImVec2(win_pos.x + 12, fp.y + 7), COL_TEXT_DIM(), "--- READY ---");

            const char* hint = "[INSERT] toggle";
            ImVec2 ts = ImGui::CalcTextSize(hint);
            dl->AddText(ImVec2(win_pos.x + W - ts.x - 12, fp.y + 7), COL_TEXT_DIM(), hint);
        }

        ImGui::End();

        if (s_mono)
            ImGui::PopFont();
    }
}
