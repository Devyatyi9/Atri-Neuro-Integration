#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 26495)
#include "tp_stub.h"
#pragma warning(pop)

#include "ncbind.hpp"

#include "ActionCommon.h"
#include "ActionRouter.h"
#include "AssetMedia.h"
#include "AudioContext.h"
#include "ConnectionIndicator.h"
#include "ContextPayload.h"
#include "D3DWatch.h"
#include "GameDebugLog.h"
#include "Logging.h"
#include "NeuroClient.h"
#include "NeuroConsole.h"
#include "PluginConfig.h"
#include "PluginLifecycle.h"
#include "ScreenNotebook.h"
#include "TJSBindings.h"
#include "TJSExecutor.h"
#include "UtfConversion.h"

namespace {

static tjs_error TJS_INTF_METHOD NeuroTestFunction(tTJSVariant *result,
                                                   tjs_int numparams,
                                                   tTJSVariant **param,
                                                   iTJSDispatch2 *objthis) {
	OutputDebugStringW(L"[NEURO PLUGIN] NeuroTestFunction called from TJS!\n");
	Log(">>> NeuroTestFunction CALLED FROM TJS SCRIPT!\n");

	MessageBoxW(NULL,
	            L"SUCCESS!\n\n"
	            L"NeuroTestFunction was successfully called from TJS!\n\n"
	            L"Your Neuro-integration plugin is fully working!",
	            L"Neuro Plugin - Function Called!", MB_OK | MB_ICONINFORMATION);

	if (result) {
		*result = TJS_W("Hello from Neuro-integration plugin!");
	}

	return TJS_S_OK;
}

// The table is filled once by the init thread and only read afterwards, on the
// game's main thread; the flag is what keeps the two apart without a lock.
static AudioContext g_AudioContext;
static std::atomic<bool> g_AudioTableLoaded(false);

void ReadAudioContextTable() {
	if (!GetPluginConfig().AudioContext) {
		return;
	}
	wchar_t exe[MAX_PATH] = {};
	DWORD const length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring path(exe, length);
	path.resize(path.find_last_of(L'\\') + 1);
	path += L"neuro-assets\\audio_context.tsv";

	std::string text;
	HANDLE const file =
	    CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
	                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file != INVALID_HANDLE_VALUE) {
		LARGE_INTEGER size = {};
		if (GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
		    size.QuadPart < (1 << 20)) {
			text.resize(static_cast<size_t>(size.QuadPart));
			DWORD read = 0;
			if (!ReadFile(file, &text[0], static_cast<DWORD>(text.size()), &read,
			              nullptr)) {
				read = 0;
			}
			text.resize(read);
		}
		CloseHandle(file);
	}
	// A table saved by an editor may open with a byte order mark.
	if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
		text.erase(0, 3);
	}
	g_AudioContext.LoadTable(text);
	g_AudioContext.SetKinds(GetPluginConfig().AudioContextMusic,
	                        GetPluginConfig().AudioContextSounds);
	g_AudioTableLoaded.store(true, std::memory_order_release);
	Log(("[Audio] table: " + std::to_string(g_AudioContext.RowCount()) +
	     " row(s)\n")
	        .c_str());
}

static tjs_error TJS_INTF_METHOD NeuroMountAssets(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *objthis) {
	bool const ok = MountAssetMedia();
	if (result) {
		*result = (tTVInteger)(ok ? 1 : 0);
	}
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroGetVersion(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *objthis) {
	OutputDebugStringW(L"[NEURO PLUGIN] NeuroGetVersion called!\n");
	Log(">>> NeuroGetVersion called\n");

	if (result) {
		*result = (tTVInteger)100;  // Version 1.0.0
	}

	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD
NeuroBeginExitShutdown(tTJSVariant *result,
                       tjs_int numparams,
                       tTJSVariant **param,
                       iTJSDispatch2 *objthis) {
	// System.exit runs on the game main thread, so joining native workers is
	// safe.
	ShutdownPluginWorkers();
	if (result) {
		*result = (tTVInteger)1;
	}
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD
NeuroStressBlockNextSyncCall(tTJSVariant *result,
                             tjs_int numparams,
                             tTJSVariant **param,
                             iTJSDispatch2 *objthis) {
	BlockNextTJSSyncCallForStressTest();
	Log(">>> Next TJS sync call armed for stress blocking\n");
	if (result) {
		*result = (tTVInteger)1;
	}
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD
NeuroStressHoldSyncCalls(tTJSVariant *result,
                         tjs_int numparams,
                         tTJSVariant **param,
                         iTJSDispatch2 *objthis) {
	HoldTJSSyncCallsForStressTest();
	Log(">>> TJS sync calls armed for pending-unload stress\n");
	if (result) {
		*result = (tTVInteger)1;
	}
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroConnectClient(tTJSVariant *result,
                                                    tjs_int numparams,
                                                    tTJSVariant **param,
                                                    iTJSDispatch2 *objthis) {
	std::string url;
	if (numparams > 0 && param && param[0]) {
		if (param[0]->Type() != tvtString) {
			if (result) {
				*result = (tTVInteger)0;
			}
			return TJS_S_OK;
		}
		ttstr value = *param[0];
		if (!Utf16ToUtf8(value.c_str(), value.GetLen(), url) ||
		    url.find('\0') != std::string::npos) {
			if (result) {
				*result = (tTVInteger)0;
			}
			return TJS_S_OK;
		}
	}
	bool const started =
	    NeuroConnect(url.empty() ? nullptr : url.c_str(), nullptr);
	if (result) {
		*result = (tTVInteger)(started ? 1 : 0);
	}
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroDropLinkFromTJS(tTJSVariant *result,
                                                      tjs_int numparams,
                                                      tTJSVariant **param,
                                                      iTJSDispatch2 *objthis) {
	// Neuro.dropLink("result") waits for the next answer; a number waits that
	// many milliseconds; nothing at all drops it now.
	if (numparams > 0 && param && param[0] && param[0]->Type() == tvtString) {
		NeuroDropLinkOnNextResult();
		if (result) {
			*result = (tTVInteger)1;
		}
		return TJS_S_OK;
	}
	unsigned int afterMs = 0;
	if (numparams > 0 && param && param[0] && param[0]->Type() == tvtInteger) {
		tTVInteger const value = param[0]->AsInteger();
		if (value > 0) {
			afterMs = static_cast<unsigned int>(value > 60000 ? 60000 : value);
		}
	}
	NeuroDropLink(afterMs);
	if (result) {
		*result = (tTVInteger)1;
	}
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroDisconnectClient(tTJSVariant *result,
                                                       tjs_int numparams,
                                                       tTJSVariant **param,
                                                       iTJSDispatch2 *objthis) {
	NeuroDisconnect();
	if (result) {
		*result = (tTVInteger)1;
	}
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroClientConnected(tTJSVariant *result,
                                                      tjs_int numparams,
                                                      tTJSVariant **param,
                                                      iTJSDispatch2 *objthis) {
	if (result) {
		*result = (tTVInteger)(NeuroIsConnected() ? 1 : 0);
	}
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroClientReady(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *objthis) {
	if (result) {
		*result = (tTVInteger)(NeuroIsReady() ? 1 : 0);
	}
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroEcho(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
	OutputDebugStringW(L"[NEURO PLUGIN] NeuroEcho called!\n");
	Log(">>> NeuroEcho called\n");

	if (numparams > 0 && param && param[0]) {
		tTJSVariant *firstParam = param[0];

		if (firstParam->Type() == tvtString) {
			ttstr str = *firstParam;
			MessageBoxW(NULL, str.c_str(), L"Neuro Echo", MB_OK | MB_ICONINFORMATION);

			if (result) {
				*result = *firstParam;
			}
		} else if (firstParam->Type() == tvtInteger) {
			wchar_t buffer[128];
			wsprintf(buffer, L"Number received: %I64d", (tTVInteger)*firstParam);
			MessageBoxW(NULL, buffer, L"Neuro Echo", MB_OK | MB_ICONINFORMATION);

			if (result) {
				*result = *firstParam;
			}
		}
	}

	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroShowConsole(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *objthis) {
	Log(">>> NeuroShowConsole called\n");

	tTJSVariant checkResult;
	TVPExecuteExpression(
	    TJS_W(
	        "typeof Debug != 'undefined' && typeof Debug.console != 'undefined'"),
	    &checkResult);

	if ((tTVInteger)checkResult != 0) {
		tTJSVariant dummy;
		TVPExecuteExpression(TJS_W("Debug.console.visible = true"), &dummy);
		Log("  Debug.console.visible = true\n");
		if (result)
			*result = (tTVInteger)1;
	} else {
		Log("  Debug.console not available\n");
		if (result)
			*result = (tTVInteger)0;
	}

	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroIsKAGReady(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *objthis) {
	tTJSVariant checkResult;
	TVPExecuteExpression(TJS_W("typeof global.kag != 'undefined'"), &checkResult);
	bool ready = (tTVInteger)checkResult != 0;

	Log(ready ? ">>> NeuroIsKAGReady: YES\n" : ">>> NeuroIsKAGReady: NO\n");

	if (result)
		*result = (tTVInteger)(ready ? 1 : 0);
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroIsTJSReady(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *objthis) {
	bool ready = IsTJSReady();
	Log(ready ? ">>> NeuroIsTJSReady: YES\n" : ">>> NeuroIsTJSReady: NO\n");

	if (result) {
		*result = (tTVInteger)(ready ? 1 : 0);
	}

	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroConsoleLog(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *objthis) {
	if (numparams < 1 || !param || !param[0]) {
		return TJS_S_OK;
	}

	ttstr line = *param[0];
	if (!line.IsEmpty()) {
		AppendGameDebugLog(line.c_str());
		ttstr lineWithNL = line + TJS_W("\n");
		AppendToNeuroConsole(lineWithNL.c_str(), RGB(100, 100, 100));
	}

	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroGetDebugLog(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *objthis) {
	std::size_t offset = 0;
	std::size_t count = 100;
	bool fromEnd = true;
	if (numparams > 0 && param && param[0]) {
		tTVInteger value = *param[0];
		count = value > 0 ? static_cast<std::size_t>(value) : 0;
	}
	if (numparams > 1 && param && param[1]) {
		fromEnd = static_cast<tTVInteger>(*param[1]) != 0;
	}
	if (numparams > 2 && param && param[2]) {
		tTVInteger value = *param[2];
		offset = value > 0 ? static_cast<std::size_t>(value) : 0;
	}
	if (result) {
		std::wstring const log = GetGameDebugLog(offset, count, fromEnd);
		*result = ttstr(log.c_str());
	}
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroClearDebugLog(tTJSVariant *result,
                                                    tjs_int numparams,
                                                    tTJSVariant **param,
                                                    iTJSDispatch2 *objthis) {
	ClearGameDebugLog();
	if (result) {
		*result = (tTVInteger)1;
	}
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD
NeuroSendContextFromTJS(tTJSVariant *result,
                        tjs_int numparams,
                        tTJSVariant **param,
                        iTJSDispatch2 *objthis) {
	if (numparams < 1 || !param || !param[0]) {
		return TJS_E_BADPARAMCOUNT;
	}

	ttstr monitorOutput = *param[0];
	if (monitorOutput.IsEmpty()) {
		if (result) {
			*result = (tTVInteger)0;
		}
		return TJS_S_OK;
	}

	std::string utf8Output;
	if (!Utf16ToUtf8(monitorOutput.c_str(), monitorOutput.GetLen(), utf8Output) ||
	    utf8Output.empty()) {
		if (result) {
			*result = (tTVInteger)0;
		}
		return TJS_S_OK;
	}

	LogTagFile(utf8Output.c_str());
	ScreenNotebook::NoteMonitorOutput(utf8Output);

	ContextPayload payload = BuildContextPayload(utf8Output);
	// Sound first: the scenario starts it before the line it belongs to.
	if (GetPluginConfig().AudioContext &&
	    g_AudioTableLoaded.load(std::memory_order_acquire)) {
		std::string const heard = g_AudioContext.Describe(utf8Output);
		if (!heard.empty()) {
			payload.message =
			    payload.message.empty() ? heard : heard + "\n" + payload.message;
		}
	}
	if (!payload.Empty()) {
		NeuroSendContext(payload.message.c_str(), payload.silent);
	}

	if (result) {
		*result = (tTVInteger)1;
	}
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroGetPluginLog(tTJSVariant *result,
                                                   tjs_int numparams,
                                                   tTJSVariant **param,
                                                   iTJSDispatch2 *objthis) {
	std::size_t offset = 0;
	std::size_t count = 100;
	bool fromEnd = true;
	if (numparams > 0 && param && param[0]) {
		tTVInteger value = *param[0];
		count = value > 0 ? static_cast<std::size_t>(value) : 0;
	}
	if (numparams > 1 && param && param[1]) {
		fromEnd = static_cast<tTVInteger>(*param[1]) != 0;
	}
	if (numparams > 2 && param && param[2]) {
		tTVInteger value = *param[2];
		offset = value > 0 ? static_cast<std::size_t>(value) : 0;
	}
	if (result) {
		std::wstring const log = GetPluginLogFile(offset, count, fromEnd);
		*result = ttstr(log.c_str());
	}
	return TJS_S_OK;
}

class tEnumMemberCallback : public tTJSDispatch {
 public:
	int m_count;
	int m_maxCount;

	tEnumMemberCallback(int maxCount = 50) : m_count(0), m_maxCount(maxCount) { }

	tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32 flag,
	                                   tjs_char const *membername,
	                                   tjs_uint32 *hint,
	                                   tTJSVariant *result,
	                                   tjs_int numparams,
	                                   tTJSVariant **param,
	                                   iTJSDispatch2 *objthis) override {
		// EnumMembers invokes callback(name, flags, value).
		if (numparams < 3)
			return TJS_S_OK;

		ttstr name = *param[0];
		tTJSVariant *value = param[2];

		if (name.IsEmpty())
			return TJS_S_OK;

		if (m_count >= m_maxCount) {
			if (m_count == m_maxCount) {
				wchar_t buf[64];
				swprintf_s(buf, L"  ... (more than %d members)\n", m_maxCount);
				AppendToNeuroConsole(buf, RGB(128, 128, 128));
			}
			m_count++;
			return TJS_S_OK;
		}
		m_count++;

		AppendToNeuroConsole(L"  ", RGB(0, 0, 0));
		AppendToNeuroConsole(name.c_str(), RGB(0, 128, 128));
		AppendToNeuroConsole(L": ", RGB(0, 0, 0));

		if (!value) {
			AppendToNeuroConsole(L"(no value)\n", RGB(128, 128, 128));
			return TJS_S_OK;
		}

		tTJSVariantType vtype = value->Type();
		wchar_t buf[128];

		switch (vtype) {
			case tvtVoid:
				AppendToNeuroConsole(L"void\n", RGB(128, 128, 128));
				break;
			case tvtObject: {
				iTJSDispatch2 *memberObj = value->AsObjectNoAddRef();
				if (memberObj) {
					tTJSVariant memberClass;
					if (TJS_SUCCEEDED(
					        memberObj->ClassInstanceInfo(TJS_CII_GET, 0, &memberClass))) {
						ttstr mcStr = memberClass;
						AppendToNeuroConsole(L"[", RGB(0, 0, 255));
						AppendToNeuroConsole(mcStr.c_str(), RGB(128, 0, 128));
						AppendToNeuroConsole(L"]\n", RGB(0, 0, 255));
					} else {
						AppendToNeuroConsole(L"[Object]\n", RGB(0, 0, 255));
					}
				} else {
					AppendToNeuroConsole(L"null\n", RGB(128, 128, 128));
				}
				break;
			}
			case tvtString: {
				ttstr str = *value;
				if (str.GetLen() > 50) {
					str = ttstr(str.c_str(), 50) + TJS_W("...");
				}
				AppendToNeuroConsole(L"\"", RGB(0, 128, 0));
				AppendToNeuroConsole(str.c_str(), RGB(0, 128, 0));
				AppendToNeuroConsole(L"\"\n", RGB(0, 128, 0));
				break;
			}
			case tvtInteger:
				swprintf_s(buf, L"%lld\n", value->AsInteger());
				AppendToNeuroConsole(buf, RGB(0, 0, 128));
				break;
			case tvtReal:
				swprintf_s(buf, L"%g\n", value->AsReal());
				AppendToNeuroConsole(buf, RGB(0, 0, 128));
				break;
			default:
				swprintf_s(buf, L"(type %d)\n", (int)vtype);
				AppendToNeuroConsole(buf, RGB(128, 128, 128));
				break;
		}
		return TJS_S_OK;
	}
};

static tjs_error TJS_INTF_METHOD
NeuroGetCurrentContext(tTJSVariant *result,
                       tjs_int numparams,
                       tTJSVariant **param,
                       iTJSDispatch2 *objthis) {
	if (!IsTJSReady()) {
		if (result)
			*result = TJS_W("{\"error\":\"TJS not ready\"}");
		return TJS_S_OK;
	}

	// Collect the current game context as JSON.
	wchar_t const *tjsCode =
	    L"(function(){"
	    L"  try {"
	    L"    var r = {};"
	    L"    r.languageType = typeof kag !== 'undefined' ? kag.languageType : "
	    L"-1;"
	    L"    r.languageTag = typeof CurrentLanguageTag !== 'undefined' ? "
	    L"CurrentLanguageTag : '';"
	    L"    r.hooksActive = !!(global.__neuroHooksInstalled && "
	    L"global.__neuroHooksEnabled);"
	    L"    r.lastTag = global.__neuroLastTag || '';"
	    L"    r.lastScene = global.__neuroLastScene || '';"
	    L"    r.pendingSend = !!(global.__neuroPendingSend);"
	    L"    global.__neuroPendingSend = false;"
	    L"    if (typeof kag !== 'undefined' && kag.backlogWindow && "
	    L"kag.backlogWindow._historyTextStore) {"
	    L"      var store = kag.backlogWindow._historyTextStore;"
	    L"      var data = store.data;"
	    L"      var cnt = store.count;"
	    L"      r.backlogCount = cnt;"
	    L"      var entries = [];"
	    L"      var start = cnt > 5 ? cnt - 5 : 0;"
	    L"      for (var i = start; i < cnt; i++) {"
	    L"        var e = data[i];"
	    L"        "
	    L"entries.push({disp:e.disp,text:e.text,voice:e.voice,name:e.name});"
	    L"      }"
	    L"      r.recentEntries = entries;"
	    L"    }"
	    L"    var ml = (typeof kag !== 'undefined' && kag.messageLayer) ? "
	    L"kag.messageLayer : null;"
	    L"    if (ml && ml.message) {"
	    L"      r.currentText = ml.message;"
	    L"      r.currentSpeaker = ml.name || '';"
	    L"    } else if (typeof kag !== 'undefined' && kag.backlogWindow && "
	    L"kag.backlogWindow._historyTextStore) {"
	    L"      var store = kag.backlogWindow._historyTextStore;"
	    L"      if (store.count > 0) {"
	    L"        var last = store.data[store.count - 1];"
	    L"        r.currentText = last.text;"
	    L"        r.currentSpeaker = last.disp;"
	    L"      }"
	    L"    } else {"
	    L"      r.currentText = '';"
	    L"      r.currentSpeaker = '';"
	    L"    }"
	    L"    if (typeof kag !== 'undefined' && kag.backlogWindow && "
	    L"kag.backlogWindow._historyTextStore) {"
	    L"      var store = kag.backlogWindow._historyTextStore;"
	    L"      if (store.count > 0) {"
	    L"        var last = store.data[store.count - 1];"
	    L"        r.currentVoice = last.voice;"
	    L"        r.currentName = last.name;"
	    L"        if (last.voice && last.name && typeof characterVoiceInfo !== "
	    L"'undefined') {"
	    L"          var cinfo = characterVoiceInfo[last.name];"
	    L"          if (cinfo) r.voicePrefix = cinfo.file;"
	    L"        }"
	    L"      }"
	    L"    }"
	    L"    if (typeof characterVoiceInfo !== 'undefined') {"
	    L"      var vm = {};"
	    L"      var _cviKeys = Scripts.getObjectKeys(characterVoiceInfo);"
	    L"      for (var _cviIdx = 0; _cviIdx < _cviKeys.count; _cviIdx++) {"
	    L"        var n = _cviKeys[_cviIdx];"
	    L"        var f = characterVoiceInfo[n].file;"
	    L"        if (f) vm[f] = n;"
	    L"      }"
	    L"      r.voiceMap = vm;"
	    L"    }"
	    L"    r.currentScene = typeof kag !== 'undefined' ? kag.currentScene : "
	    L"'';"
	    L"    r.currentPage = typeof kag !== 'undefined' ? kag.currentPage : 0;"
	    L"    return System.toJSONString(r);"
	    L"  } catch(e) {"
	    L"    return '{\"error\":\"' + (e && e.message ? e.message : 'unknown') "
	    L"+ '\"}';"
	    L"  }"
	    L"})()";

	tTJSVariant execResult;
	TVPExecuteExpression(ttstr(tjsCode), &execResult);

	if (result) {
		*result = execResult;
	}

	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroExecFile(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *objthis) {
	if (numparams < 1 || param[0]->Type() != tvtString) {
		if (result)
			*result = TJS_W("error: filename required");
		return TJS_S_OK;
	}

	ttstr filename = *param[0];
	FILE *f = _wfopen(filename.c_str(), L"rb");
	if (!f) {
		if (result)
			*result = ttstr(L"error: cannot open '") + filename + L"'";
		return TJS_S_OK;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	std::vector<char> buf(size + 1);
	fread(buf.data(), 1, size, f);
	buf[size] = 0;
	fclose(f);

	std::wstring wideCode;
	if (!Utf8ToUtf16(buf.data(), static_cast<size_t>(size), wideCode) ||
	    wideCode.find(L'\0') != std::wstring::npos) {
		if (result)
			*result = TJS_W("error: file is not valid UTF-8");
		return TJS_S_OK;
	}

	bool silent = (numparams >= 2 && param[1]->Type() == tvtInteger &&
	               (tjs_int)(*param[1]) != 0);

	if (!silent) {
		std::string code(buf.data());
		if (code.size() > 500)
			code.resize(500);
		std::string utf8Filename;
		if (!Utf16ToUtf8(filename.c_str(), filename.GetLen(), utf8Filename))
			utf8Filename = "<invalid UTF-16 filename>";
		std::string entry = "\n=== EXEC FILE: " + utf8Filename + " ===\n" + code +
		                    "\n--- RESULT ---\n";
		Log(entry.c_str());
	}

	tTJSVariant execResult;
	TVPExecuteExpression(ttstr(wideCode.c_str()), &execResult);

	if (!silent) {
		ttstr resultString(execResult);
		std::string utf8Result;
		if (!Utf16ToUtf8(resultString.c_str(), resultString.GetLen(), utf8Result))
			utf8Result = "<invalid UTF-16 result>";
		std::string entry = utf8Result + "\n=== END ===\n";
		Log(entry.c_str());
	}

	if (result)
		*result = execResult;
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroBuildVoiceMap(tTJSVariant *result,
                                                    tjs_int numparams,
                                                    tTJSVariant **param,
                                                    iTJSDispatch2 *objthis) {
	if (!IsTJSReady()) {
		if (result)
			*result = TJS_W("{\"error\":\"TJS not ready\"}");
		return TJS_S_OK;
	}

	wchar_t const *tjsCode =
	    L"(function(){"
	    L"  try {"
	    L"    if (typeof characterVoiceInfo === 'undefined') return '{}';"
	    L"    var vm = {};"
	    L"    var keys = Scripts.getObjectKeys(characterVoiceInfo);"
	    L"    for (var i = 0; i < keys.count; i++) {"
	    L"      var n = keys[i];"
	    L"      var f = characterVoiceInfo[n].file;"
	    L"      if (f) vm[f] = n;"
	    L"    }"
	    L"    return System.toJSONString(vm);"
	    L"  } catch(e) {"
	    L"    return '{}';"
	    L"  }"
	    L"})()";

	tTJSVariant execResult;
	TVPExecuteExpression(ttstr(tjsCode), &execResult);

	if (result) {
		*result = execResult;
	}

	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroDump(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
	if (numparams < 1 || !param || !param[0]) {
		AppendToNeuroConsole(L"Usage: Neuro.dump(object [, maxCount])\n",
		                     RGB(255, 0, 0));
		return TJS_S_OK;
	}

	int maxCount = 50;
	if (numparams >= 2 && param[1] && param[1]->Type() == tvtInteger) {
		maxCount = (int)param[1]->AsInteger();
		if (maxCount < 1)
			maxCount = 1;
		if (maxCount > 1000)
			maxCount = 1000;
	}

	tTJSVariantType type = param[0]->Type();

	if (type != tvtObject) {
		wchar_t buf[256];
		swprintf_s(buf, L"Not an object. Type: %d\n", (int)type);
		AppendToNeuroConsole(buf, RGB(255, 128, 0));

		if (type == tvtString) {
			ttstr str = *param[0];
			AppendToNeuroConsole(L"Value: \"", RGB(0, 0, 255));
			AppendToNeuroConsole(str.c_str(), RGB(0, 128, 0));
			AppendToNeuroConsole(L"\"\n", RGB(0, 0, 255));
		} else if (type == tvtInteger) {
			swprintf_s(buf, L"Value: %lld\n", param[0]->AsInteger());
			AppendToNeuroConsole(buf, RGB(0, 0, 128));
		} else if (type == tvtReal) {
			swprintf_s(buf, L"Value: %g\n", param[0]->AsReal());
			AppendToNeuroConsole(buf, RGB(0, 0, 128));
		}
		return TJS_S_OK;
	}

	iTJSDispatch2 *obj = param[0]->AsObjectNoAddRef();
	if (!obj) {
		AppendToNeuroConsole(L"Object is null\n", RGB(255, 0, 0));
		return TJS_S_OK;
	}

	tTJSVariant className;
	if (TJS_SUCCEEDED(obj->ClassInstanceInfo(TJS_CII_GET, 0, &className))) {
		ttstr classStr = className;
		AppendToNeuroConsole(L"=== Object: ", RGB(0, 100, 200));
		AppendToNeuroConsole(classStr.c_str(), RGB(128, 0, 128));
		AppendToNeuroConsole(L" ===\n", RGB(0, 100, 200));
	} else {
		AppendToNeuroConsole(L"=== Object (unknown class) ===\n", RGB(0, 100, 200));
	}

	tEnumMemberCallback *callback = new tEnumMemberCallback(maxCount);
	tTJSVariantClosure closure(callback, NULL);

	tjs_error err = obj->EnumMembers(0, &closure, obj);

	int memberCount = callback->m_count;
	callback->Release();

	if (TJS_FAILED(err)) {
		AppendToNeuroConsole(L"  (cannot enumerate members)\n", RGB(255, 0, 0));
	} else if (memberCount == 0) {
		AppendToNeuroConsole(L"  (no members)\n", RGB(128, 128, 128));
	}

	wchar_t buf[64];
	swprintf_s(buf, L"Total: %d members\n", memberCount);
	AppendToNeuroConsole(buf, RGB(100, 100, 100));

	return TJS_S_OK;
}

// Read from outside the game, once a turn, the way memory already is: a driver
// that can see the frame path straining can slow down before anything is
// refused, instead of finding out from a log line afterwards.
// Fails one frame on purpose, so the draw device's recovery path can be tried
// on a game that is drawing normally instead of waiting for a real refusal.
static tjs_error TJS_INTF_METHOD NeuroD3DFailLockOnce(tTJSVariant *result,
                                                      tjs_int numparams,
                                                      tTJSVariant **param,
                                                      iTJSDispatch2 *objthis) {
	D3DWatchFailNextLock();
	if (result) {
		*result = TJS_W("armed");
	}
	return TJS_S_OK;
}

// Switches the one thing the D3D watch is allowed to change. Kept out here
// rather than turned on at startup so that one build can be run both ways.
static tjs_error TJS_INTF_METHOD NeuroD3DFixReset(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *objthis) {
	bool const on = numparams >= 1 && param && param[0] &&
	                static_cast<tjs_int>(*param[0]) != 0;
	D3DWatchNudgeResetOnRefusal(on);
	if (result) {
		*result = on ? TJS_W("on") : TJS_W("off");
	}
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroD3DPressure(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *objthis) {
	char line[192];
	D3DWatchPressure(line, sizeof(line));
	if (result) {
		*result = ttstr(line);
	}
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroExec(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
	if (numparams < 1 || !param || !param[0]) {
		return TJS_E_BADPARAMCOUNT;
	}

	if (!IsTJSReady()) {
		Log(">>> NeuroExec: TJS not ready!\n");
		if (result) {
			*result = TJS_W("Error: TJS not initialized yet");
		}
		return TJS_S_OK;
	}

	if (param[0]->Type() == tvtString) {
		ttstr code = *param[0];
		Log(">>> NeuroExec: executing TJS code\n");

		tTJSVariant execResult;
		TVPExecuteExpression(code, &execResult);

		if (result) {
			*result = execResult;
		}
	}

	return TJS_S_OK;
}

}  // namespace

void LoadAudioContextTable() {
	ReadAudioContextTable();
}

// Everything the plugin gives TJS hangs off one global object, Neuro, which
// RegisterTJSBindings creates before these attach to it.
NCB_ATTACH_FUNCTION(test, Neuro, NeuroTestFunction);
NCB_ATTACH_FUNCTION(getVersion, Neuro, NeuroGetVersion);
NCB_ATTACH_FUNCTION(mountAssets, Neuro, NeuroMountAssets);
NCB_ATTACH_FUNCTION(beginExitShutdown, Neuro, NeuroBeginExitShutdown);
NCB_ATTACH_FUNCTION(stressBlockNextSyncCall,
                    Neuro,
                    NeuroStressBlockNextSyncCall);
NCB_ATTACH_FUNCTION(stressHoldSyncCalls, Neuro, NeuroStressHoldSyncCalls);
NCB_ATTACH_FUNCTION(connect, Neuro, NeuroConnectClient);
NCB_ATTACH_FUNCTION(disconnect, Neuro, NeuroDisconnectClient);
NCB_ATTACH_FUNCTION(dropLink, Neuro, NeuroDropLinkFromTJS);
NCB_ATTACH_FUNCTION(isConnected, Neuro, NeuroClientConnected);
NCB_ATTACH_FUNCTION(isReady, Neuro, NeuroClientReady);
NCB_ATTACH_FUNCTION(echo, Neuro, NeuroEcho);
NCB_ATTACH_FUNCTION(isTJSReady, Neuro, NeuroIsTJSReady);
NCB_ATTACH_FUNCTION(isKAGReady, Neuro, NeuroIsKAGReady);
NCB_ATTACH_FUNCTION(showConsole, Neuro, NeuroShowConsole);
NCB_ATTACH_FUNCTION(exec, Neuro, NeuroExec);
NCB_ATTACH_FUNCTION(d3dPressure, Neuro, NeuroD3DPressure);
NCB_ATTACH_FUNCTION(d3dFixReset, Neuro, NeuroD3DFixReset);
NCB_ATTACH_FUNCTION(d3dFailLockOnce, Neuro, NeuroD3DFailLockOnce);
NCB_ATTACH_FUNCTION(consoleLog, Neuro, NeuroConsoleLog);
NCB_ATTACH_FUNCTION(getDebugLog, Neuro, NeuroGetDebugLog);
NCB_ATTACH_FUNCTION(clearDebugLog, Neuro, NeuroClearDebugLog);
NCB_ATTACH_FUNCTION(getPluginLog, Neuro, NeuroGetPluginLog);
// Which of the stub's engine functions this build of the game exports, as
// "name=1" or "name=0" lines. Nothing is called: each signature is only looked
// up, one at a time, since a batched lookup fails as a whole on one miss. The
// strings are the stub's own, and must match it exactly.
static tjs_error TJS_INTF_METHOD NeuroStubExports(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *objthis) {
	static char const *const kSignatures[] = {
	    "void ::TVPRegisterStorageMedia(iTVPStorageMedia *)",
	    "void ::TVPUnregisterStorageMedia(iTVPStorageMedia *)",
	    "iTJSTextReadStream * ::TVPCreateTextStreamForRead(const ttstr &,const "
	    "ttstr &)",
	    "iTJSTextReadStream * ::TVPCreateTextStreamForReadByEncoding(const ttstr "
	    "&,const ttstr &,const ttstr &)",
	    "void ::TVPSetXP3ArchiveExtractionFilter(tTVPXP3ArchiveExtractionFilter)",
	    "bool ::TVPGetBreathing()",
	    "void ::TVPPostEvent(iTJSDispatch2 *,iTJSDispatch2 *,ttstr "
	    "&,tjs_uint32,tjs_uint32,tjs_uint,tTJSVariant *)",
	    "bool ::TVPAreEventsInQueue(iTJSDispatch2 *,iTJSDispatch2 *,const ttstr "
	    "&,tjs_uint32)",
	    "tjs_int ::TVPCountEventsInQueue(iTJSDispatch2 *,iTJSDispatch2 *,const "
	    "ttstr &,tjs_uint32)",
	    "void ::TVPAddCompactEventHook(tTVPCompactEventCallbackIntf *)",
	    "void ::TVPRemoveCompactEventHook(tTVPCompactEventCallbackIntf *)",
	    "bool ::TVPGetAsyncKeyState(tjs_uint,bool)",
	    "void ::TVPExecuteStorage(const ttstr &,tTJSVariant *,bool,const "
	    "tjs_char *)",
	    "void ::TVPDumpScriptEngine()",
	    "void "
	    "::TVPDoTryBlock(tTVPTryBlockFunction,tTVPCatchBlockFunction,"
	    "tTVPFinallyBlockFunction,void *)",
	    "tjs_int ::TVPGetCommandLineArgumentGeneration()",
	    "void ::TVPSetCommandLine(const tjs_char *,const ttstr &)",
	    "tjs_uint64 ::TVPGetTickCount()",
	    "void ::TVPConvertPCMTo16bits(tjs_int16 *,const void *,const "
	    "tTVPWaveFormat &,tjs_int,bool)",
	    "void ::TVPConvertPCMToFloat(float *,const void *,const tTVPWaveFormat "
	    "&,tjs_int)",
	    "IDirectSound * ::TVPGetDirectSound()",
	    "void ::TVPRegisterGraphicLoadingHandler(const ttstr "
	    "&,tTVPGraphicLoadingHandlerForPlugin,"
	    "tTVPGraphicHeaderLoadingHandlerForPlugin,"
	    "tTVPGraphicSaveHandlerForPlugin,tTVPGraphicAcceptSaveHandler,void *)",
	    "iTJSDispatch2 * ::TVPGetObjectFrom_NI_BaseLayer(tTJSNI_BaseLayer *)",
	};
	std::string report;
	iTVPFunctionExporter *const exporter = PluginFunctionExporter();
	for (char const *signature : kSignatures) {
		void *function = nullptr;
		char const *name = signature;
		bool const found =
		    exporter &&
		    exporter->QueryFunctionsByNarrowString(&name, &function, 1) && function;
		report += std::string(signature) + (found ? "=1\n" : "=0\n");
	}
	if (result) {
		std::wstring wide;
		*result = Utf8ToUtf16(report.c_str(), report.size(), wide)
		              ? ttstr(wide.c_str())
		              : ttstr(TJS_W("error: report could not be converted"));
	}
	return TJS_S_OK;
}

NCB_ATTACH_FUNCTION(stubExports, Neuro, NeuroStubExports);

// Counts the engine's compact events by level. The engine calls it on the main
// thread when it asks everything to release caches.
class NeuroCompactHook final : public tTVPCompactEventCallbackIntf {
 public:
	void TJS_INTF_METHOD OnCompact(tjs_int level) override {
		last.store(level, std::memory_order_relaxed);
		switch (level) {
			case TVP_COMPACT_LEVEL_IDLE:
				idle.fetch_add(1, std::memory_order_relaxed);
				break;
			case TVP_COMPACT_LEVEL_DEACTIVATE:
				deactivate.fetch_add(1, std::memory_order_relaxed);
				break;
			case TVP_COMPACT_LEVEL_MINIMIZE:
				minimize.fetch_add(1, std::memory_order_relaxed);
				break;
			case TVP_COMPACT_LEVEL_MAX:
				max.fetch_add(1, std::memory_order_relaxed);
				break;
			default:
				other.fetch_add(1, std::memory_order_relaxed);
				break;
		}
	}

	std::atomic<long long> idle{0};
	std::atomic<long long> deactivate{0};
	std::atomic<long long> minimize{0};
	std::atomic<long long> max{0};
	std::atomic<long long> other{0};
	std::atomic<tjs_int> last{-1};
};

static NeuroCompactHook g_CompactHook;
static bool g_CompactHookAdded = false;

// A one-line reading of the stub functions that are safe to call: whether the
// engine is breathing now, how often the continuous hook ran while it was, the
// compact events seen, and how many queued events have no source and no target.
static tjs_error TJS_INTF_METHOD NeuroStubProbe(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                iTJSDispatch2 *objthis) {
	char line[256];
	sprintf_s(
	    line,
	    "breathing=%d breathingCallbacks=%llu compactHook=%d idle=%lld "
	    "deactivate=%lld minimize=%lld max=%lld other=%lld last=%d "
	    "anonymousEvents=%d",
	    TVPGetBreathing() ? 1 : 0, TJSBreathingCallbackCount(),
	    g_CompactHookAdded ? 1 : 0,
	    g_CompactHook.idle.load(std::memory_order_relaxed),
	    g_CompactHook.deactivate.load(std::memory_order_relaxed),
	    g_CompactHook.minimize.load(std::memory_order_relaxed),
	    g_CompactHook.max.load(std::memory_order_relaxed),
	    g_CompactHook.other.load(std::memory_order_relaxed),
	    static_cast<int>(g_CompactHook.last.load(std::memory_order_relaxed)),
	    static_cast<int>(
	        TVPCountEventsInQueue(nullptr, nullptr, ttstr(TJS_W("")), 0)));
	if (result) {
		*result = ttstr(line);
	}
	return TJS_S_OK;
}

NCB_ATTACH_FUNCTION(stubProbe, Neuro, NeuroStubProbe);
NCB_ATTACH_FUNCTION(sendContextFromTJS, Neuro, NeuroSendContextFromTJS);
NCB_ATTACH_FUNCTION(dump, Neuro, NeuroDump);
NCB_ATTACH_FUNCTION(getCurrentContext, Neuro, NeuroGetCurrentContext);
NCB_ATTACH_FUNCTION(buildVoiceMap, Neuro, NeuroBuildVoiceMap);
NCB_ATTACH_FUNCTION(execFile, Neuro, NeuroExecFile);

static bool g_FirstRunChecked = false;

// Runs the one-time settings script until the game is far enough along to
// answer, then stops asking for the rest of the launch. The script itself keeps
// the mark that stops it applying twice across launches.
static void RetryFirstRunIfReady() {
	if (g_FirstRunChecked)
		return;
	FirstRunMode const mode = GetPluginConfig().FirstRun;
	if (mode == FirstRunMode::Skip) {
		g_FirstRunChecked = true;
		Log("[FirstRun] skipped by the config\n");
		return;
	}
	if (mode == FirstRunMode::Apply) {
		// The script applies its settings unless the profile is marked as
		// having had them, so clearing the mark is what "every start" means.
		std::string cleared;
		ExecuteTJSSync(
		    L"(function(){ if (typeof kag != 'Object' || typeof "
		    L"kag.sflags != 'Object') return 0;"
		    L" kag.sflags.neuroFirstRunApplied = false; return 1; })()",
		    &cleared, 5000, TJSExecMode::Expression);
	}
	std::string answer;
	if (!ExecuteTJSSync(
	        L"Neuro.execFile('neuro-assets/install/first_run.tjs', 1)", &answer,
	        5000, TJSExecMode::Expression) ||
	    answer == "0") {
		return;
	}
	g_FirstRunChecked = true;
	// Only a profile that is genuinely new: with first_run = apply the script
	// answers "applied" on every start.
	if (answer == "2" && mode == FirstRunMode::Auto) {
		ScreenNotebook::SetAsideNotebooks();
	}
	Log(answer == "2" ? "[FirstRun] one-time settings applied\n"
	    : answer == "1"
	        ? "[FirstRun] one-time settings already applied\n"
	        : ("[FirstRun] unexpected answer: " + answer + "\n").c_str());
}

// Keeps the notebooks' unlock mark in the game's system variables, beside the
// first-run mark: read once the game can answer, written when an ending earns
// something new.
static void SyncNotebookUnlock() {
	static bool read = false;
	if (!read) {
		std::string answer;
		if (!ExecuteTJSSync(
		        L"(function(){ if (typeof kag != 'Object' || typeof kag.sflags != "
		        L"'Object') return -1; var v = kag.sflags.neuroNotebookUnlocked;"
		        L" return v === void ? 0 : +v; })()",
		        &answer, 5000, TJSExecMode::Expression) ||
		    answer.empty() || answer == "-1") {
			return;
		}
		read = true;
		ScreenNotebook::SetUnlocked(atoi(answer.c_str()));
	}
	int const books = ScreenNotebook::UnlockToSave();
	if (books == 0) {
		return;
	}
	wchar_t code[256] = {};
	swprintf(code, 256,
	         L"(function(){ if (typeof kag.saveSystemVariables != 'Object') "
	         L"return 0; kag.sflags.neuroNotebookUnlocked = %d; "
	         L"kag.saveSystemVariables(); return 1; })()",
	         books);
	std::string saved;
	if (ExecuteTJSSync(code, &saved, 5000, TJSExecMode::Expression) &&
	    saved == "1") {
		ScreenNotebook::SetUnlocked(books);
		Log("[Notebook] kept for the next time through\n");
	}
}

static tjs_error TJS_INTF_METHOD NeuroPrototypeUpkeep(tTJSVariant *result,
                                                      tjs_int numparams,
                                                      tTJSVariant **param,
                                                      iTJSDispatch2 *objthis) {
	// Periodic upkeep: refresh action registration and retry exit hook.
	// Called from continuous hook when safe (outside tag processing).
	RefreshPrototypeActionState();
	RetryInstallExitHookIfReady();
	RetryFirstRunIfReady();
	SyncNotebookUnlock();
	ConnectionIndicator::Update(NeuroIsConnected());
	if (result) {
		*result = (tTVInteger)1;
	}
	return TJS_S_OK;
}

NCB_ATTACH_FUNCTION(prototypeUpkeep, Neuro, NeuroPrototypeUpkeep);

static bool g_ExitHookInstalled = false;

void RetryInstallExitHookIfReady() {
	if (g_ExitHookInstalled)
		return;
	std::string ready;
	bool const executed = ExecuteTJSSync(
	    L"(function(){ return typeof MyYesNoDialogLayer == 'Object' && "
	    L"typeof MyYesNoDialog != 'undefined'; })()",
	    &ready, 5000, TJSExecMode::Expression);
	{
		char msg[128];
		_snprintf_s(msg, sizeof(msg), "[ExitHook] retry executed=%d ready=%s\n",
		            executed ? 1 : 0, ready.c_str());
		Log(msg);
	}
	// The probe answers with the engine's own rendering of true, which is "1".
	if (!executed || ready != "1")
		return;
	Log("[ExitHook] installing hook\n");
	// Use the synchronous form and confirm the assignment actually landed
	// before flipping g_ExitHookInstalled. Sync execution waits for the game
	// thread to complete the call and returns the result, ensuring the hook
	// is live before the flag is set.
	std::string installed;
	bool const installExecuted = ExecuteTJSSync(
	    L"(function(){ "
	    L"MyYesNoDialogLayer.onItemClick = function(itemName) { "
	    L"var result = itemName == 'yes' || itemName.indexOf('はい') >= 0; "
	    L"if (result && MyYesNoDialog.currentTextPart == 'AskExit') { "
	    L"global.Neuro.beginExitShutdown(); "
	    L"} "
	    L"return result; "
	    L"}; "
	    L"return typeof MyYesNoDialogLayer.onItemClick == 'Object'; "
	    L"})()",
	    &installed, 5000, TJSExecMode::Expression);
	if (!installExecuted || installed != "1") {
		Log("[ExitHook] installation failed or unconfirmed, will retry\n");
		return;
	}
	Log("[ExitHook] hook installed and confirmed\n");
	g_ExitHookInstalled = true;
}

void RegisterTJSBindings() {
	// The object has to exist first: ncbind finds it by name when attaching,
	// and attaches nothing if it is not there.
	iTJSDispatch2 *const neuro = TJSCreateDictionaryObject();
	if (neuro) {
		if (!TVPRegisterGlobalObject(TJS_W("Neuro"), neuro)) {
			Log("[Bindings] could not register the Neuro object\n");
		}
		neuro->Release();
	}
	ncbAutoRegister::AllRegist();
	if (!g_CompactHookAdded) {
		TVPAddCompactEventHook(&g_CompactHook);
		g_CompactHookAdded = true;
	}
	RetryInstallExitHookIfReady();
}

void UnregisterTJSBindings() {
	if (g_CompactHookAdded) {
		TVPRemoveCompactEventHook(&g_CompactHook);
		g_CompactHookAdded = false;
	}
	ncbAutoRegister::AllUnregist();
	TVPRemoveGlobalObject(TJS_W("Neuro"));
	g_ExitHookInstalled = false;
}
