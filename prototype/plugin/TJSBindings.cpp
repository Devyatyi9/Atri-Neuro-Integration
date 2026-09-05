#include <windows.h>

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 26495)
#include "tp_stub.h"
#pragma warning(pop)

#include "ncbind.hpp"

#include "Logging.h"
#include "NeuroConsole.h"
#include "PluginLifecycle.h"
#include "TJSBindings.h"
#include "TJSExecutor.h"
#include "UtfConversion.h"

namespace {

static tjs_error TJS_INTF_METHOD NeuroTestFunction(tTJSVariant *result,
                                                   tjs_int numparams,
                                                   tTJSVariant **param,
                                                   iTJSDispatch2 *objthis) {
	OutputDebugStringW(L"[NEURO PLUGIN] NeuroTestFunction called from TJS!\n");

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
	if (result) {
		*result = (tTVInteger)1;
	}
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroEcho(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
	OutputDebugStringW(L"[NEURO PLUGIN] NeuroEcho called!\n");

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
	tTJSVariant checkResult;
	TVPExecuteExpression(
	    TJS_W(
	        "typeof Debug != 'undefined' && typeof Debug.console != 'undefined'"),
	    &checkResult);

	if ((tTVInteger)checkResult != 0) {
		tTJSVariant dummy;
		TVPExecuteExpression(TJS_W("Debug.console.visible = true"), &dummy);
		if (result)
			*result = (tTVInteger)1;
	} else {
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

	if (result)
		*result = (tTVInteger)(ready ? 1 : 0);
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroIsTJSReady(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *objthis) {
	bool ready = IsTJSReady();

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
		std::wstring const log = GetPluginLog(offset, count, fromEnd);
		*result = ttstr(log.c_str());
	}
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroClearPluginLog(tTJSVariant *result,
                                                     tjs_int numparams,
                                                     tTJSVariant **param,
                                                     iTJSDispatch2 *objthis) {
	ClearPluginLog();
	if (result) {
		*result = (tTVInteger)1;
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

	tTJSVariant execResult;
	TVPExecuteExpression(ttstr(wideCode.c_str()), &execResult);

	if (result)
		*result = execResult;
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
		if (result) {
			*result = TJS_W("Error: TJS not initialized yet");
		}
		return TJS_S_OK;
	}

	if (param[0]->Type() == tvtString) {
		ttstr code = *param[0];

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
NCB_REGISTER_FUNCTION(neuroBeginExitShutdown, NeuroBeginExitShutdown);
NCB_REGISTER_FUNCTION(neuroStressBlockNextSyncCall,
                      NeuroStressBlockNextSyncCall);
NCB_REGISTER_FUNCTION(neuroStressHoldSyncCalls, NeuroStressHoldSyncCalls);
NCB_REGISTER_FUNCTION(neuroEcho, NeuroEcho);
NCB_REGISTER_FUNCTION(neuroIsTJSReady, NeuroIsTJSReady);
NCB_REGISTER_FUNCTION(neuroIsKAGReady, NeuroIsKAGReady);
NCB_REGISTER_FUNCTION(neuroShowConsole, NeuroShowConsole);
NCB_REGISTER_FUNCTION(neuroExec, NeuroExec);
NCB_REGISTER_FUNCTION(neuroConsoleLog, NeuroConsoleLog);
NCB_REGISTER_FUNCTION(neuroGetPluginLog, NeuroGetPluginLog);
NCB_REGISTER_FUNCTION(neuroClearPluginLog, NeuroClearPluginLog);
NCB_REGISTER_FUNCTION(neuroDump, NeuroDump);
NCB_REGISTER_FUNCTION(neuroExecFile, NeuroExecFile);

static tjs_error TJS_INTF_METHOD NeuroPrototypeUpkeep(tTJSVariant *result,
                                                      tjs_int numparams,
                                                      tTJSVariant **param,
                                                      iTJSDispatch2 *objthis) {
	// Called from the continuous hook when safe, outside tag processing.
	RetryInstallExitHookIfReady();
	if (result) {
		*result = (tTVInteger)1;
	}
	return TJS_S_OK;
}

NCB_REGISTER_FUNCTION(neuroPrototypeUpkeep, NeuroPrototypeUpkeep);

static bool g_ExitHookInstalled = false;

void RetryInstallExitHookIfReady() {
	if (g_ExitHookInstalled)
		return;
	std::string ready;
	bool const executed = ExecuteTJSSync(
	    L"(function(){ return typeof MyYesNoDialogLayer == 'Object' && "
	    L"typeof MyYesNoDialog != 'undefined'; })()",
	    &ready, 5000, TJSExecMode::Expression);
	// The probe answers with the engine's own rendering of true, which is "1".
	if (!executed || ready != "1")
		return;
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
	    L"neuroBeginExitShutdown(); "
	    L"} "
	    L"return result; "
	    L"}; "
	    L"return typeof MyYesNoDialogLayer.onItemClick == 'Object'; "
	    L"})()",
	    &installed, 5000, TJSExecMode::Expression);
	if (!installExecuted || installed != "1") {
		return;
	}
	g_ExitHookInstalled = true;
}

void RegisterTJSBindings() {
	ncbAutoRegister::AllRegist();
	RetryInstallExitHookIfReady();
}

void UnregisterTJSBindings() {
	ncbAutoRegister::AllUnregist();
	g_ExitHookInstalled = false;
}
