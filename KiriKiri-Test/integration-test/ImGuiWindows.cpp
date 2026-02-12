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

// Объекты для Object Inspector
static ObjectNodeData g_ObjectNodes[] = {
    {"kag", "kag", false, 0, 0},
    {"kag.conductor", "kag.conductor", false, 0, 0},
    {"System", "System", false, 0, 0},
    {"global", "global", false, 0, 0}
};

static void RenderObjectInspector() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Object Inspector##Neuro", nullptr)) {
        ImGui::End();
        return;
    }

    ImGui::InputText("Search##inspector", g_SearchBufferInspector, sizeof(g_SearchBufferInspector));
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
        
        // Обновляем состояние раскрытия
        if (isOpen && !node.expanded) {
            node.expanded = true;
            // Показываем загруженные ключи
            if (node.expanded && node.loadedKeys > 0) {
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
                            // Получаем тип ключа
                            extern const char* neuroImGuiGetPropertyType(const char*, const char*);
                            const char* type = neuroImGuiGetPropertyType(node.fullPath.c_str(), key.c_str());
                            
                            // Определяем префикс и цвет
                            ImVec4 color = ImVec4(1, 1, 1, 1);  // Белый - значение
                            std::string prefix = "[v] ";
                            
                            if (strcmp(type, "function") == 0) {
                                color = ImVec4(0.5f, 0.5f, 1, 1);  // Синий - функция
                                prefix = "[f] ";
                            } else if (strcmp(type, "object") == 0) {
                                color = ImVec4(1, 0.9f, 0.3f, 1);  // Жёлтый - объект
                                prefix = "[o] ";
                            }
                            
                            // Показываем ключ с типом
                            ImGui::TextColored(color, "%s%s", prefix.c_str(), key.c_str());
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

    ImGui::InputText("Search##vars", g_SearchBufferVars, sizeof(g_SearchBufferVars));
    ImGui::Separator();

    std::string searchLower = g_SearchBufferVars;
    for (auto& c : searchLower) c = (char)tolower(c);

    // Получаем переменные
    extern const char* neuroImGuiGetFlags(const char*);
    
    static std::string fFlags, tfFlags, sfFlags;
    static bool fLoaded = false, tfLoaded = false, sfLoaded = false;
    
    if (!fLoaded) {
        fFlags = neuroImGuiGetFlags("f");
        fLoaded = true;
    }
    if (!tfLoaded) {
        tfFlags = neuroImGuiGetFlags("tf");
        tfLoaded = true;
    }
    if (!sfLoaded) {
        sfFlags = neuroImGuiGetFlags("sf");
        sfLoaded = true;
    }

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
