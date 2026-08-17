// ============================================================================
// Class: console for executing TJS commands.
// ============================================================================

#include <richedit.h>
#include <windows.h>
#include <atomic>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// RichEdit is loaded dynamically through LoadLibrary("Msftedit.dll") in
// Create().

// KiriKiri SDK
#pragma warning(push)
#pragma warning(disable : 26495)
#include "../krkr-sdk/tp_stub.h"
#pragma warning(pop)

#include "NeuroConsole.h"
#include "ScriptingWindow.h"

// Flag for enabling or disabling debug output.
static std::atomic<bool> g_DebugOutputEnabled(true);

class NeuroConsole {
 private:
	HWND m_hWnd;
	HWND m_hOutput;       // RichEdit output control.
	HWND m_hInput;        // Command input edit control.
	HWND m_hExecute;      // Execute button.
	HWND m_hClear;        // Clear button.
	HWND m_hScriptMode;   // Script Mode button.
	HWND m_hAlwaysOnTop;  // AlwaysOnTop checkbox.
	HWND m_hDebugOutput;  // Debug Output checkbox.
	HWND m_hNoWrapper;    // No Wrapper checkbox.

	std::vector<std::wstring> m_history;  // Command history.
	int m_historyIndex;

	// Resource management to prevent leaks.
	HMODULE m_hRichEditDll;
	std::vector<HFONT> m_fonts;  // Created font storage.

	static NeuroConsole *s_Instance;
	static WNDPROC s_OriginalEditProc;  // Input Edit control subclass procedure.
	static WNDPROC
	    s_OriginalOutputProc;  // Output RichEdit control subclass procedure.

	// Output field subclass procedure for Esc and Ctrl+A handling.
	static LRESULT CALLBACK OutputEditProc(HWND hwnd,
	                                       UINT msg,
	                                       WPARAM wParam,
	                                       LPARAM lParam) {
		if (msg == WM_KEYDOWN) {
			if (wParam == VK_ESCAPE) {
				if (s_Instance && s_Instance->m_hWnd) {
					ShowWindow(s_Instance->m_hWnd, SW_HIDE);
				}
				return 0;
			} else if (wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
				SendMessage(hwnd, EM_SETSEL, 0, -1);
				return 0;
			}
		}
		// Block WM_CHAR for Esc and Ctrl+A to prevent a beep.
		if (msg == WM_CHAR && (wParam == 27 || wParam == 1)) {
			return 0;
		}
		return CallWindowProc(s_OriginalOutputProc, hwnd, msg, wParam, lParam);
	}

	// Input field subclass procedure for Enter, arrow keys, and Ctrl+A.
	static LRESULT CALLBACK InputEditProc(HWND hwnd,
	                                      UINT msg,
	                                      WPARAM wParam,
	                                      LPARAM lParam) {
		if (msg == WM_KEYDOWN) {
			if (wParam == VK_RETURN) {
				// Enter pressed: execute the command.
				if (s_Instance) {
					s_Instance->ExecuteCommand();
				}
				return 0;  // Do not pass Enter further.
			} else if (wParam == VK_UP) {
				// Up arrow: previous command from history.
				if (s_Instance) {
					s_Instance->HistoryUp();
				}
				return 0;
			} else if (wParam == VK_DOWN) {
				// Down arrow: next command from history.
				if (s_Instance) {
					s_Instance->HistoryDown();
				}
				return 0;
			} else if (wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
				// Ctrl+A: select all.
				SendMessage(hwnd, EM_SETSEL, 0, -1);
				return 0;
			} else if (wParam == VK_ESCAPE) {
				// Esc: hide the console.
				OutputDebugStringW(L"[NeuroConsole] ESC pressed in input\n");
				if (s_Instance && s_Instance->m_hWnd) {
					ShowWindow(s_Instance->m_hWnd, SW_HIDE);
				}
				return 0;
			}
		}
		// Block WM_CHAR for Enter, Esc, and Ctrl+A to prevent a beep.
		if (msg == WM_CHAR && (wParam == '\r' || wParam == 27 || wParam == 1)) {
			return 0;
		}
		// Pass all other messages to the original procedure.
		return CallWindowProc(s_OriginalEditProc, hwnd, msg, wParam, lParam);
	}

	// Window Procedure
	static LRESULT CALLBACK WindowProc(HWND hwnd,
	                                   UINT msg,
	                                   WPARAM wParam,
	                                   LPARAM lParam) {
		NeuroConsole *console =
		    (NeuroConsole *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

		if (msg == WM_CREATE) {
			CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
			console = (NeuroConsole *)cs->lpCreateParams;
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
				// Execute button click.
				if ((HWND)lParam == console->m_hExecute) {
					if (console) {
						console->ExecuteCommand();
					}
					return 0;
				}

				// Script Mode button click.
				if ((HWND)lParam == console->m_hScriptMode) {
					ToggleScriptingWindow();
					return 0;
				}

				// Clear button click.
				if ((HWND)lParam == console->m_hClear) {
					if (console)
						console->ClearOutput();
					return 0;
				}

				// AlwaysOnTop checkbox.
				if ((HWND)lParam == console->m_hAlwaysOnTop) {
					if (console)
						console->OnAlwaysOnTopChanged();
					return 0;
				}

				// Debug Output checkbox.
				if ((HWND)lParam == console->m_hDebugOutput) {
					if (console)
						console->OnDebugOutputChanged();
					return 0;
				}

				// No Wrapper checkbox.
				if ((HWND)lParam == console->m_hNoWrapper) {
					// No special handling; state is checked during execution.
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
		int const buttonHeight = 30;
		int const inputHeight = 25;
		int const checkboxHeight = 20;
		int const padding = 5;
		int const buttonWidth = 80;
		int const scriptModeWidth = 100;
		int const checkboxWidth = 100;

		// Output fills most of the window, leaving space for bottom checkboxes.
		int outputHeight = height - inputHeight - checkboxHeight - padding * 4;
		SetWindowPos(m_hOutput, NULL, padding, padding, width - padding * 2,
		             outputHeight, SWP_NOZORDER);

		// Input field
		int inputY = outputHeight + padding * 2;
		SetWindowPos(m_hInput, NULL, padding, inputY,
		             width - buttonWidth * 2 - scriptModeWidth - padding * 5,
		             inputHeight, SWP_NOZORDER);

		// Script Mode button (opens multi-line editor)
		int scriptModeX = width - buttonWidth * 2 - scriptModeWidth - padding * 3;
		SetWindowPos(m_hScriptMode, NULL, scriptModeX, inputY, scriptModeWidth,
		             inputHeight, SWP_NOZORDER);

		// Execute button
		int executeX = width - buttonWidth * 2 - padding * 2;
		SetWindowPos(m_hExecute, NULL, executeX, inputY, buttonWidth, inputHeight,
		             SWP_NOZORDER);

		// Clear button
		int clearX = width - buttonWidth - padding;
		SetWindowPos(m_hClear, NULL, clearX, inputY, buttonWidth, inputHeight,
		             SWP_NOZORDER);

		// Bottom checkbox row with three checkboxes.
		int checkboxY = inputY + inputHeight + padding;
		SetWindowPos(m_hAlwaysOnTop, NULL, padding, checkboxY, checkboxWidth,
		             checkboxHeight, SWP_NOZORDER);

		SetWindowPos(m_hDebugOutput, NULL, padding + checkboxWidth + padding,
		             checkboxY, checkboxWidth, checkboxHeight, SWP_NOZORDER);

		SetWindowPos(m_hNoWrapper, NULL, padding + checkboxWidth * 2 + padding * 2,
		             checkboxY, checkboxWidth, checkboxHeight, SWP_NOZORDER);
	}

	void OnAlwaysOnTopChanged() {
		BOOL checked =
		    (SendMessage(m_hAlwaysOnTop, BM_GETCHECK, 0, 0) == BST_CHECKED);
		HWND insertAfter = checked ? HWND_TOPMOST : HWND_NOTOPMOST;
		SetWindowPos(m_hWnd, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	}

	void OnDebugOutputChanged() {
		BOOL checked =
		    (SendMessage(m_hDebugOutput, BM_GETCHECK, 0, 0) == BST_CHECKED);
		g_DebugOutputEnabled.store(checked == TRUE);
	}

	void HistoryUp() {
		if (m_history.empty())
			return;

		if (m_historyIndex > 0) {
			m_historyIndex--;
		}

		if (m_historyIndex < (int)m_history.size()) {
			SetWindowTextW(m_hInput, m_history[m_historyIndex].c_str());
			// Move the cursor to the end.
			SendMessage(m_hInput, EM_SETSEL, -1, -1);
		}
	}

	void HistoryDown() {
		if (m_history.empty())
			return;

		if (m_historyIndex < (int)m_history.size()) {
			m_historyIndex++;
		}

		if (m_historyIndex < (int)m_history.size()) {
			SetWindowTextW(m_hInput, m_history[m_historyIndex].c_str());
		} else {
			// After the last command, use an empty line.
			SetWindowTextW(m_hInput, L"");
		}
		// Move the cursor to the end.
		SendMessage(m_hInput, EM_SETSEL, -1, -1);
	}

	void ExecuteCommand() {
		// Safely get the command text.
		int textLength = GetWindowTextLengthW(m_hInput);
		if (textLength == 0)
			return;

		if (textLength > MAX_COMMAND_LENGTH) {
			AppendOutput(L"Error: Command too long\n", RGB(255, 0, 0));
			return;
		}

		std::wstring command;
		command.resize(textLength + 1);
		GetWindowTextW(m_hInput, &command[0], textLength + 1);
		command.resize(wcslen(command.c_str()));

		// Add to history.
		m_history.push_back(command);
		m_historyIndex = (int)m_history.size();

		// Output the command.
		AppendOutput(L">> ", RGB(0, 150, 0));
		AppendOutput(command.c_str(), RGB(0, 0, 0));
		AppendOutput(L"\n", RGB(0, 0, 0));

		// Execute TJS code.
		ExecuteTJSCommand(command.c_str());

		// Clear the input field.
		SetWindowTextW(m_hInput, L"");

		// Return focus to the input field.
		SetFocus(m_hInput);
	}

	void ExecuteTJSCommand(wchar_t const *code) {
		if (!code || wcslen(code) == 0)
			return;
		std::wstring cleanCode = code;

		// Safely trim using iswspace().
		while (!cleanCode.empty() &&
		       (cleanCode.back() == L';' || iswspace(cleanCode.back()))) {
			cleanCode.pop_back();
		}
		if (cleanCode.empty())
			return;

		// Check the No Wrapper checkbox state.
		BOOL noWrapperChecked =
		    (SendMessage(m_hNoWrapper, BM_GETCHECK, 0, 0) == BST_CHECKED);

		std::wstring finalCode;
		if (noWrapperChecked) {
			// Execute code without a wrapper.
			finalCode = cleanCode;
		} else {
			// Wrap in try-catch for safety.
			finalCode = L"(function(){ try { return (" + cleanCode +
			            L"); } catch(e) { return \"!!!TJS_EXCEPTION!!! \" + "
			            L"e.message; } })()";
		}

		tTJSVariant result;
		// Call the synchronous function from dllmain.
		if (ExecuteTJSSync(finalCode.c_str(), &result)) {
			// 1. Check for an exception from our !!!TJS_EXCEPTION!!! wrapper.
			if (result.Type() == tvtString) {
				// Use .GetString() to access the internal string.
				std::wstring strVal = result.GetString();
				if (strVal.find(L"!!!TJS_EXCEPTION!!!") == 0) {
					std::wstring errMsg = strVal.substr(20);
					AppendOutput((L"TJS Error: " + errMsg + L"\n").c_str(),
					             RGB(255, 50, 50));
					return;
				}
			}

			// 2. Determine the type for output.
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

			// 3. Safely convert the result to a string.
			// Explicitly cast to ttstr, then call c_str().
			std::wstring valStr;
			if (result.Type() == tvtVoid) {
				valStr = L"void";
			} else {
				// Most reliable conversion method in the KiriKiri SDK.
				valStr = ttstr(result).c_str();
			}

			std::wstring output = L"Result [" + typeStr + L"]: " + valStr + L"\n";
			AppendOutput(output.c_str(), RGB(0, 255, 0));  // Green for success.

		} else {
			AppendOutput(L"Critical Error: TJS execution failed or timed out.\n",
			             RGB(255, 0, 0));
		}
	}

	void ClearOutput() { SetWindowTextW(m_hOutput, L""); }

 public:
	void AppendOutput(wchar_t const *text, COLORREF color) {
		if (!m_hOutput)
			return;

		// Move to the end of the text.
		CHARRANGE cr;
		cr.cpMin = -1;
		cr.cpMax = -1;
		SendMessageW(m_hOutput, EM_EXSETSEL, 0, (LPARAM)&cr);

		// Set the color.
		CHARFORMAT2W cf;
		ZeroMemory(&cf, sizeof(cf));
		cf.cbSize = sizeof(cf);
		cf.dwMask = CFM_COLOR;
		cf.crTextColor = color;
		SendMessageW(m_hOutput, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

		// Append text.
		SendMessageW(m_hOutput, EM_REPLACESEL, FALSE, (LPARAM)text);

		// Scroll to the bottom.
		SendMessageW(m_hOutput, WM_VSCROLL, SB_BOTTOM, 0);
	}

	NeuroConsole()
	    : m_hWnd(NULL),
	      m_hOutput(NULL),
	      m_hInput(NULL),
	      m_hExecute(NULL),
	      m_hClear(NULL),
	      m_hScriptMode(NULL),
	      m_hAlwaysOnTop(NULL),
	      m_hDebugOutput(NULL),
	      m_hNoWrapper(NULL),
	      m_historyIndex(0),
	      m_hRichEditDll(NULL) {
		s_Instance = this;
	}

	bool Create() {
		// Load the RichEdit DLL.
		m_hRichEditDll = LoadLibraryW(L"Msftedit.dll");
		if (!m_hRichEditDll) {
			return false;
		}

		// Register the window class.
		WNDCLASSW wc = {};
		wc.lpfnWndProc = WindowProc;
		wc.hInstance = GetModuleHandle(NULL);
		wc.lpszClassName = L"NeuroConsoleClass";
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		RegisterClassW(&wc);

		// Create an independent window.
		m_hWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"NeuroConsoleClass",
		                         L"Neuro Console - TJS Command Executor",
		                         WS_OVERLAPPEDWINDOW, 100, 100, 800, 600, NULL,
		                         NULL, GetModuleHandle(NULL), this);

		if (!m_hWnd)
			return false;

		// Create a RichEdit control for output.
		m_hOutput =
		    CreateWindowExW(0, MSFTEDIT_CLASS, L"",
		                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
		                        ES_READONLY | ES_AUTOVSCROLL,
		                    0, 0, 0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);

		// Subclass RichEdit to handle Esc and Ctrl+A.
		s_OriginalOutputProc = (WNDPROC)SetWindowLongPtr(m_hOutput, GWLP_WNDPROC,
		                                                 (LONG_PTR)OutputEditProc);

		// Set the output font.
		CHARFORMAT2W cf;
		ZeroMemory(&cf, sizeof(cf));
		cf.cbSize = sizeof(cf);
		cf.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET;
		wcscpy_s(cf.szFaceName, L"Consolas");
		cf.yHeight = 200;  // 10pt * 20
		cf.bCharSet = DEFAULT_CHARSET;
		SendMessageW(m_hOutput, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);

		// Create an Edit control for input.
		m_hInput = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
		                           WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0,
		                           0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);

		// Subclass Edit to handle Enter.
		s_OriginalEditProc = (WNDPROC)SetWindowLongPtr(m_hInput, GWLP_WNDPROC,
		                                               (LONG_PTR)InputEditProc);

		// Create the buttons.
		m_hExecute =
		    CreateWindowW(L"BUTTON", L"Run", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		                  0, 0, 0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);

		m_hClear = CreateWindowW(L"BUTTON", L"Clear",
		                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
		                         m_hWnd, NULL, GetModuleHandle(NULL), NULL);

		// Create the Script Mode button to open the multi-line editor.
		m_hScriptMode = CreateWindowW(
		    L"BUTTON", L"Script Mode", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0,
		    0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);

		// Create the checkboxes.
		m_hAlwaysOnTop = CreateWindowW(
		    L"BUTTON", L"Always On Top", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0,
		    0, 0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);
		// Disabled by default; the window is created without WS_EX_TOPMOST.
		SendMessage(m_hAlwaysOnTop, BM_SETCHECK, BST_UNCHECKED, 0);

		m_hDebugOutput = CreateWindowW(
		    L"BUTTON", L"Debug Output", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0,
		    0, 0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);
		// Enabled by default.
		SendMessage(m_hDebugOutput, BM_SETCHECK, BST_CHECKED, 0);

		m_hNoWrapper = CreateWindowW(
		    L"BUTTON", L"No Wrapper", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0,
		    0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);
		// Disabled by default.
		SendMessage(m_hNoWrapper, BM_SETCHECK, BST_UNCHECKED, 0);

		// Set the font.
		HFONT hFont =
		    CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

		// Keep the font for later deletion.
		m_fonts.push_back(hFont);

		SendMessageW(m_hInput, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendMessageW(m_hExecute, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendMessageW(m_hClear, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendMessageW(m_hScriptMode, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendMessageW(m_hAlwaysOnTop, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendMessageW(m_hDebugOutput, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendMessageW(m_hNoWrapper, WM_SETFONT, (WPARAM)hFont, TRUE);

		// Welcome message.
		AppendOutput(L"=== Neuro Console Ready ===\n", RGB(0, 100, 200));
		AppendOutput(
		    L"Enter TJS commands below and press Enter or click Execute.\n\n",
		    RGB(100, 100, 100));
		AppendOutput(L"Examples:\n", RGB(0, 0, 0));
		AppendOutput(L"  neuroTest()\n", RGB(0, 150, 0));
		AppendOutput(L"  Debug.message('Hello')\n", RGB(0, 150, 0));
		AppendOutput(L"  2 + 2\n", RGB(0, 150, 0));
		AppendOutput(L"  System.title\n\n", RGB(0, 150, 0));
		AppendOutput(L"Game Exploration:\n", RGB(0, 0, 0));
		AppendOutput(L"  Scripts.getObjectKeys(kag) - List kag properties\n",
		             RGB(0, 150, 0));
		AppendOutput(L"  Scripts.dumpKeys(kag, 'kag_keys.txt') - Dump to file\n",
		             RGB(0, 150, 0));
		AppendOutput(
		    L"  kag.conductor.curStorage + ':' + kag.conductor.curLabel - Current "
		    L"position\n",
		    RGB(0, 150, 0));
		AppendOutput(L"  kag.conductor.getAliasName() - Current character\n",
		             RGB(0, 150, 0));
		AppendOutput(
		    L"  typeof kag.selectLayer != 'undefined' - Check if selecting\n\n",
		    RGB(0, 150, 0));

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
			} else {
				Show();
			}
		}
	}

	bool IsVisible() const { return m_hWnd && IsWindowVisible(m_hWnd); }

	~NeuroConsole() {
		// Restore the original procedures before deletion.
		if (m_hOutput && s_OriginalOutputProc) {
			SetWindowLongPtr(m_hOutput, GWLP_WNDPROC, (LONG_PTR)s_OriginalOutputProc);
		}
		if (m_hInput && s_OriginalEditProc) {
			SetWindowLongPtr(m_hInput, GWLP_WNDPROC, (LONG_PTR)s_OriginalEditProc);
		}

		// Delete all created fonts.
		for (HFONT font : m_fonts) {
			DeleteObject(font);
		}
		m_fonts.clear();

		// Release the RichEdit DLL.
		if (m_hRichEditDll) {
			FreeLibrary(m_hRichEditDll);
			m_hRichEditDll = nullptr;
		}

		if (m_hWnd) {
			DestroyWindow(m_hWnd);
		}
	}
};

NeuroConsole *NeuroConsole::s_Instance = nullptr;
WNDPROC NeuroConsole::s_OriginalEditProc = nullptr;
WNDPROC NeuroConsole::s_OriginalOutputProc = nullptr;

// ============================================================================
// Global Scripting Window function implementations.
// ============================================================================

void InitScriptingWindow() {
	// Create Scripting Window on the main thread.
	ScriptingWindow *window = new ScriptingWindow();
	if (window->Create()) {
		g_ScriptingWindow.store(window);
		window->Show();
	} else {
		delete window;
		g_ScriptingWindow.store(nullptr);
	}
}

void ShutdownScriptingWindow() {
	ScriptingWindow *window = g_ScriptingWindow.load();
	if (window) {
		delete window;
		g_ScriptingWindow.store(nullptr);
	}
}

void ToggleScriptingWindow() {
	if (!g_ScriptingWindow.load()) {
		InitScriptingWindow();
	} else {
		ScriptingWindow *window = g_ScriptingWindow.load();
		if (window) {
			window->Toggle();
		}
	}
}

bool IsScriptingWindowVisible() {
	ScriptingWindow *window = g_ScriptingWindow.load();
	return window && window->IsVisible();
}

// ============================================================================
// Console thread: creates the window and runs the message loop.
// ============================================================================

static std::atomic<NeuroConsole *> g_Console{nullptr};
static std::thread g_ConsoleThread;
static std::atomic<bool> g_ConsoleThreadRunning{false};
static std::atomic<bool> g_ConsoleReady{false};
static std::atomic<HWND> g_ConsoleHWND{
    nullptr};                      // For cross-thread communication.
static std::mutex g_ConsoleMutex;  // For thread-safe operations.

// Custom messages for console control from other threads.
// Defined in NeuroConsole.h.

// Structure for passing data through PostMessage.
struct ConsoleAppendData {
	wchar_t *text;
	COLORREF color;
};

// Console thread with its own message loop.
void ConsoleThreadProc() {
	// Create the console on this thread.
	NeuroConsole *console = new NeuroConsole();
	g_Console.store(console);
	if (!g_Console.load()->Create()) {
		delete g_Console.load();
		g_Console.store(nullptr);
		g_ConsoleReady.store(false);
		return;
	}

	g_ConsoleHWND.store(g_Console.load()->GetHWND());
	g_ConsoleReady.store(true);

	// The console message loop is critical for event processing.
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		// Handle custom messages.
		if (msg.hwnd == g_ConsoleHWND.load() || msg.hwnd == NULL) {
			if (msg.message == WM_CONSOLE_TOGGLE) {
				NeuroConsole *console = g_Console.load();
				if (console)
					console->Toggle();
				continue;
			}
			if (msg.message == WM_CONSOLE_SHUTDOWN) {
				break;  // Exit the message loop.
			}
			if (msg.message == WM_CONSOLE_APPEND) {
				std::lock_guard<std::mutex> lock(g_ConsoleMutex);
				ConsoleAppendData *data = (ConsoleAppendData *)msg.wParam;
				NeuroConsole *console = g_Console.load();
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

	// Cleanup after exiting the loop.
	NeuroConsole *consoleToDelete = g_Console.load();
	if (consoleToDelete) {
		delete consoleToDelete;
		g_Console.store(nullptr);
	}
	g_ConsoleHWND.store(nullptr);
	g_ConsoleReady.store(false);
}

// ============================================================================
// Exported functions for use from dllmain.cpp.
// ============================================================================

void InitNeuroConsole() {
	if (g_ConsoleThreadRunning.load()) {
		return;  // Already running.
	}

	g_ConsoleThreadRunning.store(true);
	g_ConsoleThread = std::thread(ConsoleThreadProc);

	// Wait for the console to become ready, up to two seconds.
	for (int i = 0; i < 20 && !g_ConsoleReady.load(); i++) {
		Sleep(100);
	}
}

void ShutdownNeuroConsole() {
	if (!g_ConsoleThreadRunning.load()) {
		return;
	}

	// Send the shutdown message to the console thread.
	if (g_ConsoleHWND) {
		PostMessage(g_ConsoleHWND, WM_CONSOLE_SHUTDOWN, 0, 0);
	}

	// Wait for the thread to finish.
	if (g_ConsoleThread.joinable()) {
		g_ConsoleThread.join();
	}

	g_ConsoleThreadRunning.store(false);
}

void ToggleNeuroConsole() {
	// Start the thread if it is not running.
	if (!g_ConsoleThreadRunning.load()) {
		InitNeuroConsole();
	}

	// Send the toggle message to the console thread.
	if (g_ConsoleHWND) {
		PostMessage(g_ConsoleHWND, WM_CONSOLE_TOGGLE, 0, 0);
	}
}

bool IsNeuroConsoleVisible() {
	NeuroConsole *console = g_Console.load();
	return console && console->IsVisible();
}

void AppendToNeuroConsole(wchar_t const *text, unsigned int color) {
	if (!g_ConsoleReady.load() || !g_Console.load() || !text)
		return;

	// Check whether debug output is enabled.
	if (!g_DebugOutputEnabled.load())
		return;

	// Safely check the text length.
	size_t len = wcslen(text);
	if (len == 0)
		return;

	if (len > MAX_CONSOLE_TEXT_LENGTH) {
		len = MAX_CONSOLE_TEXT_LENGTH;
	}

	// Copy the text; the handler deletes it.
	wchar_t *textCopy = new wchar_t[len + 1];
	wcscpy_s(textCopy, len + 1, text);
	textCopy[len] = L'\0';

	ConsoleAppendData *data = new ConsoleAppendData();
	data->text = textCopy;
	data->color = (COLORREF)color;

	// Send to the console thread with failure handling.
	HWND consoleHwnd = g_ConsoleHWND.load();
	if (consoleHwnd) {
		if (!PostMessage(consoleHwnd, WM_CONSOLE_APPEND, (WPARAM)data, 0)) {
			// Release memory if PostMessage fails.
			delete[] textCopy;
			delete data;
		}
	} else {
		delete[] textCopy;
		delete data;
	}
}
