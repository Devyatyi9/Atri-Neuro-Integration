#include <windows.h>
#include <cstring>

#include "imgui.h"
#include "ImGuiInit.h"
#include "ImGuiWindows.h"

static char g_SearchBuffer[256] = "";

static void RenderVariablesPanel() {
    ImGui::SetNextWindowPos(ImVec2(10, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Variables##Neuro", nullptr)) {
        ImGui::End();
        return;
    }

    ImGui::InputText("Search", g_SearchBuffer, sizeof(g_SearchBuffer));
    ImGui::Separator();
    ImGui::Text("Variables (WIP - requires TJS access)");
    ImGui::TextWrapped("Use F12 to toggle.");

    ImGui::End();
}

static void RenderObjectInspector() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 380), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Object Inspector##Neuro", nullptr)) {
        ImGui::End();
        return;
    }

    ImGui::InputText("Search##inspector", g_SearchBuffer, sizeof(g_SearchBuffer));
    ImGui::Separator();

    static ImGuiTreeNodeFlags base_flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanAvailWidth;

    if (ImGui::TreeNodeEx("global", base_flags)) {
        ImGui::TextDisabled("(Use Scripts.getObjectKeys(global))");
        ImGui::TreePop();
    }
    ImGui::End();
}

void RenderImGuiWindows() {
    if (!IsImGuiVisible()) return;

    RenderObjectInspector();
    RenderVariablesPanel();
}
