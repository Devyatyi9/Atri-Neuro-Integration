// ============================================================================
// Класс: Консоль для выполнения TJS команд
// ============================================================================

#include <windows.h>
#include <richedit.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

// RichEdit загружается динамически через LoadLibrary("Msftedit.dll") в Create()

// KiriKiri SDK
#pragma warning(push)
#pragma warning(disable: 26495)
#include "../krkr-sdk/tp_stub.h"
#pragma warning(pop)

class NeuroConsole {
private:
    HWND m_hWnd;
    HWND m_hOutput;  // RichEdit для вывода
    HWND m_hInput;   // Edit для ввода команд
    HWND m_hExecute; // Кнопка Execute
    HWND m_hClear;   // Кнопка Clear

    std::vector<std::wstring> m_history; // История команд
    int m_historyIndex;

    static NeuroConsole* s_Instance;
    static WNDPROC s_OriginalEditProc;  // Для subclass Edit control

    // Subclass процедура для поля ввода - обработка Enter
    static LRESULT CALLBACK InputEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
            // Enter нажат - выполняем команду
            if (s_Instance) {
                s_Instance->ExecuteCommand();
            }
            return 0;  // Не передаём Enter дальше (иначе будет beep)
        }
        // Все остальные сообщения - оригинальной процедуре
        return CallWindowProc(s_OriginalEditProc, hwnd, msg, wParam, lParam);
    }

    // Window Procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        NeuroConsole* console = (NeuroConsole*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

        if (msg == WM_CREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            console = (NeuroConsole*)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)console);
            return 0;
        }

        if (!console) {
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        switch (msg) {
        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            console->OnResize(width, height);
            return 0;
        }

        case WM_COMMAND: {
            // Клик на кнопку Execute
            if ((HWND)lParam == console->m_hExecute) {
                console->ExecuteCommand();
                return 0;
            }

            // Клик на кнопку Clear
            if ((HWND)lParam == console->m_hClear) {
                console->ClearOutput();
                return 0;
            }
            break;
        }

        case WM_CLOSE:
            console->Hide();
            return 0;

        case WM_DESTROY:
            return 0;
        }

        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    void OnResize(int width, int height) {
        const int buttonHeight = 30;
        const int inputHeight = 25;
        const int padding = 5;
        const int buttonWidth = 80;

        // Output занимает почти всё окно
        int outputHeight = height - inputHeight - buttonHeight - padding * 3;
        SetWindowPos(m_hOutput, NULL, padding, padding,
            width - padding * 2, outputHeight,
            SWP_NOZORDER);

        // Input field
        int inputY = outputHeight + padding * 2;
        SetWindowPos(m_hInput, NULL, padding, inputY,
            width - buttonWidth * 2 - padding * 4, inputHeight,
            SWP_NOZORDER);

        // Execute button
        int executeX = width - buttonWidth * 2 - padding * 2;
        SetWindowPos(m_hExecute, NULL, executeX, inputY,
            buttonWidth, inputHeight,
            SWP_NOZORDER);

        // Clear button
        int clearX = width - buttonWidth - padding;
        SetWindowPos(m_hClear, NULL, clearX, inputY,
            buttonWidth, inputHeight,
            SWP_NOZORDER);
    }

    void ExecuteCommand() {
        // Получаем текст команды
        wchar_t command[4096];
        GetWindowTextW(m_hInput, command, 4096);

        if (wcslen(command) == 0) return;

        // Добавляем в историю
        m_history.push_back(command);
        m_historyIndex = (int)m_history.size();

        // Выводим команду
        AppendOutput(L">> ", RGB(0, 150, 0));
        AppendOutput(command, RGB(0, 0, 0));
        AppendOutput(L"\n", RGB(0, 0, 0));

        // Выполняем TJS код
        ExecuteTJSCommand(command);

        // Очищаем поле ввода
        SetWindowTextW(m_hInput, L"");

        // Фокус обратно на input
        SetFocus(m_hInput);
    }

    void ExecuteTJSCommand(const wchar_t* code) {
        // Получаем глобальный объект TJS
        iTJSDispatch2* global = TVPGetScriptDispatch();
        if (!global) {
            AppendOutput(L"Error: Cannot get script dispatch\n", RGB(255, 0, 0));
            return;
        }

        // ПРИМЕЧАНИЕ: TJS исключения (eTJS) НЕ работают в плагинах!
        // Они обрабатываются внутри движка KiriKiri.
        // Поэтому try-catch с eTJS здесь бесполезен.

        tTJSVariant result;

        // Выполняем код
        // TVPExecuteExpression не бросает исключения в плагине,
        // ошибки выводятся в Debug.console движка
        TVPExecuteExpression(ttstr(code), &result);

        // Выводим результат
        if (result.Type() != tvtVoid) {
            ttstr resultStr = result;
            AppendOutput(L"Result: ", RGB(0, 0, 255));
            AppendOutput(resultStr.c_str(), RGB(0, 0, 0));
            AppendOutput(L"\n", RGB(0, 0, 0));
        }
        else {
            AppendOutput(L"OK\n", RGB(0, 150, 0));
        }

        global->Release();
    }

    void AppendOutput(const wchar_t* text, COLORREF color) {
        // Переходим в конец текста
        CHARRANGE cr;
        cr.cpMin = -1;
        cr.cpMax = -1;
        SendMessageW(m_hOutput, EM_EXSETSEL, 0, (LPARAM)&cr);

        // Устанавливаем цвет
        CHARFORMAT2W cf;
        ZeroMemory(&cf, sizeof(cf));
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR;
        cf.crTextColor = color;
        SendMessageW(m_hOutput, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

        // Добавляем текст
        SendMessageW(m_hOutput, EM_REPLACESEL, FALSE, (LPARAM)text);

        // Прокручиваем вниз
        SendMessageW(m_hOutput, WM_VSCROLL, SB_BOTTOM, 0);
    }

    void ClearOutput() {
        SetWindowTextW(m_hOutput, L"");
    }

public:
    NeuroConsole() : m_hWnd(NULL), m_hOutput(NULL), m_hInput(NULL),
        m_hExecute(NULL), m_hClear(NULL), m_historyIndex(0) {
        s_Instance = this;
    }

    bool Create() {
        // Загружаем RichEdit DLL
        LoadLibraryW(L"Msftedit.dll");

        // Регистрируем класс окна
        WNDCLASSW wc = {};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"NeuroConsoleClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);

        // Создаём главное окно
        m_hWnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"NeuroConsoleClass",
            L"Neuro Console - TJS Command Executor",
            WS_OVERLAPPEDWINDOW,
            100, 100, 800, 600,
            NULL, NULL, GetModuleHandle(NULL), this
        );

        if (!m_hWnd) return false;

        // Создаём RichEdit для вывода
        m_hOutput = CreateWindowExW(
            0,
            MSFTEDIT_CLASS,
            L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 0, 0,
            m_hWnd, NULL, GetModuleHandle(NULL), NULL
        );

        // Устанавливаем шрифт для output
        CHARFORMAT2W cf;
        ZeroMemory(&cf, sizeof(cf));
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET;
        wcscpy_s(cf.szFaceName, L"Consolas");
        cf.yHeight = 200; // 10pt * 20
        cf.bCharSet = DEFAULT_CHARSET;
        SendMessageW(m_hOutput, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);

        // Создаём Edit для ввода
        m_hInput = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0,
            m_hWnd, NULL, GetModuleHandle(NULL), NULL
        );

        // Subclass Edit для обработки Enter
        s_OriginalEditProc = (WNDPROC)SetWindowLongPtr(m_hInput, GWLP_WNDPROC, (LONG_PTR)InputEditProc);

        // Создаём кнопки
        m_hExecute = CreateWindowW(
            L"BUTTON",
            L"Execute",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            m_hWnd, NULL, GetModuleHandle(NULL), NULL
        );

        m_hClear = CreateWindowW(
            L"BUTTON",
            L"Clear",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            m_hWnd, NULL, GetModuleHandle(NULL), NULL
        );

        // Устанавливаем шрифт
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        SendMessageW(m_hInput, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(m_hExecute, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(m_hClear, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Приветственное сообщение
        AppendOutput(L"=== Neuro Console Ready ===\n", RGB(0, 100, 200));
        AppendOutput(L"Enter TJS commands below and press Enter or click Execute.\n\n", RGB(100, 100, 100));
        AppendOutput(L"Examples:\n", RGB(0, 0, 0));
        AppendOutput(L"  neuroTest()\n", RGB(0, 150, 0));
        AppendOutput(L"  Debug.message('Hello')\n", RGB(0, 150, 0));
        AppendOutput(L"  2 + 2\n", RGB(0, 150, 0));
        AppendOutput(L"  System.title\n\n", RGB(0, 150, 0));

        return true;
    }

    HWND GetHWND() const { return m_hWnd; }

    void Show() {
        if (m_hWnd) {
            ShowWindow(m_hWnd, SW_SHOW);
            SetForegroundWindow(m_hWnd);
            SetFocus(m_hInput);
        }
    }

    void Hide() {
        if (m_hWnd) {
            ShowWindow(m_hWnd, SW_HIDE);
        }
    }

    void Toggle() {
        if (m_hWnd) {
            if (IsWindowVisible(m_hWnd)) {
                Hide();
            }
            else {
                Show();
            }
        }
    }

    bool IsVisible() const {
        return m_hWnd && IsWindowVisible(m_hWnd);
    }

    ~NeuroConsole() {
        if (m_hWnd) {
            DestroyWindow(m_hWnd);
        }
    }
};

NeuroConsole* NeuroConsole::s_Instance = nullptr;
WNDPROC NeuroConsole::s_OriginalEditProc = nullptr;

// ============================================================================
// Console Thread - создаёт окно и запускает message loop
// ============================================================================

static NeuroConsole* g_Console = nullptr;
static std::thread g_ConsoleThread;
static std::atomic<bool> g_ConsoleThreadRunning(false);
static std::atomic<bool> g_ConsoleReady(false);
static HWND g_ConsoleHWND = nullptr;  // Для кросс-поточной коммуникации

// Custom messages для управления консолью из других потоков
static const UINT WM_CONSOLE_TOGGLE = WM_USER + 200;
static const UINT WM_CONSOLE_SHUTDOWN = WM_USER + 201;

// Поток консоли с собственным message loop
void ConsoleThreadProc() {
    // Создаём консоль В ЭТОМ потоке
    g_Console = new NeuroConsole();
    if (!g_Console->Create()) {
        delete g_Console;
        g_Console = nullptr;
        g_ConsoleReady.store(false);
        return;
    }

    g_ConsoleHWND = g_Console->GetHWND();
    g_ConsoleReady.store(true);

    // Message loop для консоли - ЭТО КРИТИЧЕСКИ ВАЖНО!
    // Без этого окно не будет обрабатывать события
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        // Обрабатываем наши custom messages
        if (msg.hwnd == g_ConsoleHWND || msg.hwnd == NULL) {
            if (msg.message == WM_CONSOLE_TOGGLE) {
                if (g_Console) g_Console->Toggle();
                continue;
            }
            if (msg.message == WM_CONSOLE_SHUTDOWN) {
                break;  // Выход из message loop
            }
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup после выхода из loop
    if (g_Console) {
        delete g_Console;
        g_Console = nullptr;
    }
    g_ConsoleHWND = nullptr;
    g_ConsoleReady.store(false);
}

// ============================================================================
// Экспортируемые функции для использования из dllmain.cpp
// ============================================================================

void InitNeuroConsole() {
    if (g_ConsoleThreadRunning.load()) {
        return;  // Уже запущен
    }

    g_ConsoleThreadRunning.store(true);
    g_ConsoleThread = std::thread(ConsoleThreadProc);

    // Ждём готовности консоли (до 2 секунд)
    for (int i = 0; i < 20 && !g_ConsoleReady.load(); i++) {
        Sleep(100);
    }
}

void ShutdownNeuroConsole() {
    if (!g_ConsoleThreadRunning.load()) {
        return;
    }

    // Отправляем сообщение о закрытии в поток консоли
    if (g_ConsoleHWND) {
        PostMessage(g_ConsoleHWND, WM_CONSOLE_SHUTDOWN, 0, 0);
    }

    // Ждём завершения потока
    if (g_ConsoleThread.joinable()) {
        g_ConsoleThread.join();
    }

    g_ConsoleThreadRunning.store(false);
}

void ToggleNeuroConsole() {
    // Если поток не запущен - запускаем
    if (!g_ConsoleThreadRunning.load()) {
        InitNeuroConsole();
    }

    // Отправляем сообщение toggle в поток консоли
    if (g_ConsoleHWND) {
        PostMessage(g_ConsoleHWND, WM_CONSOLE_TOGGLE, 0, 0);
    }
}

bool IsNeuroConsoleVisible() {
    return g_Console && g_Console->IsVisible();
}