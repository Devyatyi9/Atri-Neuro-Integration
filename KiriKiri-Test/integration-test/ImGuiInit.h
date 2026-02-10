#pragma once

#include <windows.h>
#include <d3d9.h>

void ImGui_Init(IDirect3DDevice9* device, HWND hwnd);
void ImGui_Shutdown();
void ImGui_NewFrame();
void ImGui_Render();
void ImGui_SetDevice(IDirect3DDevice9* device);

bool IsImGuiInitialized();
bool IsImGuiVisible();
void ToggleImGui();
