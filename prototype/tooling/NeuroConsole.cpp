// ============================================================================
// Class: console for executing TJS commands.
// ============================================================================

#include <windows.h>
// After windows.h: both of these depend on its types.
#include <commdlg.h>
#include <richedit.h>
#include <atomic>
#include <cstddef>
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
#include "tp_stub.h"
#pragma warning(pop)

#include "NeuroConsole.h"
#include "ThreadGuard.h"
#include "UtfConversion.h"

// Flag for enabling or disabling debug output.
static std::atomic<bool> g_DebugOutputEnabled(true);

class NeuroConsole {
 private:
	HWND m_hWnd;
	HWND m_hOutput;       // RichEdit output control.
	HWND m_hInput;        // Command input edit control.
	HWND m_hExecute;      // Execute button.
	HWND m_hClear;        // Clear button.
	HWND m_hScriptMode;   // Expand button: shows and hides the script area.
	HWND m_hScript;       // Multi-line script area, hidden until expanded.
	HWND m_hScriptRun;    // Run for the script area, so Run is never ambiguous.
	HWND m_hScriptClear;  // Clear for the script area, likewise.
	HWND m_hScriptLoad;   // Loads a .tjs file into the script area.
	HWND m_hScriptSave;   // Writes the script area back out to a .tjs file.
	bool m_ScriptExpanded;
	HWND m_hAlwaysOnTop;  // AlwaysOnTop checkbox.
	HWND m_hDebugOutput;  // Debug Output checkbox.
	HWND m_hFlatScript;   // Flat Script checkbox: picks the entry point.
	HWND m_hRaw;          // Raw checkbox: send the code without the wrapper.

	std::vector<std::wstring> m_history;  // Command history.
	int m_historyIndex;

	// Resource management to prevent leaks.
	HMODULE m_hRichEditDll;
	std::vector<HFONT> m_fonts;  // Created font storage.

	static NeuroConsole *s_Instance;
	static WNDPROC s_OriginalEditProc;  // Input Edit control subclass procedure.
	static WNDPROC
	    s_OriginalOutputProc;  // Output RichEdit control subclass procedure.
	static WNDPROC s_OriginalScriptProc;  // Script area subclass procedure.

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

	// Script area subclass procedure. A plain EDIT has no select-all of its
	// own, so Ctrl+A has to be answered here the way the other two fields do.
	// Enter is deliberately left alone: ES_WANTRETURN makes it a newline, which
	// is the point of this field.
	static LRESULT CALLBACK ScriptEditProc(HWND hwnd,
	                                       UINT msg,
	                                       WPARAM wParam,
	                                       LPARAM lParam) {
		if (msg == WM_KEYDOWN) {
			if (wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
				SendMessage(hwnd, EM_SETSEL, 0, -1);
				return 0;
			}
			if (wParam == VK_ESCAPE) {
				if (s_Instance && s_Instance->m_hWnd) {
					ShowWindow(s_Instance->m_hWnd, SW_HIDE);
				}
				return 0;
			}
		}
		// Block WM_CHAR for Esc and Ctrl+A to prevent a beep.
		if (msg == WM_CHAR && (wParam == 27 || wParam == 1)) {
			return 0;
		}
		return CallWindowProc(s_OriginalScriptProc, hwnd, msg, wParam, lParam);
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

				// Expand button: the script area lives in this window rather
				// than in a second one, so output, history and scrollback stay
				// in one place.
				if ((HWND)lParam == console->m_hScriptMode) {
					console->ToggleScriptArea();
					return 0;
				}

				// Clear button click.
				if ((HWND)lParam == console->m_hClear) {
					if (console)
						console->ClearOutput();
					return 0;
				}

				// The script area's own Run and Clear. Two buttons named Run in
				// one window would be a guess; these say which area they mean.
				if ((HWND)lParam == console->m_hScriptRun) {
					console->RunScript();
					return 0;
				}

				if ((HWND)lParam == console->m_hScriptClear) {
					SetWindowTextW(console->m_hScript, L"");
					SetFocus(console->m_hScript);
					return 0;
				}

				if ((HWND)lParam == console->m_hScriptLoad) {
					console->LoadScriptFile();
					return 0;
				}

				if ((HWND)lParam == console->m_hScriptSave) {
					console->SaveScriptFile();
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

				// Flat Script checkbox.
				if ((HWND)lParam == console->m_hFlatScript) {
					// No special handling; state is checked during execution.
					return 0;
				}

				// Raw checkbox.
				if ((HWND)lParam == console->m_hRaw) {
					// Same: read at execution time.
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

			default:
				break;
		}

		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	void OnResize(int width, int height) {
		int const buttonHeight = 30;
		int const inputHeight = 25;
		int const checkboxHeight = 20;
		int const padding = 5;
		int const buttonWidth = 80;
		int const scriptModeWidth = 130;
		int const checkboxWidth = 100;

		// The script area takes its height from the output rather than from the
		// window, so expanding it never pushes the input row off the bottom.
		int available = height - inputHeight - checkboxHeight - padding * 4;
		int scriptHeight = m_ScriptExpanded ? (available * 2) / 5 : 0;
		int outputHeight = available - scriptHeight - (scriptHeight ? padding : 0);

		// Script area on top, output under it: what is being written stays put
		// while the output below grows with each answer. Its own buttons sit
		// between them, next to the area they act on.
		int scriptRow = m_ScriptExpanded ? inputHeight + padding : 0;
		scriptHeight -= scriptRow;
		if (scriptHeight > 0) {
			SetWindowPos(m_hScript, NULL, padding, padding, width - padding * 2,
			             scriptHeight, SWP_NOZORDER);
			int const rowY = padding + scriptHeight + padding;
			SetWindowPos(m_hScriptRun, NULL, padding, rowY, buttonWidth, inputHeight,
			             SWP_NOZORDER);
			SetWindowPos(m_hScriptClear, NULL, padding * 2 + buttonWidth, rowY,
			             buttonWidth + 20, inputHeight, SWP_NOZORDER);
			SetWindowPos(m_hScriptLoad, NULL, padding * 3 + buttonWidth * 2 + 20,
			             rowY, buttonWidth, inputHeight, SWP_NOZORDER);
			SetWindowPos(m_hScriptSave, NULL, padding * 4 + buttonWidth * 3 + 20,
			             rowY, buttonWidth, inputHeight, SWP_NOZORDER);
		}

		int outputY =
		    padding + (scriptHeight > 0 ? scriptHeight + padding + scriptRow : 0);
		SetWindowPos(m_hOutput, NULL, padding, outputY, width - padding * 2,
		             outputHeight, SWP_NOZORDER);

		// Input field
		int inputY = available + padding * 2;
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

		SetWindowPos(m_hFlatScript, NULL, padding + checkboxWidth * 2 + padding * 2,
		             checkboxY, checkboxWidth, checkboxHeight, SWP_NOZORDER);

		SetWindowPos(m_hRaw, NULL, padding + checkboxWidth * 3 + padding * 3,
		             checkboxY, 60, checkboxHeight, SWP_NOZORDER);
	}

	// Reads a .tjs into the script area. UTF-8 with or without a BOM, and
	// UTF-16 LE with one -- the same three shapes tjscheck and tjsbox accept, so
	// a file that passes those loads here unchanged.
	void LoadScriptFile() {
		wchar_t path[MAX_PATH] = {};
		OPENFILENAMEW ofn = {};
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = m_hWnd;
		ofn.lpstrFilter = L"TJS scripts\0*.tjs\0All files\0*.*\0";
		ofn.lpstrFile = path;
		ofn.nMaxFile = MAX_PATH;
		ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
		if (!GetOpenFileNameW(&ofn)) {
			return;
		}

		HANDLE const file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE) {
			AppendOutput(L"Error: cannot open the file\n", RGB(255, 0, 0));
			return;
		}

		std::string bytes;
		char buffer[4096];
		DWORD read = 0;
		while (ReadFile(file, buffer, sizeof(buffer), &read, NULL) && read > 0) {
			bytes.append(buffer, read);
		}
		CloseHandle(file);

		std::wstring text;
		if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0xFF &&
		    (unsigned char)bytes[1] == 0xFE) {
			text.assign(reinterpret_cast<wchar_t const *>(bytes.data() + 2),
			            (bytes.size() - 2) / sizeof(wchar_t));
		} else {
			size_t offset = 0;
			if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF &&
			    (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF) {
				offset = 3;
			}
			if (!Utf8ToUtf16(bytes.data() + offset, bytes.size() - offset, text)) {
				AppendOutput(L"Error: the file is not valid UTF-8 or UTF-16 LE\n",
				             RGB(255, 0, 0));
				return;
			}
		}

		// An EDIT control breaks lines on CRLF and on nothing else, so a file
		// saved with bare LF -- which is most of ours -- arrives as one long
		// line. Normalising here rather than asking the files to change.
		std::wstring normalised;
		normalised.reserve(text.size() + text.size() / 16);
		for (size_t i = 0; i < text.size(); ++i) {
			wchar_t const c = text[i];
			if (c == L'\r') {
				normalised += L"\r\n";
				if (i + 1 < text.size() && text[i + 1] == L'\n') {
					++i;
				}
			} else if (c == L'\n') {
				normalised += L"\r\n";
			} else {
				normalised += c;
			}
		}

		SetWindowTextW(m_hScript, normalised.c_str());
		SetFocus(m_hScript);
		AppendOutput((std::wstring(L"Loaded ") + path + L"\n").c_str(),
		             RGB(0, 120, 0));
	}

	// Writes the script area out as UTF-8 with LF endings and no BOM -- the
	// shape the scripts in the repository already have, so a file saved here
	// does not stand out from the ones beside it. The control holds CRLF
	// because an EDIT needs it; that is a display detail and does not belong in
	// the file.
	void SaveScriptFile() {
		int const textLength = GetWindowTextLengthW(m_hScript);
		if (textLength == 0) {
			AppendOutput(L"Nothing to save\n", RGB(150, 150, 0));
			return;
		}

		wchar_t path[MAX_PATH] = {};
		OPENFILENAMEW ofn = {};
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = m_hWnd;
		ofn.lpstrFilter = L"TJS scripts\0*.tjs\0All files\0*.*\0";
		ofn.lpstrFile = path;
		ofn.nMaxFile = MAX_PATH;
		ofn.lpstrDefExt = L"tjs";
		ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
		if (!GetSaveFileNameW(&ofn)) {
			return;
		}

		std::wstring text;
		text.resize(textLength + 1);
		GetWindowTextW(m_hScript, &text[0], textLength + 1);
		text.resize(wcslen(text.c_str()));

		std::wstring unixEndings;
		unixEndings.reserve(text.size());
		for (size_t i = 0; i < text.size(); ++i) {
			if (text[i] == L'\r') {
				continue;
			}
			unixEndings += text[i];
		}

		std::string utf8;
		if (!Utf16ToUtf8(unixEndings.c_str(), unixEndings.size(), utf8)) {
			AppendOutput(L"Error: the script could not be encoded\n", RGB(255, 0, 0));
			return;
		}

		HANDLE const file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		                                FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE) {
			AppendOutput(L"Error: cannot write the file\n", RGB(255, 0, 0));
			return;
		}
		DWORD written = 0;
		BOOL const ok =
		    WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, NULL) &&
		    written == utf8.size();
		CloseHandle(file);

		if (!ok) {
			AppendOutput(L"Error: the file was not written in full\n",
			             RGB(255, 0, 0));
			return;
		}
		AppendOutput((std::wstring(L"Saved ") + path + L"\n").c_str(),
		             RGB(0, 120, 0));
	}

	// The script area's Run. Its text is not added to the command history: a
	// whole script there would bury the one-line commands the history exists to
	// recall.
	void RunScript() {
		int const textLength = GetWindowTextLengthW(m_hScript);
		if (textLength == 0)
			return;
		if (textLength > MAX_COMMAND_LENGTH) {
			AppendOutput(L"Error: Script too long\n", RGB(255, 0, 0));
			return;
		}

		std::wstring script;
		script.resize(textLength + 1);
		GetWindowTextW(m_hScript, &script[0], textLength + 1);
		script.resize(wcslen(script.c_str()));

		AppendOutput(L">> [script]\n", RGB(0, 150, 0));
		ExecuteTJSCommand(script.c_str(), true);
	}

	void ToggleScriptArea() {
		m_ScriptExpanded = !m_ScriptExpanded;
		int const show = m_ScriptExpanded ? SW_SHOW : SW_HIDE;
		ShowWindow(m_hScript, show);
		ShowWindow(m_hScriptRun, show);
		ShowWindow(m_hScriptClear, show);
		ShowWindow(m_hScriptLoad, show);
		ShowWindow(m_hScriptSave, show);
		SetWindowTextW(m_hScriptMode, m_ScriptExpanded ? L"Collapse Multiline"
		                                               : L"Expand Multiline");

		RECT rc = {};
		GetClientRect(m_hWnd, &rc);
		OnResize(rc.right - rc.left, rc.bottom - rc.top);

		if (m_ScriptExpanded) {
			SetFocus(m_hScript);
		} else {
			SetFocus(m_hInput);
		}
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
		HWND const source = m_hInput;

		int textLength = GetWindowTextLengthW(source);
		if (textLength == 0)
			return;

		if (textLength > MAX_COMMAND_LENGTH) {
			AppendOutput(L"Error: Command too long\n", RGB(255, 0, 0));
			return;
		}

		std::wstring command;
		command.resize(textLength + 1);
		GetWindowTextW(source, &command[0], textLength + 1);
		command.resize(wcslen(command.c_str()));

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

	void ExecuteTJSCommand(wchar_t const *code, bool forceScript = false) {
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

		// No local wrapper: TJSExecutor owns the try/catch and emits the same
		// !!!TJS_EXCEPTION!!! marker read below. Wrapping here too meant the
		// snippet was wrapped twice, and this copy used `e.message` directly,
		// which yields void when a primitive is thrown rather than an object.
		//
		// The checkbox selects the entry point: unchecked evaluates a single
		// expression and returns its value, checked runs the input as a script so
		// that pasted statements work.
		BOOL const scriptModeChecked =
		    (SendMessage(m_hFlatScript, BM_GETCHECK, 0, 0) == BST_CHECKED);
		// The script area is many statements by definition, so it always takes
		// the script entry point; the checkbox governs the one-line input, where
		// evaluating a single expression is the useful default.
		TJSExecMode const mode = (scriptModeChecked || forceScript)
		                             ? TJSExecMode::Script
		                             : TJSExecMode::Expression;

		// Text and type, both read on the game's main thread inside the call:
		// this runs on the console's own thread, and an engine value must not
		// come back here.
		BOOL const rawChecked =
		    (SendMessage(m_hRaw, BM_GETCHECK, 0, 0) == BST_CHECKED);

		std::string result;
		std::string typeName;
		if (ExecuteTJSSync(cleanCode.c_str(), &result, 5000, mode, &typeName,
		                   rawChecked == FALSE)) {
			std::wstring valStr;
			if (!Utf8ToUtf16(result.c_str(), result.size(), valStr)) {
				valStr = L"<result could not be converted>";
			}
			// The wrapper marks a thrown exception in the text itself.
			if (valStr.find(L"!!!TJS_EXCEPTION!!!") == 0) {
				AppendOutput((L"TJS Error: " + valStr.substr(20) + L"\n").c_str(),
				             RGB(255, 50, 50));
				return;
			}
			std::wstring typeStr;
			if (!Utf8ToUtf16(typeName.c_str(), typeName.size(), typeStr)) {
				typeStr = L"Unknown";
			}
			if (typeStr == L"Void") {
				valStr = L"void";
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
	      m_hScript(NULL),
	      m_hScriptRun(NULL),
	      m_hScriptClear(NULL),
	      m_hScriptLoad(NULL),
	      m_hScriptSave(NULL),
	      m_ScriptExpanded(false),
	      m_hAlwaysOnTop(NULL),
	      m_hDebugOutput(NULL),
	      m_hFlatScript(NULL),
	      m_hRaw(NULL),
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
		    L"BUTTON", L"Expand Multiline", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		    0, 0, 0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);

		// Created hidden and given no height until expanded. ES_WANTRETURN is
		// what makes Enter insert a newline here instead of running the command,
		// which is the difference between a script area and a long input.
		m_hScriptRun =
		    CreateWindowW(L"BUTTON", L"Run Script", WS_CHILD | BS_PUSHBUTTON, 0, 0,
		                  0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);
		m_hScriptClear =
		    CreateWindowW(L"BUTTON", L"Clear Script", WS_CHILD | BS_PUSHBUTTON, 0,
		                  0, 0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);
		m_hScriptLoad =
		    CreateWindowW(L"BUTTON", L"Load .tjs", WS_CHILD | BS_PUSHBUTTON, 0, 0,
		                  0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);
		m_hScriptSave =
		    CreateWindowW(L"BUTTON", L"Save .tjs", WS_CHILD | BS_PUSHBUTTON, 0, 0,
		                  0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);

		// Seeded the way the separate window used to be: the hint says what the
		// buttons here do, and the example is something safe to press Run on.
		m_hScript = CreateWindowExW(
		    WS_EX_CLIENTEDGE, L"EDIT", L"",
		    WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL,
		    0, 0, 0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);
		SetWindowTextW(
		    m_hScript,
		    L"// Multi-line TJS. Enter inserts a newline; Run Script executes.\r\n"
		    L"// Load .tjs reads a file in here. Example, safe to run:\r\n"
		    L"(function() {\r\n"
		    L"    var keys = Scripts.getObjectKeys(kag);\r\n"
		    L"    return \"kag has \" + keys.count + \" properties\";\r\n"
		    L"})()\r\n");

		// After the control exists: subclassing a NULL handle silently does
		// nothing, which is how this field ended up without its Ctrl+A.
		s_OriginalScriptProc = (WNDPROC)SetWindowLongPtr(m_hScript, GWLP_WNDPROC,
		                                                 (LONG_PTR)ScriptEditProc);

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

		// "Flat Script", not "No Wrapper": the wrapper is there either way. What
		// this picks is the entry point -- TVPExecuteExpression wraps the input
		// in an IIFE, TVPExecuteScript runs it flat, statements and all.
		m_hFlatScript = CreateWindowW(
		    L"BUTTON", L"Flat Script", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0,
		    0, 0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);
		// Disabled by default.
		SendMessage(m_hFlatScript, BM_SETCHECK, BST_UNCHECKED, 0);

		// Off by default, and meant to stay off: without the wrapper there is no
		// readable error text, only a failed call. What it buys is that the
		// engine gets to report the error its own way first.
		m_hRaw = CreateWindowW(L"BUTTON", L"Raw",
		                       WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0,
		                       m_hWnd, NULL, GetModuleHandle(NULL), NULL);
		SendMessage(m_hRaw, BM_SETCHECK, BST_UNCHECKED, 0);

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
		SendMessageW(m_hScript, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendMessageW(m_hScriptRun, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendMessageW(m_hScriptClear, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendMessageW(m_hScriptLoad, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendMessageW(m_hScriptSave, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendMessageW(m_hAlwaysOnTop, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendMessageW(m_hDebugOutput, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendMessageW(m_hFlatScript, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendMessageW(m_hRaw, WM_SETFONT, (WPARAM)hFont, TRUE);

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
		if (m_hScript && s_OriginalScriptProc) {
			SetWindowLongPtr(m_hScript, GWLP_WNDPROC, (LONG_PTR)s_OriginalScriptProc);
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
WNDPROC NeuroConsole::s_OriginalScriptProc = nullptr;
WNDPROC NeuroConsole::s_OriginalEditProc = nullptr;
WNDPROC NeuroConsole::s_OriginalOutputProc = nullptr;

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
				// AppendToNeuroConsole is this message's only sender: wParam is
				// always a freshly allocated ConsoleAppendData, and g_Console is
				// only ever cleared before this loop starts or after it exits, so
				// neither can be null while a WM_CONSOLE_APPEND is being handled.
				ConsoleAppendData *data = (ConsoleAppendData *)msg.wParam;
				g_Console.load()->AppendOutput(data->text, data->color);
				delete[] data->text;
				delete data;
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
	g_ConsoleThread =
	    std::thread([] { RunThreadGuarded("console", ConsoleThreadProc); });

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
