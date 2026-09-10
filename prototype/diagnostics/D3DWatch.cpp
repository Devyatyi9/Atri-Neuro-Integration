#include "D3DWatch.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>

#include <d3d9.h>

#include "Logging.h"

namespace {

// Where CreateTexture sits in IDirect3DDevice9's table. Fixed by the interface,
// which has not changed since D3D9 shipped: QueryInterface, AddRef, Release,
// TestCooperativeLevel, GetAvailableTextureMem, EvictManagedResources,
// GetDirect3D, GetDeviceCaps, GetDisplayMode, GetCreationParameters,
// SetCursorProperties, SetCursorPosition, ShowCursor,
// CreateAdditionalSwapChain, GetSwapChain, GetNumberOfSwapChains, Reset,
// Present, GetBackBuffer, GetRasterStatus, SetDialogBoxMode, SetGammaRamp,
// GetGammaRamp, CreateTexture.
constexpr int kCreateTextureSlot = 23;
constexpr int kAvailableTextureMemSlot = 4;

using CreateTextureFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *,
                                                     UINT,
                                                     UINT,
                                                     UINT,
                                                     DWORD,
                                                     D3DFORMAT,
                                                     D3DPOOL,
                                                     IDirect3DTexture9 **,
                                                     HANDLE *);

CreateTextureFn g_OriginalCreateTexture = nullptr;

// Counted rather than logged one by one: a working game creates textures all
// the time, and a line each would bury the log it is meant to explain. What
// matters is the shape -- how many, how big, and how many refused.
std::atomic<long long> g_Created(0);
std::atomic<long long> g_Refused(0);
std::atomic<long long> g_Bytes(0);

int BytesPerPixel(D3DFORMAT format) {
	switch (format) {
		case D3DFMT_A8R8G8B8:
		case D3DFMT_X8R8G8B8:
		case D3DFMT_A8B8G8R8:
		case D3DFMT_X8B8G8R8:
			return 4;
		case D3DFMT_R5G6B5:
		case D3DFMT_X1R5G5B5:
		case D3DFMT_A1R5G5B5:
		case D3DFMT_A4R4G4B4:
			return 2;
		case D3DFMT_A8:
			return 1;
		default:
			// Compressed and odd formats are not worth a table here; the count
			// still tells the story, and a wrong size would be worse than none.
			return 0;
	}
}

HRESULT STDMETHODCALLTYPE WatchedCreateTexture(IDirect3DDevice9 *device,
                                               UINT width,
                                               UINT height,
                                               UINT levels,
                                               DWORD usage,
                                               D3DFORMAT format,
                                               D3DPOOL pool,
                                               IDirect3DTexture9 **texture,
                                               HANDLE *shared) {
	HRESULT const hr = g_OriginalCreateTexture(
	    device, width, height, levels, usage, format, pool, texture, shared);
	long long const bytes =
	    static_cast<long long>(width) * height * BytesPerPixel(format);
	if (SUCCEEDED(hr)) {
		g_Created.fetch_add(1, std::memory_order_relaxed);
		g_Bytes.fetch_add(bytes, std::memory_order_relaxed);
		return hr;
	}

	// A refusal is rare and is the whole reason for being here, so this one is
	// written out in full -- including what Direct3D says it still has, which
	// is the number the engine's own message leaves out.
	long long const refused = g_Refused.fetch_add(1, std::memory_order_relaxed);
	if (refused < 20 || refused % 100 == 0) {
		UINT available = 0;
		if (device) {
			available = device->GetAvailableTextureMem();
		}
		char line[256];
		sprintf_s(line,
		          "[D3D] CreateTexture refused: %ux%u fmt=%d usage=0x%lx pool=%d"
		          " hr=0x%08lX available=%uMB refusals=%lld created=%lld\n",
		          width, height, static_cast<int>(format),
		          static_cast<unsigned long>(usage), static_cast<int>(pool),
		          static_cast<unsigned long>(hr), available / (1024u * 1024u),
		          refused + 1, g_Created.load(std::memory_order_relaxed));
		Log(line);
	}
	return hr;
}

// The table to patch belongs to the class, not to one device, so any device
// will do to find it -- including one of ours, made and thrown away. That is
// the reason for the window below: D3D9 will not create a device without one,
// and borrowing the game's would mean waiting for it to exist.
IDirect3DDevice9 *MakeThrowawayDevice(IDirect3D9 *&d3d, HWND &window) {
	d3d = Direct3DCreate9(D3D_SDK_VERSION);
	if (!d3d) {
		return nullptr;
	}
	window =
	    CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 1, 1, nullptr,
	                    nullptr, GetModuleHandleW(nullptr), nullptr);
	if (!window) {
		d3d->Release();
		d3d = nullptr;
		return nullptr;
	}
	D3DPRESENT_PARAMETERS present = {};
	present.Windowed = TRUE;
	present.SwapEffect = D3DSWAPEFFECT_DISCARD;
	present.BackBufferFormat = D3DFMT_UNKNOWN;
	present.hDeviceWindow = window;
	IDirect3DDevice9 *device = nullptr;
	HRESULT const hr = d3d->CreateDevice(
	    D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
	    D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES, &present,
	    &device);
	if (FAILED(hr)) {
		DestroyWindow(window);
		window = nullptr;
		d3d->Release();
		d3d = nullptr;
		return nullptr;
	}
	return device;
}

}  // namespace

bool D3DWatchInstall() {
	if (g_OriginalCreateTexture) {
		return true;
	}
	IDirect3D9 *d3d = nullptr;
	HWND window = nullptr;
	IDirect3DDevice9 *device = MakeThrowawayDevice(d3d, window);
	if (!device) {
		Log("[D3D] watch not installed: no device to read the table from\n");
		return false;
	}

	void **table = *reinterpret_cast<void ***>(device);
	DWORD previous = 0;
	bool installed = false;
	if (VirtualProtect(&table[kCreateTextureSlot], sizeof(void *), PAGE_READWRITE,
	                   &previous)) {
		g_OriginalCreateTexture =
		    reinterpret_cast<CreateTextureFn>(table[kCreateTextureSlot]);
		table[kCreateTextureSlot] = reinterpret_cast<void *>(&WatchedCreateTexture);
		VirtualProtect(&table[kCreateTextureSlot], sizeof(void *), previous,
		               &previous);
		installed = true;
	}

	// The device was only ever a way to reach the table. The patch stays: the
	// table is the class's, shared by every device in the process, including the
	// one the game makes later.
	device->Release();
	d3d->Release();
	if (window) {
		DestroyWindow(window);
	}
	Log(installed ? "[D3D] watching CreateTexture\n"
	              : "[D3D] watch not installed: the table would not open\n");
	(void)kAvailableTextureMemSlot;
	return installed;
}

void D3DWatchReport() {
	if (!g_OriginalCreateTexture) {
		return;
	}
	char line[192];
	sprintf_s(line, "[D3D] textures created=%lld refused=%lld total=%lldMB\n",
	          g_Created.load(std::memory_order_relaxed),
	          g_Refused.load(std::memory_order_relaxed),
	          g_Bytes.load(std::memory_order_relaxed) / (1024 * 1024));
	Log(line);
}
