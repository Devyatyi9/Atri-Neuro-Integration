#include "SelfLink.h"

#include <windows.h>

#include <cstdio>

#include "Logging.h"

namespace {
// These addresses are specific to the tested ATRI executable version.
constexpr DWORD kExporterRva = 0x003B4538;
constexpr DWORD kVtableRva = 0x00374A50;
}  // namespace

iTVPFunctionExporter *WaitForExporterInit(int maxWaitMs) {
	int elapsed = 0;
	int const checkInterval = 100;
	bool mismatchReported = false;

	char logBuf[256];
	sprintf_s(logBuf, "Waiting for exporter (RVA=0x%08X, vtable RVA=0x%08X)...\n",
	          kExporterRva, kVtableRva);
	LogToFile(logBuf);

	while (elapsed < maxWaitMs) {
		HMODULE mainModule = GetModuleHandle(nullptr);
		if (!mainModule) {
			Sleep(checkInterval);
			elapsed += checkInterval;
			continue;
		}

		BYTE *baseAddress = reinterpret_cast<BYTE *>(mainModule);
		auto *exporter =
		    reinterpret_cast<iTVPFunctionExporter *>(baseAddress + kExporterRva);

		if (!IsBadReadPtr(exporter, sizeof(void *))) {
			void **vtable = *reinterpret_cast<void ***>(exporter);
			void **expectedVtable =
			    reinterpret_cast<void **>(baseAddress + kVtableRva);

			if (vtable == expectedVtable) {
				sprintf_s(logBuf, "Exporter found after %d ms at 0x%08X\n", elapsed,
				          reinterpret_cast<DWORD>(exporter));
				LogToFile(logBuf);
				return exporter;
			}

			if (!mismatchReported && vtable != nullptr) {
				mismatchReported = true;
				DWORD actualRva =
				    static_cast<DWORD>(reinterpret_cast<BYTE *>(vtable) - baseAddress);

				sprintf_s(
				    logBuf,
				    "ERROR: VTable mismatch! Expected RVA=0x%08X, Actual RVA=0x%08X\n",
				    kVtableRva, actualRva);
				LogToFile(logBuf);

				wchar_t message[512];
				swprintf_s(message,
				           L"VTable mismatch detected!\n\n"
				           L"Expected: 0x%08X (RVA: 0x%08X)\n"
				           L"Actual:   0x%08X\n\n"
				           L"Update kVtableRva in SelfLink.cpp\n"
				           L"Actual RVA = 0x%08X",
				           reinterpret_cast<DWORD>(expectedVtable), kVtableRva,
				           reinterpret_cast<DWORD>(vtable), actualRva);

				MessageBoxW(nullptr, message, L"Neuro Plugin - RVA Error",
				            MB_OK | MB_ICONWARNING);
				return nullptr;
			}
		}

		Sleep(checkInterval);
		elapsed += checkInterval;

		if (elapsed % 1000 == 0) {
			sprintf_s(logBuf, "Still waiting... %d/%d ms\n", elapsed, maxWaitMs);
			LogToFile(logBuf);
		}
	}

	LogToFile("ERROR: Exporter init timeout!\n");
	return nullptr;
}
