#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 26495)
#include "../krkr-sdk/tp_stub.h"
#pragma warning(pop)

#include "ncbind.hpp"

#include "Logging.h"
#include "NeuroConsole.h"
#include "TJSBindings.h"
#include "TJSExecutor.h"
#include "UtfConversion.h"

namespace {

static tjs_error TJS_INTF_METHOD NeuroTestFunction(tTJSVariant *result,
                                                   tjs_int numparams,
                                                   tTJSVariant **param,
                                                   iTJSDispatch2 *objthis) {
	OutputDebugStringW(L"[NEURO PLUGIN] NeuroTestFunction called from TJS!\n");
	LogToFile(">>> NeuroTestFunction CALLED FROM TJS SCRIPT!\n");

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

static tjs_error TJS_INTF_METHOD NeuroGetVersion(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *objthis) {
	OutputDebugStringW(L"[NEURO PLUGIN] NeuroGetVersion called!\n");
	LogToFile(">>> NeuroGetVersion called\n");

	if (result) {
		*result = (tTVInteger)100;  // Version 1.0.0
	}

	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroEcho(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
	OutputDebugStringW(L"[NEURO PLUGIN] NeuroEcho called!\n");
	LogToFile(">>> NeuroEcho called\n");

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

static tjs_error TJS_INTF_METHOD NeuroSendData(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *objthis) {
	LogToFile(">>> NeuroSendData called with ");

	char logMsg[128];
	sprintf_s(logMsg, sizeof(logMsg), "%d parameters\n", numparams);
	LogToFile(logMsg);

	// TODO: Integrate with libneurosdk.
	if (result) {
		*result = (tTVInteger)1;
	}

	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroShowConsole(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *objthis) {
	LogToFile(">>> NeuroShowConsole called\n");

	tTJSVariant checkResult;
	TVPExecuteExpression(
	    TJS_W(
	        "typeof Debug != 'undefined' && typeof Debug.console != 'undefined'"),
	    &checkResult);

	if ((tTVInteger)checkResult != 0) {
		tTJSVariant dummy;
		TVPExecuteExpression(TJS_W("Debug.console.visible = true"), &dummy);
		LogToFile("  Debug.console.visible = true\n");
		if (result)
			*result = (tTVInteger)1;
	} else {
		LogToFile("  Debug.console not available\n");
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

	LogToFile(ready ? ">>> NeuroIsKAGReady: YES\n" : ">>> NeuroIsKAGReady: NO\n");

	if (result)
		*result = (tTVInteger)(ready ? 1 : 0);
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroIsTJSReady(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *objthis) {
	bool ready = IsTJSReady();
	LogToFile(ready ? ">>> NeuroIsTJSReady: YES\n" : ">>> NeuroIsTJSReady: NO\n");

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
		ttstr lineWithNL = line + TJS_W("\n");
		AppendToNeuroConsole(lineWithNL.c_str(), RGB(100, 100, 100));
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
		LogToFile(entry.c_str());
	}

	tTJSVariant execResult;
	TVPExecuteExpression(ttstr(wideCode.c_str()), &execResult);

	if (!silent) {
		ttstr resultString(execResult);
		std::string utf8Result;
		if (!Utf16ToUtf8(resultString.c_str(), resultString.GetLen(), utf8Result))
			utf8Result = "<invalid UTF-16 result>";
		std::string entry = utf8Result + "\n=== END ===\n";
		LogToFile(entry.c_str());
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
		AppendToNeuroConsole(L"Usage: neuroDump(object [, maxCount])\n",
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

static tjs_error TJS_INTF_METHOD NeuroExec(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
	if (numparams < 1 || !param || !param[0]) {
		return TJS_E_BADPARAMCOUNT;
	}

	if (!IsTJSReady()) {
		LogToFile(">>> NeuroExec: TJS not ready!\n");
		if (result) {
			*result = TJS_W("Error: TJS not initialized yet");
		}
		return TJS_S_OK;
	}

	if (param[0]->Type() == tvtString) {
		ttstr code = *param[0];
		LogToFile(">>> NeuroExec: executing TJS code\n");

		tTJSVariant execResult;
		TVPExecuteExpression(code, &execResult);

		if (result) {
			*result = execResult;
		}
	}

	return TJS_S_OK;
}

}  // namespace

NCB_REGISTER_FUNCTION(neuroTest, NeuroTestFunction);
NCB_REGISTER_FUNCTION(neuroGetVersion, NeuroGetVersion);
NCB_REGISTER_FUNCTION(neuroEcho, NeuroEcho);
NCB_REGISTER_FUNCTION(neuroSendData, NeuroSendData);
NCB_REGISTER_FUNCTION(neuroIsTJSReady, NeuroIsTJSReady);
NCB_REGISTER_FUNCTION(neuroIsKAGReady, NeuroIsKAGReady);
NCB_REGISTER_FUNCTION(neuroShowConsole, NeuroShowConsole);
NCB_REGISTER_FUNCTION(neuroExec, NeuroExec);
NCB_REGISTER_FUNCTION(neuroConsoleLog, NeuroConsoleLog);
NCB_REGISTER_FUNCTION(neuroDump, NeuroDump);
NCB_REGISTER_FUNCTION(neuroGetCurrentContext, NeuroGetCurrentContext);
NCB_REGISTER_FUNCTION(neuroBuildVoiceMap, NeuroBuildVoiceMap);
NCB_REGISTER_FUNCTION(neuroExecFile, NeuroExecFile);

void RegisterTJSBindings() {
	ncbAutoRegister::AllRegist();
}

void UnregisterTJSBindings() {
	ncbAutoRegister::AllUnregist();
}
