#include <windows.h>
#include <d3d9.h>
#include <cstring>

#include <MinHook.h>

#include "imgui.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"

static bool g_ImGuiInitialized = false;
static bool g_ImGuiVisible = false;
static HWND g_GameWindowHWND = nullptr;
static IDirect3DDevice9* g_GameDevice = nullptr;

bool IsImGuiInitialized() { return g_ImGuiInitialized; }
bool IsImGuiVisible() { return g_ImGuiVisible; }
void ToggleImGui() { g_ImGuiVisible = !g_ImGuiVisible; }

void ImGui_Init(IDirect3DDevice9* device, HWND hwnd) {
    if (g_ImGuiInitialized) return;

    g_GameDevice = device;
    g_GameWindowHWND = hwnd;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplDX9_Init(device)) {
        OutputDebugStringW(L"[ImGui] Failed to init DX9 backend\n");
        return;
    }

    if (!ImGui_ImplWin32_Init(hwnd)) {
        OutputDebugStringW(L"[ImGui] Failed to init Win32 backend\n");
        ImGui_ImplDX9_Shutdown();
        return;
    }

    g_ImGuiInitialized = true;
    g_ImGuiVisible = false;
    OutputDebugStringW(L"[ImGui] Initialized successfully\n");
}

void ImGui_Shutdown() {
    if (!g_ImGuiInitialized) return;

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    g_ImGuiInitialized = false;
    g_ImGuiVisible = false;
    g_GameDevice = nullptr;
    g_GameWindowHWND = nullptr;
}

void ImGui_NewFrame() {
    if (!g_ImGuiInitialized) return;

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGui_Render() {
    if (!g_ImGuiInitialized) return;

    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

void ImGui_SetDevice(IDirect3DDevice9* device) {
    g_GameDevice = device;
}
