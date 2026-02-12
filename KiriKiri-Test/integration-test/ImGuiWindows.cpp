#include <windows.h>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "ImGuiInit.h"
#include "ImGuiWindows.h"

static char g_SearchBufferInspector[256] = "";
static char g_SearchBufferVars[256] = "";

// ============================================================================
// Object Inspector - Tree View с lazy loading (только объекты)
// ============================================================================

struct ObjectNodeData {
    std::string name;           // Имя узла
    std::string fullPath;       // Полный путь
    bool expanded = false;
    int loadedKeys = 0;         // Сколько ключей загружено
    int totalKeys = 0;          // Всего ключей
};

// Объекты для Object Inspector - только global для теста
static ObjectNodeData g_ObjectNodes[] = {
    {"global", "global", false, 0, 0}
};

// Опция для перезагрузки при каждом открытии
static bool g_AlwaysReloadObjects = false;

static void RenderObjectInspector() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Object Inspector##Neuro", nullptr)) {
        ImGui::End();
        return;
    }

    ImGui::InputText("Search##inspector", g_SearchBufferInspector, sizeof(g_SearchBufferInspector));
    ImGui::SameLine();
    ImGui::Checkbox("Always reload", &g_AlwaysReloadObjects);
    ImGui::Separator();

    static ImGuiTreeNodeFlags base_flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanAvailWidth;

    std::string searchLower = g_SearchBufferInspector;
    for (auto& c : searchLower) c = (char)tolower(c);

    for (auto& node : g_ObjectNodes) {
        // Фильтрация по поиску
        if (!searchLower.empty()) {
            std::string nameLower = node.name;
            for (auto& c : nameLower) c = (char)tolower(c);
            if (nameLower.find(searchLower) == std::string::npos) {
                continue;
            }
        }

        // Рендерим узел
        bool isOpen = ImGui::TreeNodeEx(node.name.c_str(), base_flags);
        
        // При открытии - загружаем ключи (при первом открытии или если Always reload включен)
        bool shouldReload = (isOpen && node.loadedKeys == 0) || 
                           (g_AlwaysReloadObjects && isOpen && node.expanded);
        
        if (shouldReload) {
            node.expanded = true;
            // Если Always reload - сбрасываем счётчик для перезагрузки
            if (g_AlwaysReloadObjects) {
                node.loadedKeys = 0;
            }
            extern const char* neuroImGuiGetKeys(const char*, int, int);
            extern int neuroImGuiGetKeysCount();
            neuroImGuiGetKeys(node.fullPath.c_str(), 0, 30);
            node.loadedKeys = 30;
            node.totalKeys = neuroImGuiGetKeysCount();
        }
        
        // При закрытии - обновляем флаг
        if (!isOpen && node.expanded) {
            node.expanded = false;
        }
        
        // Показываем ключи если узел открыт
        if (isOpen && node.loadedKeys > 0) {
            extern const char* neuroImGuiGetKeys(const char*, int, int);
            const char* keys = neuroImGuiGetKeys(node.fullPath.c_str(), 0, node.loadedKeys);
            
            if (keys && strlen(keys) > 0) {
                ImGui::Indent();
                
                // Разбиваем ключи по строкам
                std::string keysStr = keys;
                size_t start = 0;
                while (start < keysStr.size()) {
                    size_t end = keysStr.find('\n', start);
                    if (end == std::string::npos) end = keysStr.size();
                    
                    std::string key = keysStr.substr(start, end - start);
                    if (!key.empty()) {
                        // Показываем ключ (без проверки типа - слишком медленно)
                        // Можно добавить позже с батчем
                        ImVec4 color = ImVec4(1, 1, 1, 1);  // Белый
                        ImGui::TextColored(color, "%s", key.c_str());
                    }
                    
                    start = (end < keysStr.size()) ? end + 1 : end;
                }
                
                ImGui::Unindent();
            }

            // Кнопка "More..." если есть ещё ключи
            if (node.loadedKeys < node.totalKeys) {
                if (ImGui::Button("More...")) {
                    int newCount = node.loadedKeys + 30;
                    if (newCount > node.totalKeys) newCount = node.totalKeys;
                    node.loadedKeys = newCount;
                }
            }
            
            // Статистика
            ImGui::TextDisabled("(%d/%d keys)", node.loadedKeys, node.totalKeys);
        }

        // Всегда вызываем TreePop для открытых узлов
        if (isOpen) {
            ImGui::TreePop();
        }
    }

    ImGui::Separator();
    ImGui::TextWrapped("Click nodes to load keys. Use Search to filter.");

    ImGui::End();
}

// ============================================================================
// Variables Panel - f, tf, sf (только переменные)
// ============================================================================

static void RenderVariablesPanel() {
    ImGui::SetNextWindowPos(ImVec2(10, 420), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Variables##Neuro", nullptr)) {
        ImGui::End();
        return;
    }

    // Объявляем extern функции и переменные в правильном порядке
    extern const char* neuroImGuiGetFlags(const char*);
    static std::string fFlags = "";
    static std::string tfFlags = "";
    static std::string sfFlags = "";

    ImGui::InputText("Search##vars", g_SearchBufferVars, sizeof(g_SearchBufferVars));
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        fFlags = neuroImGuiGetFlags("f");
        tfFlags = neuroImGuiGetFlags("tf");
        sfFlags = neuroImGuiGetFlags("sf");
    }
    ImGui::Separator();

    std::string searchLower = g_SearchBufferVars;
    for (auto& c : searchLower) c = (char)tolower(c);

    static ImGuiTreeNodeFlags base_flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanAvailWidth;

    // Показываем f (temporary flags)
    if (ImGui::TreeNodeEx("f (temporary)", base_flags)) {
        ImGui::Indent();
        if (!fFlags.empty()) {
            std::string flagsStr = fFlags;
            size_t start = 0;
            while (start < flagsStr.size()) {
                size_t end = flagsStr.find('\n', start);
                if (end == std::string::npos) end = flagsStr.size();
                
                std::string line = flagsStr.substr(start, end - start);
                if (!line.empty()) {
                    size_t eqPos = line.find('=');
                    if (eqPos != std::string::npos) {
                        std::string name = line.substr(0, eqPos);
                        std::string value = line.substr(eqPos + 1);
                        
                        if (searchLower.empty() || 
                            name.find(searchLower) != std::string::npos) {
                            ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s = %s", name.c_str(), value.c_str());
                        }
                    }
                }
                start = (end < flagsStr.size()) ? end + 1 : end;
            }
        }
        ImGui::Unindent();
        ImGui::TreePop();
    }

    // Показываем tf (temporary game flags)
    if (ImGui::TreeNodeEx("tf (game)", base_flags)) {
        ImGui::Indent();
        if (!tfFlags.empty()) {
            std::string flagsStr = tfFlags;
            size_t start = 0;
            while (start < flagsStr.size()) {
                size_t end = flagsStr.find('\n', start);
                if (end == std::string::npos) end = flagsStr.size();
                
                std::string line = flagsStr.substr(start, end - start);
                if (!line.empty()) {
                    size_t eqPos = line.find('=');
                    if (eqPos != std::string::npos) {
                        std::string name = line.substr(0, eqPos);
                        std::string value = line.substr(eqPos + 1);
                        
                        if (searchLower.empty() || 
                            name.find(searchLower) != std::string::npos) {
                            ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s = %s", name.c_str(), value.c_str());
                        }
                    }
                }
                start = (end < flagsStr.size()) ? end + 1 : end;
            }
        }
        ImGui::Unindent();
        ImGui::TreePop();
    }

    // Показываем sf (system flags)
    if (ImGui::TreeNodeEx("sf (system)", base_flags)) {
        ImGui::Indent();
        if (!sfFlags.empty()) {
            std::string flagsStr = sfFlags;
            size_t start = 0;
            while (start < flagsStr.size()) {
                size_t end = flagsStr.find('\n', start);
                if (end == std::string::npos) end = flagsStr.size();
                
                std::string line = flagsStr.substr(start, end - start);
                if (!line.empty()) {
                    size_t eqPos = line.find('=');
                    if (eqPos != std::string::npos) {
                        std::string name = line.substr(0, eqPos);
                        std::string value = line.substr(eqPos + 1);
                        
                        if (searchLower.empty() || 
                            name.find(searchLower) != std::string::npos) {
                            ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s = %s", name.c_str(), value.c_str());
                        }
                    }
                }
                start = (end < flagsStr.size()) ? end + 1 : end;
            }
        }
        ImGui::Unindent();
        ImGui::TreePop();
    }

    ImGui::End();
}

void RenderImGuiWindows() {
    if (!IsImGuiVisible()) return;

    RenderObjectInspector();
    RenderVariablesPanel();
}
