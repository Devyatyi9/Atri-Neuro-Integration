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
#include <cwctype>

// RichEdit загружается динамически через LoadLibrary("Msftedit.dll") в Create()

// KiriKiri SDK
#pragma warning(push)
#pragma warning(disable: 26495)
#include "../krkr-sdk/tp_stub.h"
#pragma warning(pop)

#include "NeuroConsole.h"  // ExecuteTJSSync declaration

// Флаг для включения/выключения debug output
static std::atomic<bool> g_DebugOutputEnabled(true);

class NeuroConsole {
private:
    HWND m_hWnd;
    HWND m_hOutput;  // RichEdit для вывода
    HWND m_hInput;   // Edit для ввода команд
    HWND m_hExecute; // Кнопка Execute
    HWND m_hClear;   // Кнопка Clear
    HWND m_hAlwaysOnTop; // Чекбокс AlwaysOnTop
    HWND m_hDebugOutput; // Чекбокс Debug Output
    HWND m_hNoWrapper; // Чекбокс No Wrapper

    std::vector<std::wstring> m_history; // История команд
    int m_historyIndex;

    // Управление ресурсами для предотвращения утечек
    HMODULE m_hRichEditDll;
    std::vector<HFONT> m_fonts; // Хранилище созданных шрифтов

    static NeuroConsole* s_Instance;
    static WNDPROC s_OriginalEditProc;    // Для subclass Edit control (ввод)
    static WNDPROC s_OriginalOutputProc;  // Для subclass RichEdit control (вывод)

    // Subclass процедура для поля вывода - обработка Esc и Ctrl+A
    static LRESULT CALLBACK OutputEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_KEYDOWN) {
            if (wParam == VK_ESCAPE) {
                if (s_Instance && s_Instance->m_hWnd) {
                    ShowWindow(s_Instance->m_hWnd, SW_HIDE);
                }
                return 0;
            }
            else if (wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                SendMessage(hwnd, EM_SETSEL, 0, -1);
                return 0;
            }
        }
        // Блокируем WM_CHAR для Esc и Ctrl+A (иначе будет beep)
        if (msg == WM_CHAR && (wParam == 27 || wParam == 1)) {
            return 0;
        }
        return CallWindowProc(s_OriginalOutputProc, hwnd, msg, wParam, lParam);
    }

    // Subclass процедура для поля ввода - обработка Enter, стрелок и Ctrl+A
    static LRESULT CALLBACK InputEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_KEYDOWN) {
            if (wParam == VK_RETURN) {
                // Enter нажат - выполняем команду
                if (s_Instance) {
                    s_Instance->ExecuteCommand();
                }
                return 0;  // Не передаём Enter дальше
            }
            else if (wParam == VK_UP) {
                // Стрелка вверх - предыдущая команда из истории
                if (s_Instance) {
                    s_Instance->HistoryUp();
                }
                return 0;
            }
            else if (wParam == VK_DOWN) {
                // Стрелка вниз - следующая команда из истории
                if (s_Instance) {
                    s_Instance->HistoryDown();
                }
                return 0;
            }
            else if (wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                // Ctrl+A - выделить всё
                SendMessage(hwnd, EM_SETSEL, 0, -1);
                return 0;
            }
            else if (wParam == VK_ESCAPE) {
                // Esc - скрыть консоль
                OutputDebugStringW(L"[NeuroConsole] ESC pressed in input\n");
                if (s_Instance && s_Instance->m_hWnd) {
                    ShowWindow(s_Instance->m_hWnd, SW_HIDE);
                }
                return 0;
            }
        }
        // Блокируем WM_CHAR для Enter, Esc и Ctrl+A (иначе будет beep)
        if (msg == WM_CHAR && (wParam == '\r' || wParam == 27 || wParam == 1)) {
            return 0;
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

            // Чекбокс AlwaysOnTop
            if ((HWND)lParam == console->m_hAlwaysOnTop) {
                console->OnAlwaysOnTopChanged();
                return 0;
            }

            // Чекбокс Debug Output
            if ((HWND)lParam == console->m_hDebugOutput) {
                console->OnDebugOutputChanged();
                return 0;
            }

            // Чекбокс No Wrapper
            if ((HWND)lParam == console->m_hNoWrapper) {
                // Нет специальной обработки - состояние проверяется при выполнении
                return 0;
            }
            break;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                console->Hide();
                return 0;
            }
            break;

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
        const int checkboxHeight = 20;
        const int padding = 5;
        const int buttonWidth = 80;
        const int checkboxWidth = 100; // Уменьшил для 3 чекбоксов

        // Output занимает почти всё окно (минус место для чекбоксов внизу)
        int outputHeight = height - inputHeight - checkboxHeight - padding * 4;
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

        // Checkboxes row (внизу) - теперь 3 чекбокса
        int checkboxY = inputY + inputHeight + padding;
        SetWindowPos(m_hAlwaysOnTop, NULL, padding, checkboxY,
            checkboxWidth, checkboxHeight,
            SWP_NOZORDER);

        SetWindowPos(m_hDebugOutput, NULL, padding + checkboxWidth + padding, checkboxY,
            checkboxWidth, checkboxHeight,
            SWP_NOZORDER);

        SetWindowPos(m_hNoWrapper, NULL, padding + checkboxWidth * 2 + padding * 2, checkboxY,
            checkboxWidth, checkboxHeight,
            SWP_NOZORDER);
    }

    void OnAlwaysOnTopChanged() {
        BOOL checked = (SendMessage(m_hAlwaysOnTop, BM_GETCHECK, 0, 0) == BST_CHECKED);
        HWND insertAfter = checked ? HWND_TOPMOST : HWND_NOTOPMOST;
        SetWindowPos(m_hWnd, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    void OnDebugOutputChanged() {
        BOOL checked = (SendMessage(m_hDebugOutput, BM_GETCHECK, 0, 0) == BST_CHECKED);
        g_DebugOutputEnabled.store(checked == TRUE);
    }

    void HistoryUp() {
        if (m_history.empty()) return;

        if (m_historyIndex > 0) {
            m_historyIndex--;
        }

        if (m_historyIndex < (int)m_history.size()) {
            SetWindowTextW(m_hInput, m_history[m_historyIndex].c_str());
            // Курсор в конец
            SendMessage(m_hInput, EM_SETSEL, -1, -1);
        }
    }

    void HistoryDown() {
        if (m_history.empty()) return;

        if (m_historyIndex < (int)m_history.size()) {
            m_historyIndex++;
        }

        if (m_historyIndex < (int)m_history.size()) {
            SetWindowTextW(m_hInput, m_history[m_historyIndex].c_str());
        } else {
            // После последней команды - пустая строка
            SetWindowTextW(m_hInput, L"");
        }
        // Курсор в конец
        SendMessage(m_hInput, EM_SETSEL, -1, -1);
    }

    void ExecuteCommand() {
        // Получаем текст команды безопасно
        int textLength = GetWindowTextLengthW(m_hInput);
        if (textLength == 0) return;
        
        if (textLength > MAX_COMMAND_LENGTH) {
            AppendOutput(L"Error: Command too long\n", RGB(255, 0, 0));
            return;
        }

        std::wstring command;
        command.resize(textLength + 1);
        GetWindowTextW(m_hInput, &command[0], textLength + 1);
        command.resize(wcslen(command.c_str()));

        // Добавляем в историю
        m_history.push_back(command);
        m_historyIndex = (int)m_history.size();

        // Выводим команду
        AppendOutput(L">> ", RGB(0, 150, 0));
        AppendOutput(command.c_str(), RGB(0, 0, 0));
        AppendOutput(L"\n", RGB(0, 0, 0));

        // Выполняем TJS код
        ExecuteTJSCommand(command.c_str());

        // Очищаем поле ввода
        SetWindowTextW(m_hInput, L"");

        // Фокус обратно на input
        SetFocus(m_hInput);
    }

    void ExecuteTJSCommand(const wchar_t* code) {
        if (!code || wcslen(code) == 0) return;
        std::wstring cleanCode = code;

        // Безопасная очистка с использованием iswspace()
        while (!cleanCode.empty() && (cleanCode.back() == L';' || iswspace(cleanCode.back()))) {
            cleanCode.pop_back();
        }
        if (cleanCode.empty()) return;

        // Проверяем состояние чекбокса "No Wrapper"
        BOOL noWrapperChecked = (SendMessage(m_hNoWrapper, BM_GETCHECK, 0, 0) == BST_CHECKED);

        std::wstring finalCode;
        if (noWrapperChecked) {
            // Выполняем код без обёртки
            finalCode = cleanCode;
        } else {
            // Оборачиваем в try-catch для безопасности
            finalCode = L"(function(){ try { return (" + cleanCode + L"); } catch(e) { return \"!!!TJS_EXCEPTION!!! \" + e.message; } })()";
        }

        tTJSVariant result;
        // Вызываем синхронную функцию из dllmain
        if (ExecuteTJSSync(finalCode.c_str(), &result)) {

            // 1. Проверяем на исключение (наша обертка !!!TJS_EXCEPTION!!!)
            if (result.Type() == tvtString) {
				// Используем .GetString() для получения указателя на внутреннюю строку
                std::wstring strVal = result.GetString();
                if (strVal.find(L"!!!TJS_EXCEPTION!!!") == 0) {
                    std::wstring errMsg = strVal.substr(20);
                    AppendOutput((L"TJS Error: " + errMsg + L"\n").c_str(), RGB(255, 50, 50));
                    return;
                }
            }

            // 2. Определяем тип для вывода
            std::wstring typeStr = L"Unknown";
            switch (result.Type()) {
            case tvtVoid:    typeStr = L"Void";    break;
            case tvtObject:  typeStr = L"Object";  break;
            case tvtString:  typeStr = L"String";  break;
            case tvtInteger: typeStr = L"Integer"; break;
            case tvtReal:    typeStr = L"Real";    break;
            case tvtOctet:   typeStr = L"Octet";   break;
            }

            // 3. Безопасно конвертируем результат в строку
            // Используем явное приведение к ttstr, а затем к c_str()
            std::wstring valStr;
            if (result.Type() == tvtVoid) {
                valStr = L"void";
            }
            else {
                // Самый надежный способ конвертации в KiriKiri SDK
                valStr = ttstr(result).c_str();
            }

            std::wstring output = L"Result [" + typeStr + L"]: " + valStr + L"\n";
            AppendOutput(output.c_str(), RGB(0, 255, 0)); // Зеленый для успеха

        }
        else {
            AppendOutput(L"Critical Error: TJS execution failed or timed out.\n", RGB(255, 0, 0));
        }
    }

    void ClearOutput() {
        SetWindowTextW(m_hOutput, L"");
    }

public:
    void AppendOutput(const wchar_t* text, COLORREF color) {
        if (!m_hOutput) return;

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

NeuroConsole() : m_hWnd(NULL), m_hOutput(NULL), m_hInput(NULL),
        m_hExecute(NULL), m_hClear(NULL), m_hAlwaysOnTop(NULL), m_hDebugOutput(NULL),
        m_hNoWrapper(NULL), m_historyIndex(0), m_hRichEditDll(NULL) {
        s_Instance = this;
    }

    bool Create() {
        // Загружаем RichEdit DLL
        m_hRichEditDll = LoadLibraryW(L"Msftedit.dll");
        if (!m_hRichEditDll) {
            return false;
        }

        // Регистрируем класс окна
        WNDCLASSW wc = {};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"NeuroConsoleClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);

        // Создаём независимое окно
        m_hWnd = CreateWindowExW(
            WS_EX_TOOLWINDOW,
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

        // Subclass RichEdit для обработки Esc и Ctrl+A
        s_OriginalOutputProc = (WNDPROC)SetWindowLongPtr(m_hOutput, GWLP_WNDPROC, (LONG_PTR)OutputEditProc);

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
            L"Run",
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

        // Создаём чекбоксы
        m_hAlwaysOnTop = CreateWindowW(
            L"BUTTON",
            L"Always On Top",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 0, 0,
            m_hWnd, NULL, GetModuleHandle(NULL), NULL
        );
        // По умолчанию выключён (окно создано без WS_EX_TOPMOST)
        SendMessage(m_hAlwaysOnTop, BM_SETCHECK, BST_UNCHECKED, 0);

        m_hDebugOutput = CreateWindowW(
            L"BUTTON",
            L"Debug Output",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 0, 0,
            m_hWnd, NULL, GetModuleHandle(NULL), NULL
        );
        // По умолчанию включён
        SendMessage(m_hDebugOutput, BM_SETCHECK, BST_CHECKED, 0);

        m_hNoWrapper = CreateWindowW(
            L"BUTTON",
            L"No Wrapper",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 0, 0,
            m_hWnd, NULL, GetModuleHandle(NULL), NULL
        );
        // По умолчанию выключен
        SendMessage(m_hNoWrapper, BM_SETCHECK, BST_UNCHECKED, 0);

        // Устанавливаем шрифт
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        
        // Сохраняем шрифт для последующего удаления
        m_fonts.push_back(hFont);
        
        SendMessageW(m_hInput, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(m_hExecute, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(m_hClear, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(m_hAlwaysOnTop, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(m_hDebugOutput, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(m_hNoWrapper, WM_SETFONT, (WPARAM)hFont, TRUE);

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
        // Восстанавливаем оригинальные процедуры перед удалением
        if (m_hOutput && s_OriginalOutputProc) {
            SetWindowLongPtr(m_hOutput, GWLP_WNDPROC, (LONG_PTR)s_OriginalOutputProc);
        }
        if (m_hInput && s_OriginalEditProc) {
            SetWindowLongPtr(m_hInput, GWLP_WNDPROC, (LONG_PTR)s_OriginalEditProc);
        }

        // Удаляем все созданные шрифты
        for (HFONT font : m_fonts) {
            DeleteObject(font);
        }
        m_fonts.clear();

        // Освобождаем RichEdit DLL
        if (m_hRichEditDll) {
            FreeLibrary(m_hRichEditDll);
            m_hRichEditDll = nullptr;
        }

        if (m_hWnd) {
            DestroyWindow(m_hWnd);
        }
    }
};

NeuroConsole* NeuroConsole::s_Instance = nullptr;
WNDPROC NeuroConsole::s_OriginalEditProc = nullptr;
WNDPROC NeuroConsole::s_OriginalOutputProc = nullptr;

// ============================================================================
// Console Thread - создаёт окно и запускает message loop
// ============================================================================

static std::atomic<NeuroConsole*> g_Console{nullptr};
static std::thread g_ConsoleThread;
static std::atomic<bool> g_ConsoleThreadRunning{false};
static std::atomic<bool> g_ConsoleReady{false};
static std::atomic<HWND> g_ConsoleHWND{nullptr};  // Для кросс-поточной коммуникации
static std::mutex g_ConsoleMutex;  // Для thread-safe операций

// Custom messages для управления консолью из других потоков
// (определены в NeuroConsole.h)

// Структура для передачи данных через PostMessage
struct ConsoleAppendData {
    wchar_t* text;
    COLORREF color;
};

// Поток консоли с собственным message loop
void ConsoleThreadProc() {
    // Создаём консоль В ЭТОМ потоке
    NeuroConsole* console = new NeuroConsole();
    g_Console.store(console);
    if (!g_Console.load()->Create()) {
        delete g_Console.load();
        g_Console.store(nullptr);
        g_ConsoleReady.store(false);
        return;
    }

    g_ConsoleHWND.store(g_Console.load()->GetHWND());
    g_ConsoleReady.store(true);

    // Message loop для консоли - ЭТО КРИТИЧЕСКИ ВАЖНО!
    // Без этого окно не будет обрабатывать события
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        // Обрабатываем наши custom messages
        if (msg.hwnd == g_ConsoleHWND.load() || msg.hwnd == NULL) {
            if (msg.message == WM_CONSOLE_TOGGLE) {
                NeuroConsole* console = g_Console.load();
                if (console) console->Toggle();
                continue;
            }
            if (msg.message == WM_CONSOLE_SHUTDOWN) {
                break;  // Выход из message loop
            }
            if (msg.message == WM_CONSOLE_APPEND) {
                std::lock_guard<std::mutex> lock(g_ConsoleMutex);
                ConsoleAppendData* data = (ConsoleAppendData*)msg.wParam;
                NeuroConsole* console = g_Console.load();
                if (data && console) {
                    console->AppendOutput(data->text, data->color);
                    delete[] data->text;
                    delete data;
                }
                continue;
            }
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup после выхода из loop
    NeuroConsole* consoleToDelete = g_Console.load();
    if (consoleToDelete) {
        delete consoleToDelete;
        g_Console.store(nullptr);
    }
    g_ConsoleHWND.store(nullptr);
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
    NeuroConsole* console = g_Console.load();
    return console && console->IsVisible();
}

void AppendToNeuroConsole(const wchar_t* text, unsigned int color) {
    if (!g_ConsoleReady.load() || !g_Console.load() || !text) return;

    // Проверяем, включён ли debug output
    if (!g_DebugOutputEnabled.load()) return;

    // Безопасная проверка длины текста
    size_t len = wcslen(text);
    if (len == 0) return;
    
    if (len > MAX_CONSOLE_TEXT_LENGTH) {
        len = MAX_CONSOLE_TEXT_LENGTH;
    }

    // Копируем текст (будет удалён в обработчике)
    wchar_t* textCopy = new wchar_t[len + 1];
    wcscpy_s(textCopy, len + 1, text);
    textCopy[len] = L'\0';

    ConsoleAppendData* data = new ConsoleAppendData();
    data->text = textCopy;
    data->color = (COLORREF)color;

    // Отправляем в поток консоли с проверкой
    HWND consoleHwnd = g_ConsoleHWND.load();
    if (consoleHwnd) {
        if (!PostMessage(consoleHwnd, WM_CONSOLE_APPEND, (WPARAM)data, 0)) {
            // Если PostMessage не сработал, очищаем память
            delete[] textCopy;
            delete data;
        }
    } else {
        delete[] textCopy;
        delete data;
    }
}