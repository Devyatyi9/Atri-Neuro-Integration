#pragma once

#include <windows.h>
#include <richedit.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <cwctype>

// Forward declarations
class tTJSVariant;

// ============================================================================
// ScriptingWindow - Многострочный TJS редактор
// ============================================================================

class ScriptingWindow {
private:
    HWND m_hWnd;
    HWND m_hRichEdit;
    HWND m_hExecute;
    HWND m_hClear;
    HWND m_hClose;

    // Управление ресурсами
    HMODULE m_hRichEditDll;
    std::vector<HFONT> m_fonts;

    static ScriptingWindow* s_Instance;
    static WNDPROC s_OriginalRichEditProc;

    // Subclass процедура для RichEdit
    static LRESULT CALLBACK RichEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Window Procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void OnResize(int width, int height);
    void ExecuteScript();
    void ClearContent();
public:
    void AppendResult(const wchar_t* text, COLORREF color);
    ScriptingWindow();
    ~ScriptingWindow();

    bool Create();
    HWND GetHWND() const { return m_hWnd; }
    void Show();
    void Hide();
    void Toggle();
    bool IsVisible() const { return m_hWnd && IsWindowVisible(m_hWnd); }
};

// Глобальная переменная для доступа
extern std::atomic<ScriptingWindow*> g_ScriptingWindow;