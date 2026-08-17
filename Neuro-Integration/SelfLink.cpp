#include "SelfLink.h"

#include <cstdint>

namespace {
constexpr DWORD kExporterRva = 0x003B4538;
constexpr DWORD kVtableRva = 0x00374A50;
constexpr DWORD kPollIntervalMs = 100;

bool IsReadableImageAddress(const BYTE *imageBase,
                            void const *address,
                            SIZE_T size) {
	MEMORY_BASIC_INFORMATION info{};
	if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) ||
	    info.AllocationBase != imageBase || info.State != MEM_COMMIT ||
	    (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
		return false;
	}
	auto const start = reinterpret_cast<uintptr_t>(address);
	auto const regionEnd =
	    reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
	return start <= regionEnd && size <= regionEnd - start;
}
}  // namespace

iTVPFunctionExporter *WaitForExporterInit(DWORD timeoutMs) {
	HMODULE const module = GetModuleHandleW(nullptr);
	if (!module) {
		OutputDebugStringW(L"[Neuro-Integration] Main module is unavailable.\n");
		return nullptr;
	}

	auto *const imageBase = reinterpret_cast<BYTE *>(module);
	if (!IsReadableImageAddress(imageBase, imageBase, sizeof(IMAGE_DOS_HEADER))) {
		OutputDebugStringW(L"[Neuro-Integration] DOS header is not readable.\n");
		return nullptr;
	}
	auto const *const dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(imageBase);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0) {
		OutputDebugStringW(L"[Neuro-Integration] Invalid DOS header.\n");
		return nullptr;
	}

	auto const baseAddress = reinterpret_cast<uintptr_t>(imageBase);
	auto const ntOffset = static_cast<uintptr_t>(dos->e_lfanew);
	if (ntOffset > UINTPTR_MAX - baseAddress) {
		OutputDebugStringW(L"[Neuro-Integration] Invalid NT header offset.\n");
		return nullptr;
	}
	auto const *const nt =
	    reinterpret_cast<const IMAGE_NT_HEADERS *>(baseAddress + ntOffset);
	if (!IsReadableImageAddress(imageBase, nt, sizeof(*nt)) ||
	    nt->Signature != IMAGE_NT_SIGNATURE ||
	    nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
		OutputDebugStringW(L"[Neuro-Integration] Invalid NT headers.\n");
		return nullptr;
	}

	DWORD const imageSize = nt->OptionalHeader.SizeOfImage;
	auto const fitsImage = [imageSize](DWORD rva) {
		return rva <= imageSize && sizeof(void *) <= imageSize - rva;
	};
	if (!fitsImage(kExporterRva) || !fitsImage(kVtableRva)) {
		OutputDebugStringW(
		    L"[Neuro-Integration] Exporter RVAs are outside the image.\n");
		return nullptr;
	}

	auto *const exporter =
	    reinterpret_cast<iTVPFunctionExporter *>(imageBase + kExporterRva);
	if (!IsReadableImageAddress(imageBase, exporter, sizeof(void *))) {
		OutputDebugStringW(L"[Neuro-Integration] Exporter slot is not readable.\n");
		return nullptr;
	}
	void *const expectedVtable = imageBase + kVtableRva;
	DWORD const startedAt = GetTickCount();
	for (;;) {
		void *const actualVtable = *reinterpret_cast<void **>(exporter);
		if (actualVtable == expectedVtable) {
			OutputDebugStringW(L"[Neuro-Integration] Exporter is ready.\n");
			return exporter;
		}

		DWORD const elapsed = GetTickCount() - startedAt;
		if (elapsed >= timeoutMs) {
			break;
		}
		DWORD const remaining = timeoutMs - elapsed;
		Sleep(remaining < kPollIntervalMs ? remaining : kPollIntervalMs);
	}

	OutputDebugStringW(L"[Neuro-Integration] Exporter wait timed out.\n");
	return nullptr;
}
