#include "PluginLifecycle.h"

#include "SelfLink.h"

#pragma warning(push)
#pragma warning(disable : 26495)  // Uninitialized member variable (tp_stub)
#include "tp_stub.h"
#pragma warning(pop)

namespace {
SRWLOCK gLifecycleLock = SRWLOCK_INIT;
bool gLinked = false;
iTVPFunctionExporter *gExporter = nullptr;
}  // namespace

extern "C" __declspec(dllexport) bool __cdecl InitPlugin() {
	OutputDebugStringW(L"[Neuro-Integration] InitPlugin called.\n");
	iTVPFunctionExporter *const exporter = WaitForExporterInit(10000);
	if (!exporter) {
		return false;
	}

	HRESULT const result = V2Link(exporter);
	if (FAILED(result)) {
		OutputDebugStringW(L"[Neuro-Integration] V2Link failed.\n");
		return false;
	}
	return true;
}

extern "C" __declspec(dllexport) HRESULT __stdcall V2Link(
    iTVPFunctionExporter *exporter) {
	if (!exporter) {
		OutputDebugStringW(L"[Neuro-Integration] V2Link received null exporter.\n");
		return E_INVALIDARG;
	}

	AcquireSRWLockExclusive(&gLifecycleLock);
	if (gLinked) {
		HRESULT const result = exporter == gExporter ? S_FALSE : E_UNEXPECTED;
		ReleaseSRWLockExclusive(&gLifecycleLock);
		OutputDebugStringW(
		    exporter == gExporter
		        ? L"[Neuro-Integration] Plugin is already linked.\n"
		        : L"[Neuro-Integration] Exporter changed while linked.\n");
		return result;
	}

	if (!TVPInitImportStub(exporter)) {
		ReleaseSRWLockExclusive(&gLifecycleLock);
		OutputDebugStringW(
		    L"[Neuro-Integration] Import stub initialization failed.\n");
		return E_FAIL;
	}
	gExporter = exporter;
	gLinked = true;
	ReleaseSRWLockExclusive(&gLifecycleLock);
	OutputDebugStringW(L"[Neuro-Integration] Plugin linked.\n");
	return S_OK;
}

extern "C" __declspec(dllexport) HRESULT __stdcall V2Unlink() {
	AcquireSRWLockExclusive(&gLifecycleLock);
	if (!gLinked) {
		ReleaseSRWLockExclusive(&gLifecycleLock);
		OutputDebugStringW(L"[Neuro-Integration] Plugin is already unlinked.\n");
		return S_FALSE;
	}

	TVPUninitImportStub();
	gExporter = nullptr;
	gLinked = false;
	ReleaseSRWLockExclusive(&gLifecycleLock);
	OutputDebugStringW(L"[Neuro-Integration] Plugin unlinked.\n");
	return S_OK;
}
