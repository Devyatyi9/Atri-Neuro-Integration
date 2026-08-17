// ============================================================================
// ScriptingWindow: multi-line TJS editor.
// ============================================================================

#include <richedit.h>
#include <windows.h>
#include <atomic>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// KiriKiri SDK
#pragma warning(push)
#pragma warning(disable : 26495)
#include "../krkr-sdk/tp_stub.h"
#pragma warning(pop)

#include "NeuroConsole.h"
#include "ScriptingWindow.h"
#include "TJSExecutor.h"

// Global variables.
std::atomic<ScriptingWindow *> g_ScriptingWindow{nullptr};
ScriptingWindow *ScriptingWindow::s_Instance = nullptr;
WNDPROC ScriptingWindow::s_OriginalRichEditProc = nullptr;

// ============================================================================
// Class method implementations.
// ============================================================================

ScriptingWindow::ScriptingWindow()
    : m_hWnd(NULL),
      m_hRichEdit(NULL),
      m_hExecute(NULL),
      m_hClear(NULL),
      m_hClose(NULL),
      m_hRichEditDll(NULL) {
	s_Instance = this;
}

ScriptingWindow::~ScriptingWindow() {
	// Restore the original procedure.
	if (m_hRichEdit && s_OriginalRichEditProc) {
		SetWindowLongPtr(m_hRichEdit, GWLP_WNDPROC,
		                 (LONG_PTR)s_OriginalRichEditProc);
	}

	// Delete all created fonts.
	for (HFONT font : m_fonts) {
		DeleteObject(font);
	}
	m_fonts.clear();

	// Release the RichEdit DLL.
	if (m_hRichEditDll) {
		FreeLibrary(m_hRichEditDll);
	}

	if (m_hWnd) {
		DestroyWindow(m_hWnd);
	}
}

bool ScriptingWindow::Create() {
	// Load the RichEdit DLL.
	m_hRichEditDll = LoadLibraryW(L"Msftedit.dll");
	if (!m_hRichEditDll)
		return false;

	// Register the window class.
	WNDCLASSW wc = {};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = GetModuleHandle(NULL);
	wc.lpszClassName = L"ScriptingWindowClass";
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

	if (!RegisterClassW(&wc)) {
		DWORD error = GetLastError();
		if (error != ERROR_CLASS_ALREADY_EXISTS) {
			return false;
		}
	}

	// Create the window.
	m_hWnd = CreateWindowExW(WS_EX_OVERLAPPEDWINDOW, L"ScriptingWindowClass",
	                         L"Scripting Window - Multi-line TJS Editor",
	                         WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
	                         800, 600, NULL, NULL, GetModuleHandle(NULL), this);

	if (!m_hWnd)
		return false;

	// Create a RichEdit control for multi-line editing.
	m_hRichEdit =
	    CreateWindowExW(0, MSFTEDIT_CLASS, L"",
	                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
	                        ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
	                    0, 0, 0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);

	if (!m_hRichEdit)
		return false;

	// Subclass RichEdit for custom Enter handling.
	s_OriginalRichEditProc = (WNDPROC)SetWindowLongPtr(m_hRichEdit, GWLP_WNDPROC,
	                                                   (LONG_PTR)RichEditProc);

	// Set the font.
	HFONT hFont =
	    CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
	                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
	                DEFAULT_PITCH | FF_DONTCARE, L"Consolas");

	m_fonts.push_back(hFont);
	SendMessageW(m_hRichEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

	// Create the buttons.
	m_hExecute = CreateWindowW(L"BUTTON", L"Execute",
	                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
	                           m_hWnd, NULL, GetModuleHandle(NULL), NULL);

	m_hClear =
	    CreateWindowW(L"BUTTON", L"Clear", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
	                  0, 0, 0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);

	m_hClose =
	    CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
	                  0, 0, 0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);

	// Set the button fonts.
	SendMessageW(m_hExecute, WM_SETFONT, (WPARAM)hFont, TRUE);
	SendMessageW(m_hClear, WM_SETFONT, (WPARAM)hFont, TRUE);
	SendMessageW(m_hClose, WM_SETFONT, (WPARAM)hFont, TRUE);

	// Set the initial text.
	SetWindowTextW(
	    m_hRichEdit,
	    L"// Scripting Window - Multi-line TJS Editor\n"
	    L"// Enter - перенос строки, Execute button - выполнить скрипт\n"
	    L"// Examples:\n"
	    L"(function() {\n"
	    L"    var keys = Scripts.getObjectKeys(kag);\n"
	    L"    return \"kag has \" + keys.length + \" properties: \" + "
	    L"keys.join(\", \");\n"
	    L"})()\n");

	// Lay out the controls.
	OnResize(800, 600);

	return true;
}

void ScriptingWindow::OnResize(int width, int height) {
	if (!m_hRichEdit || !m_hExecute || !m_hClear || !m_hClose)
		return;

	int const buttonHeight = 30;
	int const buttonWidth = 80;
	int const padding = 5;
	int const buttonSpacing = 5;

	// RichEdit fills the area above the bottom button panel.
	int editHeight = height - buttonHeight - padding * 2;
	SetWindowPos(m_hRichEdit, NULL, padding, padding, width - padding * 2,
	             editHeight, SWP_NOZORDER);

	// Buttons at the bottom.
	int buttonY = height - buttonHeight - padding;
	int totalButtonWidth = buttonWidth * 3 + buttonSpacing * 2;
	int startX = (width - totalButtonWidth) / 2;

	SetWindowPos(m_hClear, NULL, startX, buttonY, buttonWidth, buttonHeight,
	             SWP_NOZORDER);
	SetWindowPos(m_hExecute, NULL, startX + buttonWidth + buttonSpacing, buttonY,
	             buttonWidth, buttonHeight, SWP_NOZORDER);
	SetWindowPos(m_hClose, NULL, startX + (buttonWidth + buttonSpacing) * 2,
	             buttonY, buttonWidth, buttonHeight, SWP_NOZORDER);
}

void ScriptingWindow::ExecuteScript() {
	if (!m_hRichEdit)
		return;

	// Get text from RichEdit.
	int textLength = GetWindowTextLengthW(m_hRichEdit);
	if (textLength == 0)
		return;

	std::wstring script;
	script.resize(textLength + 1);
	GetWindowTextW(m_hRichEdit, &script[0], textLength + 1);
	script.resize(wcslen(script.c_str()));

	if (script.empty())
		return;

	// Execute the multi-line script through TVPExecuteScript.
	tTJSVariant result;
	if (ExecuteTJSSync(script.c_str(), &result)) {
		// Check for an exception.
		if (result.Type() == tvtString) {
			std::wstring strVal = result.GetString();
			if (strVal.find(L"!!!TJS_EXCEPTION!!!") == 0) {
				std::wstring errMsg = strVal.substr(20);
				AppendResult((L"TJS Error: " + errMsg + L"\n").c_str(),
				             RGB(255, 50, 50));
				return;
			}
		}

		// Determine the result type.
		std::wstring typeStr = L"Unknown";
		switch (result.Type()) {
			case tvtVoid:
				typeStr = L"Void";
				break;
			case tvtObject:
				typeStr = L"Object";
				break;
			case tvtString:
				typeStr = L"String";
				break;
			case tvtInteger:
				typeStr = L"Integer";
				break;
			case tvtReal:
				typeStr = L"Real";
				break;
			case tvtOctet:
				typeStr = L"Octet";
				break;
		}

		std::wstring valStr;
		if (result.Type() == tvtVoid) {
			valStr = L"void";
		} else {
			valStr = ttstr(result).c_str();
		}

		std::wstring output = L"Result [" + typeStr + L"]: " + valStr + L"\n";
		AppendResult(output.c_str(), RGB(0, 200, 0));  // Green for success.
	} else {
		AppendResult(L"Critical Error: TJS execution failed or timed out.\n",
		             RGB(255, 0, 0));
	}
}

void ScriptingWindow::ClearContent() {
	if (m_hRichEdit) {
		SetWindowTextW(m_hRichEdit, L"");
	}
}

void ScriptingWindow::AppendResult(wchar_t const *text, COLORREF color) {
	if (!m_hRichEdit)
		return;

	// Append text to the end of RichEdit.
	CHARRANGE cr;
	cr.cpMin = -1;
	cr.cpMax = -1;
	SendMessageW(m_hRichEdit, EM_EXSETSEL, 0, (LPARAM)&cr);

	// Set the color.
	CHARFORMAT2W cf;
	ZeroMemory(&cf, sizeof(cf));
	cf.cbSize = sizeof(cf);
	cf.dwMask = CFM_COLOR;
	cf.crTextColor = color;
	SendMessageW(m_hRichEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

	// Append text.
	SendMessageW(m_hRichEdit, EM_REPLACESEL, FALSE, (LPARAM)text);

	// Scroll to the bottom.
	SendMessageW(m_hRichEdit, WM_VSCROLL, SB_BOTTOM, 0);
}

// ============================================================================
// Static functions.
// ============================================================================

LRESULT CALLBACK ScriptingWindow::RichEditProc(HWND hwnd,
                                               UINT msg,
                                               WPARAM wParam,
                                               LPARAM lParam) {
	if (msg == WM_KEYDOWN) {
		if (wParam == VK_ESCAPE) {
			if (s_Instance && s_Instance->m_hWnd) {
				DestroyWindow(s_Instance->m_hWnd);
			}
			return 0;
		}
		// Enter inserts a newline instead of executing.
		if (wParam == VK_RETURN && !(GetKeyState(VK_CONTROL) & 0x8000)) {
			return 0;  // Block default Enter behavior.
		}
	}

	// Block WM_CHAR for Enter to prevent a beep.
	if (msg == WM_CHAR && wParam == '\r') {
		return 0;
	}

	return CallWindowProc(s_OriginalRichEditProc, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK ScriptingWindow::WindowProc(HWND hwnd,
                                             UINT msg,
                                             WPARAM wParam,
                                             LPARAM lParam) {
	ScriptingWindow *window =
	    (ScriptingWindow *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

	if (msg == WM_CREATE) {
		CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
		window = (ScriptingWindow *)cs->lpCreateParams;
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)window);
		return 0;
	}

	if (!window) {
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	switch (msg) {
		case WM_SIZE:
			window->OnResize(LOWORD(lParam), HIWORD(lParam));
			return 0;

		case WM_COMMAND:
			if ((HWND)lParam == window->m_hExecute) {
				window->ExecuteScript();
				return 0;
			}

			if ((HWND)lParam == window->m_hClear) {
				window->ClearContent();
				return 0;
			}

			if ((HWND)lParam == window->m_hClose) {
				DestroyWindow(hwnd);
				return 0;
			}
			break;

		case WM_KEYDOWN:
			if (wParam == VK_ESCAPE) {
				DestroyWindow(hwnd);
				return 0;
			}
			break;

		case WM_CLOSE:
			DestroyWindow(hwnd);
			return 0;

		case WM_DESTROY:
			g_ScriptingWindow.store(nullptr);
			return 0;

		default:
			return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

// ============================================================================
// Public control functions.
// ============================================================================

void ScriptingWindow::Show() {
	if (m_hWnd) {
		ShowWindow(m_hWnd, SW_SHOW);
		SetForegroundWindow(m_hWnd);
		SetFocus(m_hRichEdit);
	}
}

void ScriptingWindow::Hide() {
	if (m_hWnd) {
		ShowWindow(m_hWnd, SW_HIDE);
	}
}

void ScriptingWindow::Toggle() {
	if (m_hWnd) {
		if (IsWindowVisible(m_hWnd)) {
			Hide();
		} else {
			Show();
		}
	}
}
