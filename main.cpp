// CS2 External Mod Menu
// Aimbot + ESP | External process memory read
// Build: MSVC 2022, Windows 10/11 x64

#include <Windows.h>
#include <TlHelp32.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <cmath>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")

// ---------------------------------------------------------------
// CONFIG
// ---------------------------------------------------------------
namespace cfg {
    // Aimbot
    inline bool  aim_enable      = true;
    inline bool  aim_silent       = false;   // silent aim (no mouse move)
    inline float aim_fov          = 5.0f;    // degrees
    inline float aim_smooth       = 8.0f;    // higher = smoother
    inline bool  aim_visible_only = true;
    inline int   aim_bone         = 8;       // 8 = head

    // ESP
    inline bool  esp_enable       = true;
    inline bool  esp_boxes        = true;
    inline bool  esp_health       = true;
    inline bool  esp_name         = true;
    inline bool  esp_distance     = true;
    inline bool  esp_snaplines    = false;
    inline float esp_max_dist     = 1000.0f; // units

    // Colors
    inline ImVec4 col_enemy_vis   = {1.0f, 0.2f, 0.2f, 1.0f};
    inline ImVec4 col_enemy_invis = {0.8f, 0.5f, 0.0f, 1.0f};
    inline ImVec4 col_health_bar  = {0.1f, 0.9f, 0.1f, 1.0f};
    inline ImVec4 col_snapline    = {1.0f, 1.0f, 0.0f, 0.6f};

    // Keybinds
    inline int   key_aim          = VK_LBUTTON;
    inline int   key_menu_toggle  = VK_INSERT;
}

// ---------------------------------------------------------------
// OFFSETS  (update via cs2-dumper)
// ---------------------------------------------------------------
namespace offsets {
    constexpr uintptr_t dwLocalPlayerPawn     = 0x1815988;
    constexpr uintptr_t dwEntityList          = 0x18C4AB8;
    constexpr uintptr_t dwViewMatrix          = 0x19247C0;
    constexpr uintptr_t dwForceJump           = 0x16E5174;

    constexpr uintptr_t m_iHealth             = 0x344;
    constexpr uintptr_t m_iTeamNum            = 0x3E3;
    constexpr uintptr_t m_bPawnIsAlive        = 0x3E4;
    constexpr uintptr_t m_vecOrigin           = 0x7F8;
    constexpr uintptr_t m_vOldOrigin          = 0x12C;
    constexpr uintptr_t m_pGameSceneNode      = 0x310;
    constexpr uintptr_t m_modelState          = 0x170;
    constexpr uintptr_t m_iszPlayerName       = 0x640;
    constexpr uintptr_t m_flFlashDuration     = 0x1290;
}

// ---------------------------------------------------------------
// MEMORY
// ---------------------------------------------------------------
class Memory {
public:
    HANDLE  hProc   = nullptr;
    uintptr_t base  = 0;

    bool Attach(const wchar_t* procName) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W pe{ sizeof(pe) };
        while (Process32NextW(snap, &pe)) {
            if (_wcsicmp(pe.szExeFile, procName) == 0) {
                CloseHandle(snap);
                hProc = OpenProcess(PROCESS_VM_READ, FALSE, pe.th32ProcessID);
                base  = GetModuleBase(pe.th32ProcessID, L"client.dll");
                return hProc && base;
            }
        }
        CloseHandle(snap);
        return false;
    }

    template<typename T>
    T Read(uintptr_t addr) const {
        T val{};
        ReadProcessMemory(hProc, reinterpret_cast<LPCVOID>(addr), &val, sizeof(T), nullptr);
        return val;
    }

    std::string ReadString(uintptr_t addr, size_t len = 32) const {
        std::string s(len, '\0');
        ReadProcessMemory(hProc, reinterpret_cast<LPCVOID>(addr), s.data(), len, nullptr);
        s.erase(s.find('\0'));
        return s;
    }

private:
    uintptr_t GetModuleBase(DWORD pid, const wchar_t* modName) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        MODULEENTRY32W me{ sizeof(me) };
        while (Module32NextW(snap, &me)) {
            if (_wcsicmp(me.szModule, modName) == 0) {
                CloseHandle(snap);
                return reinterpret_cast<uintptr_t>(me.modBaseAddr);
            }
        }
        CloseHandle(snap);
        return 0;
    }
};

// ---------------------------------------------------------------
// MATH
// ---------------------------------------------------------------
struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };

struct Matrix4x4 { float m[4][4]; };

inline float Vec3Dist(const Vec3& a, const Vec3& b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

inline Vec2 WorldToScreen(const Vec3& pos, const Matrix4x4& vm, int sw, int sh) {
    float w = vm.m[3][0]*pos.x + vm.m[3][1]*pos.y + vm.m[3][2]*pos.z + vm.m[3][3];
    if (w < 0.001f) return {-1, -1};
    float x = vm.m[0][0]*pos.x + vm.m[0][1]*pos.y + vm.m[0][2]*pos.z + vm.m[0][3];
    float y = vm.m[1][0]*pos.x + vm.m[1][1]*pos.y + vm.m[1][2]*pos.z + vm.m[1][3];
    return {
        (sw / 2.0f) + (sw / 2.0f) * x / w,
        (sh / 2.0f) - (sh / 2.0f) * y / w
    };
}

// ---------------------------------------------------------------
// ENTITY
// ---------------------------------------------------------------
struct Entity {
    uintptr_t ptr      = 0;
    Vec3      origin   = {};
    Vec3      headPos  = {};
    int       health   = 0;
    int       team     = 0;
    bool      alive    = false;
    bool      visible  = false;
    std::string name;

    Vec2      screenFoot = {};
    Vec2      screenHead = {};
    bool      onScreen   = false;
};

// ---------------------------------------------------------------
// GLOBALS
// ---------------------------------------------------------------
Memory         g_mem;
std::vector<Entity> g_entities;
std::atomic<bool>   g_running{true};
bool                g_menuOpen = true;

// ---------------------------------------------------------------
// GAME READ THREAD
// ---------------------------------------------------------------
void GameThread() {
    while (g_running) {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);

        uintptr_t local = g_mem.Read<uintptr_t>(g_mem.base + offsets::dwLocalPlayerPawn);
        int localTeam   = g_mem.Read<int>(local + offsets::m_iTeamNum);
        Vec3 localPos   = g_mem.Read<Vec3>(local + offsets::m_vecOrigin);
        Matrix4x4 vm    = g_mem.Read<Matrix4x4>(g_mem.base + offsets::dwViewMatrix);

        uintptr_t entityList = g_mem.Read<uintptr_t>(g_mem.base + offsets::dwEntityList);
        std::vector<Entity> ents;

        for (int i = 1; i < 64; i++) {
            uintptr_t listEntry = g_mem.Read<uintptr_t>(entityList + (8 * (i & 0x7FFF) >> 9) + 16);
            if (!listEntry) continue;
            uintptr_t controller = g_mem.Read<uintptr_t>(listEntry + 120 * (i & 0x1FF));
            if (!controller) continue;
            uintptr_t pawn = g_mem.Read<uintptr_t>(controller + 0x7E4);
            if (!pawn || pawn == local) continue;

            Entity e;
            e.ptr    = pawn;
            e.team   = g_mem.Read<int>(pawn + offsets::m_iTeamNum);
            e.alive  = g_mem.Read<bool>(pawn + offsets::m_bPawnIsAlive);
            e.health = g_mem.Read<int>(pawn + offsets::m_iHealth);
            e.name   = g_mem.ReadString(controller + offsets::m_iszPlayerName);

            if (!e.alive || e.health <= 0 || e.team == localTeam) continue;

            // Origin + head bone
            e.origin  = g_mem.Read<Vec3>(pawn + offsets::m_vecOrigin);
            uintptr_t gameScene = g_mem.Read<uintptr_t>(pawn + offsets::m_pGameSceneNode);
            uintptr_t boneArr   = g_mem.Read<uintptr_t>(gameScene + offsets::m_modelState + 0x80);
            Vec3 headBone       = g_mem.Read<Vec3>(boneArr + offsets::aim_bone * 32);
            e.headPos = headBone;

            if (Vec3Dist(localPos, e.origin) > cfg::esp_max_dist) continue;

            // World to screen
            e.screenFoot = WorldToScreen(e.origin,  vm, sw, sh);
            e.screenHead = WorldToScreen(e.headPos, vm, sw, sh);
            e.onScreen   = (e.screenFoot.x > 0 && e.screenFoot.y > 0 &&
                            e.screenFoot.x < sw && e.screenFoot.y < sh);

            e.visible = true; // simplified — add ray cast for full check
            ents.push_back(e);
        }

        g_entities = ents;
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

// ---------------------------------------------------------------
// AIMBOT THREAD
// ---------------------------------------------------------------
void AimbotThread() {
    while (g_running) {
        if (!cfg::aim_enable || !(GetAsyncKeyState(cfg::key_aim) & 0x8000)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            continue;
        }

        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        Vec2 center = { sw / 2.0f, sh / 2.0f };

        Entity* best    = nullptr;
        float   bestDist = cfg::aim_fov * (sw / 90.0f); // fov to px

        for (auto& e : g_entities) {
            if (!e.onScreen) continue;
            if (cfg::aim_visible_only && !e.visible) continue;

            float dx = e.screenHead.x - center.x;
            float dy = e.screenHead.y - center.y;
            float d  = sqrtf(dx*dx + dy*dy);
            if (d < bestDist) { bestDist = d; best = &e; }
        }

        if (best) {
            float dx = best->screenHead.x - center.x;
            float dy = best->screenHead.y - center.y;

            if (!cfg::aim_silent) {
                POINT cur;
                GetCursorPos(&cur);
                int mx = cur.x + static_cast<int>(dx / cfg::aim_smooth);
                int my = cur.y + static_cast<int>(dy / cfg::aim_smooth);
                mouse_event(MOUSEEVENTF_MOVE, mx - cur.x, my - cur.y, 0, 0);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
}

// ---------------------------------------------------------------
// OVERLAY (ImGui + DX11 transparent window)
// ---------------------------------------------------------------
HWND            g_hwnd   = nullptr;
ID3D11Device*          g_dev    = nullptr;
ID3D11DeviceContext*   g_ctx    = nullptr;
IDXGISwapChain*        g_chain  = nullptr;
ID3D11RenderTargetView* g_rtv   = nullptr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true;
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

bool InitOverlay() {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.lpszClassName = L"CS2Overlay";
    wc.hInstance     = GetModuleHandle(nullptr);
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        L"CS2Overlay", L"", WS_POPUP,
        0, 0, sw, sh,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr
    );

    SetLayeredWindowAttributes(g_hwnd, RGB(0,0,0), 255, LWA_ALPHA);

    MARGINS m{-1};
    DwmExtendFrameIntoClientArea(g_hwnd, &m);
    ShowWindow(g_hwnd, SW_SHOW);

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = sw;
    sd.BufferDesc.Height                  = sh;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 144;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = g_hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL fl;
    D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &sd, &g_chain, &g_dev, &fl, &g_ctx
    );

    ID3D11Texture2D* buf = nullptr;
    g_chain->GetBuffer(0, IID_PPV_ARGS(&buf));
    g_dev->CreateRenderTargetView(buf, nullptr, &g_rtv);
    buf->Release();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    return true;
}

// ---------------------------------------------------------------
// RENDER
// ---------------------------------------------------------------
void RenderESP(ImDrawList* dl) {
    if (!cfg::esp_enable) return;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    for (const auto& e : g_entities) {
        if (!e.onScreen) continue;

        ImVec4 cv = e.visible ? cfg::col_enemy_vis : cfg::col_enemy_invis;
        ImU32 col = ImGui::ColorConvertFloat4ToU32(cv);

        if (cfg::esp_boxes) {
            float boxH = e.screenFoot.y - e.screenHead.y;
            float boxW = boxH * 0.45f;
            float bx   = e.screenHead.x - boxW / 2.0f;
            float by   = e.screenHead.y;
            dl->AddRect({bx, by}, {bx + boxW, by + boxH}, col, 0.0f, 0, 1.5f);

            // Corner brackets
            float cs = boxW * 0.25f;
            ImU32 wh = IM_COL32(255,255,255,200);
            dl->AddLine({bx,by},      {bx+cs,by},      wh, 2.0f);
            dl->AddLine({bx,by},      {bx,by+cs},       wh, 2.0f);
            dl->AddLine({bx+boxW,by}, {bx+boxW-cs,by},  wh, 2.0f);
            dl->AddLine({bx+boxW,by}, {bx+boxW,by+cs},  wh, 2.0f);
            dl->AddLine({bx,by+boxH}, {bx+cs,by+boxH},  wh, 2.0f);
            dl->AddLine({bx,by+boxH}, {bx,by+boxH-cs},  wh, 2.0f);
            dl->AddLine({bx+boxW,by+boxH},{bx+boxW-cs,by+boxH},wh,2.0f);
            dl->AddLine({bx+boxW,by+boxH},{bx+boxW,by+boxH-cs},wh,2.0f);

            if (cfg::esp_health) {
                float hPct = e.health / 100.0f;
                ImU32 hCol = ImGui::ColorConvertFloat4ToU32(cfg::col_health_bar);
                dl->AddRect ({bx - 6, by},              {bx - 4, by + boxH},         IM_COL32(0,0,0,180));
                dl->AddRectFilled({bx-6, by+boxH*(1-hPct)},{bx-4, by+boxH},          hCol);
            }

            if (cfg::esp_name && !e.name.empty()) {
                dl->AddText({bx + boxW/2 - e.name.size()*3.5f, by - 14}, IM_COL32(255,255,255,220), e.name.c_str());
            }

            if (cfg::esp_distance) {
                // distance placeholder — real calc needs local pos
                char buf[16];
                snprintf(buf, sizeof(buf), "%dhp", e.health);
                dl->AddText({bx + boxW/2 - 12, by + boxH + 2}, IM_COL32(200,200,200,180), buf);
            }
        }

        if (cfg::esp_snaplines) {
            dl->AddLine(
                {sw / 2.0f, (float)sh},
                {e.screenFoot.x, e.screenFoot.y},
                ImGui::ColorConvertFloat4ToU32(cfg::col_snapline), 1.0f
            );
        }
    }
}

void RenderMenu() {
    ImGui::SetNextWindowSize({420, 520}, ImGuiCond_Once);
    ImGui::SetNextWindowPos({30, 30}, ImGuiCond_Once);
    ImGui::Begin("CS2 Mod Menu", nullptr, ImGuiWindowFlags_NoCollapse);

    if (ImGui::BeginTabBar("tabs")) {

        if (ImGui::BeginTabItem("Aimbot")) {
            ImGui::Checkbox("Enable Aimbot",     &cfg::aim_enable);
            ImGui::Checkbox("Silent Aim",        &cfg::aim_silent);
            ImGui::Checkbox("Visible Only",      &cfg::aim_visible_only);
            ImGui::SliderFloat("FOV",            &cfg::aim_fov,    1.0f, 45.0f);
            ImGui::SliderFloat("Smooth",         &cfg::aim_smooth, 1.0f, 30.0f);
            ImGui::SliderInt("Bone (8=head)",    &cfg::aim_bone,   0,    50);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("ESP")) {
            ImGui::Checkbox("Enable ESP",        &cfg::esp_enable);
            ImGui::Checkbox("Boxes",             &cfg::esp_boxes);
            ImGui::Checkbox("Health Bar",        &cfg::esp_health);
            ImGui::Checkbox("Name",              &cfg::esp_name);
            ImGui::Checkbox("Distance",          &cfg::esp_distance);
            ImGui::Checkbox("Snaplines",         &cfg::esp_snaplines);
            ImGui::SliderFloat("Max Distance",   &cfg::esp_max_dist, 100.0f, 5000.0f);
            ImGui::ColorEdit4("Visible Color",   (float*)&cfg::col_enemy_vis);
            ImGui::ColorEdit4("Invisible Color", (float*)&cfg::col_enemy_invis);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Colors")) {
            ImGui::ColorEdit4("Health Bar",  (float*)&cfg::col_health_bar);
            ImGui::ColorEdit4("Snap Line",   (float*)&cfg::col_snapline);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::Text("INSERT = toggle menu | Entities: %d", (int)g_entities.size());
    ImGui::End();
}

// ---------------------------------------------------------------
// MAIN
// ---------------------------------------------------------------
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (!g_mem.Attach(L"cs2.exe")) {
        MessageBoxW(nullptr, L"cs2.exe not found.", L"Error", MB_ICONERROR);
        return 1;
    }

    InitOverlay();

    std::thread(GameThread).detach();
    std::thread(AimbotThread).detach();

    MSG msg{};
    while (g_running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) { g_running = false; }
        }

        if (GetAsyncKeyState(cfg::key_menu_toggle) & 1)
            g_menuOpen = !g_menuOpen;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        RenderESP(dl);
        if (g_menuOpen) RenderMenu();

        ImGui::Render();
        float cc[4]{0,0,0,0};
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_chain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    return 0;
}
