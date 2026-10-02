#include "D3DWatch.h"

#include <windows.h>

#include <psapi.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>

#include <intrin.h>

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

// And where LockRect sits in IDirect3DTexture9's, counted the same way:
// IUnknown's three, then IDirect3DResource9 (GetDevice, SetPrivateData,
// GetPrivateData, FreePrivateData, SetPriority, GetPriority, PreLoad, GetType),
// then IDirect3DBaseTexture9 (SetLOD, GetLOD, GetLevelCount,
// SetAutoGenFilterType, GetAutoGenFilterType, GenerateMipSubLevels), then
// GetLevelDesc, GetSurfaceLevel, LockRect.
//
// This is the call that actually fails: the texture exists, and what Direct3D
// cannot find room for is the copy it hands back to be written into.
constexpr int kLockRectSlot = 19;

// UnlockRect is the next one along, and it is counted for one reason: a lock
// that is never released is a buffer the driver cannot take back. If these two
// counts part company, what runs out is the driver's own room rather than the
// process's.
constexpr int kUnlockRectSlot = 20;

// TestCooperativeLevel is the fourth entry of IDirect3DDevice9's table, right
// after IUnknown's three. The draw device asks it after a failed frame and
// resets only when the answer says the device needs one.
constexpr int kTestCooperativeLevelSlot = 3;

using CreateTextureFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *,
                                                     UINT,
                                                     UINT,
                                                     UINT,
                                                     DWORD,
                                                     D3DFORMAT,
                                                     D3DPOOL,
                                                     IDirect3DTexture9 **,
                                                     HANDLE *);

using LockRectFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DTexture9 *,
                                                UINT,
                                                D3DLOCKED_RECT *,
                                                RECT const *,
                                                DWORD);

using UnlockRectFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DTexture9 *, UINT);

using TestCooperativeLevelFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *);

CreateTextureFn g_OriginalCreateTexture = nullptr;

// The table our own device carries.
void **g_OurDeviceTable = nullptr;

// The functions displaced from the game's device tables, one entry per distinct
// pair. A rebuilt device can come with a table of its own while its functions
// stay the same, so entries are kept by function: tables come and go, the
// functions behind them do not.
struct DisplacedFunctions {
	CreateTextureFn createTexture;
	TestCooperativeLevelFn test;
};

constexpr int kDisplacedSlots = 4;
DisplacedFunctions g_Displaced[kDisplacedSlots] = {};
std::atomic<int> g_DisplacedCount(0);

// Recent tables and the entry each one displaced. Only needed when more than
// one distinct pair exists; oldest is overwritten, since a freed table's
// address may be handed to the next device.
struct TableToDisplaced {
	void **table;
	int index;
};

constexpr int kRecentTableSlots = 8;
TableToDisplaced g_RecentTables[kRecentTableSlots] = {};
int g_RecentTableNext = 0;

DisplacedFunctions const *DisplacedFor(IDirect3DDevice9 *device) {
	int const count = g_DisplacedCount.load(std::memory_order_acquire);
	if (count == 1) {
		return &g_Displaced[0];
	}
	void **const table = *reinterpret_cast<void ***>(device);
	for (TableToDisplaced const &recent : g_RecentTables) {
		if (recent.table == table) {
			return &g_Displaced[recent.index];
		}
	}
	return nullptr;
}

// The device the game's textures last reported, and how many times that has
// changed. A recreated device can land at the address of the one it replaced,
// in which case the change goes unseen here; the CreateTexture count is the
// other witness of a rebuild.
std::atomic<void *> g_LastGameDevice(nullptr);
std::atomic<long long> g_DeviceChanges(0);

// The intervention: off unless asked for, armed by a refusal, and spent on the
// next question the draw device asks about its device.
std::atomic<bool> g_NudgeEnabled(false);
std::atomic<bool> g_NudgeArmed(false);
std::atomic<long long> g_Nudges(0);

// How often the game asks about its device at all. Without this there is no
// telling whether an armed nudge is waiting for a question that never comes.
std::atomic<long long> g_Tests(0);

// Set to refuse one lock on purpose; cleared by the refusal it causes.
std::atomic<bool> g_FailNextLock(false);

// True from a refused lock until the next one that succeeds.
std::atomic<bool> g_Refusing(false);

// Wall-clock time and private bytes, in the form the other logs use, so a line
// here can be lined up with the game's own log.
void Stamp(char *out, size_t size) {
	SYSTEMTIME now = {};
	GetLocalTime(&now);
	PROCESS_MEMORY_COUNTERS counters = {};
	K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
	sprintf_s(out, size, "at %02d:%02d:%02d.%03d private=%zuMB", now.wHour,
	          now.wMinute, now.wSecond, now.wMilliseconds,
	          counters.PagefileUsage / (1024 * 1024));
}

bool WatchGameDevice(IDirect3DDevice9 *game);
void NoteGameDevice(IDirect3DTexture9 *texture);

// Textures do not all share one table -- a pooled one and a driver-backed one
// can be different classes inside d3d9 -- so each table that gets patched is
// remembered with the function it displaced, and the hook finds its way back
// through the table the texture itself carries.
struct PatchedTable {
	void **table;
	LockRectFn original;
	UnlockRectFn originalUnlock;
};

constexpr int kTableSlots = 4;
PatchedTable g_LockTables[kTableSlots] = {};
int g_LockTableCount = 0;

PatchedTable *PatchedTableFor(IDirect3DTexture9 *texture) {
	void **table = *reinterpret_cast<void ***>(texture);
	for (int i = 0; i < g_LockTableCount; ++i) {
		if (g_LockTables[i].table == table) {
			return &g_LockTables[i];
		}
	}
	return nullptr;
}

// Counted rather than logged one by one: a working game creates textures all
// the time, and a line each would bury the log it is meant to explain. What
// matters is the shape -- how many, how big, and how many refused.
std::atomic<long long> g_Created(0);
std::atomic<long long> g_Refused(0);
std::atomic<long long> g_Bytes(0);
std::atomic<long long> g_Locked(0);
std::atomic<long long> g_LockRefused(0);
std::atomic<long long> g_Unlocked(0);

// The last stretch of lock times, in ticks, kept as a ring so that reading them
// costs nothing the frame path can feel. A window rather than an average since
// the run began: what matters is how the last second or so went, not how the
// game was doing ten minutes ago.
constexpr int kWindowSlots = 128;
std::atomic<long long> g_LockTicks[kWindowSlots] = {};
std::atomic<long long> g_LockWrites(0);
std::atomic<long long> g_LockTicksWorst(0);

// Which call site asked. The address is the game's own code, so it is written
// as module+offset: that is what a disassembler wants, and it survives the
// module being loaded somewhere else next time.
struct CallerSite {
	void *address;
	long long calls;
	long long bytes;
};

constexpr int kCallerSlots = 16;
CallerSite g_Callers[kCallerSlots] = {};
std::mutex g_CallersLock;

void NoteCaller(void *address, long long bytes) {
	std::lock_guard<std::mutex> hold(g_CallersLock);
	for (int i = 0; i < kCallerSlots; ++i) {
		if (g_Callers[i].address == address) {
			g_Callers[i].calls += 1;
			g_Callers[i].bytes += bytes;
			return;
		}
		if (!g_Callers[i].address) {
			g_Callers[i].address = address;
			g_Callers[i].calls = 1;
			g_Callers[i].bytes = bytes;
			return;
		}
	}
	// Full. Only a handful of places load pictures, so a table this size holds
	// them all; a latecomer past that is noise and is dropped rather than
	// evicting a site that has been counted since the run began.
}

void DescribeAddress(void *address, char *out, size_t size) {
	HMODULE module = nullptr;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                       reinterpret_cast<LPCWSTR>(address), &module) &&
	    module) {
		wchar_t path[MAX_PATH];
		if (GetModuleFileNameW(module, path, MAX_PATH)) {
			wchar_t const *name = wcsrchr(path, L'\\');
			name = name ? name + 1 : path;
			sprintf_s(out, size, "%ls+0x%lX", name,
			          static_cast<unsigned long>(reinterpret_cast<char *>(address) -
			                                     reinterpret_cast<char *>(module)));
			return;
		}
	}
	sprintf_s(out, size, "0x%p", address);
}

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
	void *const caller = _ReturnAddress();
	DisplacedFunctions const *const watched = DisplacedFor(device);
	CreateTextureFn const original = (watched && watched->createTexture)
	                                     ? watched->createTexture
	                                     : g_OriginalCreateTexture;
	HRESULT const hr = original(device, width, height, levels, usage, format,
	                            pool, texture, shared);
	long long const bytes =
	    static_cast<long long>(width) * height * BytesPerPixel(format);
	if (SUCCEEDED(hr)) {
		g_Created.fetch_add(1, std::memory_order_relaxed);
		g_Bytes.fetch_add(bytes, std::memory_order_relaxed);
		NoteCaller(caller, bytes);
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
		char where[128];
		DescribeAddress(caller, where, sizeof(where));
		char line[384];
		sprintf_s(line,
		          "[D3D] CreateTexture refused: %ux%u fmt=%d usage=0x%lx pool=%d"
		          " hr=0x%08lX available=%uMB from=%s refusals=%lld created=%lld\n",
		          width, height, static_cast<int>(format),
		          static_cast<unsigned long>(usage), static_cast<int>(pool),
		          static_cast<unsigned long>(hr), available / (1024u * 1024u),
		          where, refused + 1, g_Created.load(std::memory_order_relaxed));
		Log(line);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE WatchedLockRect(IDirect3DTexture9 *texture,
                                          UINT level,
                                          D3DLOCKED_RECT *locked,
                                          RECT const *rect,
                                          DWORD flags) {
	void *const caller = _ReturnAddress();
	PatchedTable const *const patched = PatchedTableFor(texture);
	LockRectFn const original = patched ? patched->original : nullptr;
	NoteGameDevice(texture);
	if (!original) {
		// Nothing sane is left to do: this table was never patched here, so
		// there is no original to forward to.
		return E_FAIL;
	}
	LARGE_INTEGER before = {};
	QueryPerformanceCounter(&before);
	// A refused lock never happened, so the forwarding is skipped entirely:
	// nothing is locked, and the game has nothing to unlock afterwards.
	bool const faked = g_FailNextLock.exchange(false);
	HRESULT const hr =
	    faked ? E_OUTOFMEMORY : original(texture, level, locked, rect, flags);
	if (SUCCEEDED(hr)) {
		LARGE_INTEGER after = {};
		QueryPerformanceCounter(&after);
		long long const spot = g_LockWrites.fetch_add(1, std::memory_order_relaxed);
		long long const ticks = after.QuadPart - before.QuadPart;
		g_LockTicks[spot % kWindowSlots].store(ticks, std::memory_order_relaxed);
		if (ticks > g_LockTicksWorst.load(std::memory_order_relaxed)) {
			g_LockTicksWorst.store(ticks, std::memory_order_relaxed);
		}
		g_Locked.fetch_add(1, std::memory_order_relaxed);
		if (g_Refusing.load(std::memory_order_relaxed) &&
		    g_Refusing.exchange(false)) {
			char stamp[96];
			Stamp(stamp, sizeof(stamp));
			char line[224];
			sprintf_s(line,
			          "[D3D] locks work again after %lld refusals, %s locked=%lld\n",
			          g_LockRefused.load(std::memory_order_relaxed), stamp,
			          g_Locked.load(std::memory_order_relaxed));
			Log(line);
		}
		return hr;
	}

	g_Refusing.store(true, std::memory_order_relaxed);
	long long const refused =
	    g_LockRefused.fetch_add(1, std::memory_order_relaxed);
	if (g_NudgeEnabled.load(std::memory_order_relaxed)) {
		g_NudgeArmed.store(true, std::memory_order_relaxed);
	}
	if (refused < 20 || refused % 100 == 0) {
		// What was being locked matters as much as that it failed, and the
		// texture itself is the only one who knows.
		D3DSURFACE_DESC desc = {};
		texture->GetLevelDesc(level, &desc);
		char where[128];
		DescribeAddress(caller, where, sizeof(where));
		char stamp[96];
		Stamp(stamp, sizeof(stamp));
		char line[448];
		sprintf_s(line,
		          "[D3D] LockRect refused: %ux%u fmt=%d usage=0x%lx pool=%d"
		          " level=%u flags=0x%lx hr=0x%08lX from=%s refusals=%lld"
		          " locked=%lld %s\n",
		          desc.Width, desc.Height, static_cast<int>(desc.Format),
		          static_cast<unsigned long>(desc.Usage),
		          static_cast<int>(desc.Pool), level,
		          static_cast<unsigned long>(flags), static_cast<unsigned long>(hr),
		          where, refused + 1, g_Locked.load(std::memory_order_relaxed),
		          stamp);
		Log(line);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE WatchedUnlockRect(IDirect3DTexture9 *texture,
                                            UINT level) {
	PatchedTable const *const patched = PatchedTableFor(texture);
	if (!patched || !patched->originalUnlock) {
		return E_FAIL;
	}
	HRESULT const hr = patched->originalUnlock(texture, level);
	if (SUCCEEDED(hr)) {
		g_Unlocked.fetch_add(1, std::memory_order_relaxed);
	}
	return hr;
}

// The device is fine, and that is the problem: the draw device resets only what
// it believes is lost. After a refusal this says the device needs a reset once,
// which is the game's own way out and is then left alone again.
HRESULT STDMETHODCALLTYPE
WatchedTestCooperativeLevel(IDirect3DDevice9 *device) {
	g_Tests.fetch_add(1, std::memory_order_relaxed);
	DisplacedFunctions const *const watched = DisplacedFor(device);
	HRESULT const hr =
	    (watched && watched->test) ? watched->test(device) : D3D_OK;
	if (hr != D3D_OK || !g_NudgeArmed.exchange(false)) {
		return hr;
	}
	long long const nudges = g_Nudges.fetch_add(1, std::memory_order_relaxed);
	char line[128];
	sprintf_s(line, "[D3D] telling the game its device needs a reset (%lld)\n",
	          nudges + 1);
	Log(line);
	return D3DERR_DEVICENOTRESET;
}

// The texture tables are shared by every device, so a device of our own is
// enough to reach them. It is not enough for CreateTexture: the game's device
// is a class of its own, and that one is watched through WatchGameDevice once
// its first texture is locked. The window is there because D3D9 will not
// create a device without one.
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

// A table sits in read-only memory, so it has to be opened, written and closed
// again; leaving it writable would be a change to the process nobody asked for.
bool PatchSlot(void **table, int slot, void *replacement, void **displaced) {
	DWORD previous = 0;
	if (!VirtualProtect(&table[slot], sizeof(void *), PAGE_READWRITE,
	                    &previous)) {
		return false;
	}
	*displaced = table[slot];
	table[slot] = replacement;
	VirtualProtect(&table[slot], sizeof(void *), previous, &previous);
	return true;
}

// Patches the table of a game device that is not patched yet, and says whether
// it did. Whether a table is patched is read from the slot itself, so a table
// at a reused address is still seen as new. The displaced pair is recorded
// before either slot is switched: the hooks find their originals through it,
// so it has to be there by the time the first call arrives.
bool WatchGameDevice(IDirect3DDevice9 *game) {
	void **const table = *reinterpret_cast<void ***>(game);
	if (table == g_OurDeviceTable) {
		Log("[D3D] the game's device is the class being watched\n");
		return false;
	}
	if (table[kCreateTextureSlot] ==
	    reinterpret_cast<void *>(&WatchedCreateTexture)) {
		return false;
	}
	DisplacedFunctions const pair = {
	    reinterpret_cast<CreateTextureFn>(table[kCreateTextureSlot]),
	    reinterpret_cast<TestCooperativeLevelFn>(
	        table[kTestCooperativeLevelSlot])};
	int const count = g_DisplacedCount.load(std::memory_order_acquire);
	int index = 0;
	while (index < count &&
	       (g_Displaced[index].createTexture != pair.createTexture ||
	        g_Displaced[index].test != pair.test)) {
		++index;
	}
	if (index == count) {
		if (count >= kDisplacedSlots) {
			Log("[D3D] the game's device has other functions, and there is no "
			    "room to watch it\n");
			return false;
		}
		g_Displaced[index] = pair;
	}
	g_RecentTables[g_RecentTableNext] = {table, index};
	g_RecentTableNext = (g_RecentTableNext + 1) % kRecentTableSlots;
	if (index == count) {
		g_DisplacedCount.store(count + 1, std::memory_order_release);
	}

	void *displaced = nullptr;
	bool const patched =
	    PatchSlot(table, kCreateTextureSlot,
	              reinterpret_cast<void *>(&WatchedCreateTexture), &displaced) &&
	    PatchSlot(table, kTestCooperativeLevelSlot,
	              reinterpret_cast<void *>(&WatchedTestCooperativeLevel),
	              &displaced);
	char line[160];
	sprintf_s(line,
	          patched
	              ? "[D3D] watching game device table %p (functions %d of %d)\n"
	              : "[D3D] game device table %p: would not open (%d of %d)\n",
	          static_cast<void *>(table), index + 1,
	          g_DisplacedCount.load(std::memory_order_acquire));
	Log(line);
	return patched;
}

// Called on every lock, since a lock is the only place the game's device shows
// itself. Whenever the device differs from the last one seen, its class is
// taken on if it is new, so a rebuilt device stays watched.
void NoteGameDevice(IDirect3DTexture9 *texture) {
	IDirect3DDevice9 *game = nullptr;
	if (FAILED(texture->GetDevice(&game)) || !game) {
		return;
	}
	void *const previous = g_LastGameDevice.exchange(game);
	if (previous != game) {
		bool const newClass = WatchGameDevice(game);
		if (previous != nullptr) {
			long long const changes =
			    g_DeviceChanges.fetch_add(1, std::memory_order_relaxed) + 1;
			char stamp[96];
			Stamp(stamp, sizeof(stamp));
			char line[320];
			sprintf_s(
			    line,
			    "[D3D] the game's device changed (%lld): %p -> %p, %s, %s"
			    " refusals=%lld\n",
			    changes, previous, static_cast<void *>(game),
			    newClass ? "a new table, now watched too" : "a table already watched",
			    stamp, g_LockRefused.load(std::memory_order_relaxed));
			Log(line);
		}
	}
	game->Release();
}

void WatchTextureTable(IDirect3DDevice9 *device, DWORD usage, D3DPOOL pool) {
	if (g_LockTableCount >= kTableSlots) {
		return;
	}
	IDirect3DTexture9 *sample = nullptr;
	if (FAILED(device->CreateTexture(1, 1, 1, usage, D3DFMT_X8R8G8B8, pool,
	                                 &sample, nullptr)) ||
	    !sample) {
		return;
	}
	void **table = *reinterpret_cast<void ***>(sample);
	sample->Release();
	for (int i = 0; i < g_LockTableCount; ++i) {
		if (g_LockTables[i].table == table) {
			return;
		}
	}
	void *displaced = nullptr;
	if (!PatchSlot(table, kLockRectSlot,
	               reinterpret_cast<void *>(&WatchedLockRect), &displaced)) {
		return;
	}
	g_LockTables[g_LockTableCount].table = table;
	g_LockTables[g_LockTableCount].original =
	    reinterpret_cast<LockRectFn>(displaced);
	// Read out of the table rather than taken from the patch: the unlock hook
	// finds its way back through this entry, so the entry has to be finished
	// and counted before the slot starts pointing at the hook.
	g_LockTables[g_LockTableCount].originalUnlock =
	    reinterpret_cast<UnlockRectFn>(table[kUnlockRectSlot]);
	g_LockTableCount += 1;

	void *displacedUnlock = nullptr;
	PatchSlot(table, kUnlockRectSlot,
	          reinterpret_cast<void *>(&WatchedUnlockRect), &displacedUnlock);
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
	g_OurDeviceTable = table;
	void *displaced = nullptr;
	bool installed =
	    PatchSlot(table, kCreateTextureSlot,
	              reinterpret_cast<void *>(&WatchedCreateTexture), &displaced);
	if (installed) {
		g_OriginalCreateTexture = reinterpret_cast<CreateTextureFn>(displaced);
	}

	// A texture's table cannot be reached through the device, only through a
	// texture, so a sample of each kind the game uses is made here purely to be
	// looked at. Two kinds because they need not be one class inside d3d9.
	WatchTextureTable(device, D3DUSAGE_DYNAMIC, D3DPOOL_DEFAULT);
	WatchTextureTable(device, 0, D3DPOOL_MANAGED);

	// The device and its textures were only ever a way to reach the tables. The
	// patches stay: a table is the class's, shared by everything the process
	// makes later, including the game's own.
	device->Release();
	d3d->Release();
	if (window) {
		DestroyWindow(window);
	}
	char line[128];
	sprintf_s(line, "[D3D] watching CreateTexture=%s LockRect=%d table(s)\n",
	          installed ? "yes" : "no", g_LockTableCount);
	Log(line);
	return installed || g_LockTableCount > 0;
}

void D3DWatchPressure(char *out, size_t size) {
	LARGE_INTEGER frequency = {};
	QueryPerformanceFrequency(&frequency);
	long long const written = g_LockWrites.load(std::memory_order_relaxed);
	int const held = static_cast<int>(
	    written < kWindowSlots ? written : static_cast<long long>(kWindowSlots));
	long long sum = 0;
	long long worst = 0;
	for (int i = 0; i < held; ++i) {
		long long const ticks = g_LockTicks[i].load(std::memory_order_relaxed);
		sum += ticks;
		if (ticks > worst) {
			worst = ticks;
		}
	}
	double const scale =
	    frequency.QuadPart ? 1000000.0 / frequency.QuadPart : 0.0;
	sprintf_s(out, size,
	          "textures=%lld devices=%lld locks=%lld unlocks=%lld refused=%lld"
	          " tests=%lld nudges=%lld window=%d avg_us=%.0f worst_us=%.0f"
	          " peak_us=%.0f",
	          g_Created.load(std::memory_order_relaxed),
	          g_DeviceChanges.load(std::memory_order_relaxed),
	          g_Locked.load(std::memory_order_relaxed),
	          g_Unlocked.load(std::memory_order_relaxed),
	          g_LockRefused.load(std::memory_order_relaxed),
	          g_Tests.load(std::memory_order_relaxed),
	          g_Nudges.load(std::memory_order_relaxed), held,
	          held ? sum * scale / held : 0.0, worst * scale,
	          g_LockTicksWorst.load(std::memory_order_relaxed) * scale);
}

void D3DWatchNudgeResetOnRefusal(bool on) {
	g_NudgeEnabled.store(on, std::memory_order_relaxed);
	if (!on) {
		g_NudgeArmed.store(false, std::memory_order_relaxed);
	}
	Log(on ? "[D3D] a refusal will now ask the game to reset its device\n"
	       : "[D3D] refusals are watched and nothing else\n");
}

void D3DWatchFailNextLock() {
	g_FailNextLock.store(true, std::memory_order_relaxed);
	Log("[D3D] the next frame will be refused on purpose\n");
}

void D3DWatchReport() {
	if (!g_OriginalCreateTexture && g_LockTableCount == 0) {
		return;
	}
	char line[256];
	sprintf_s(line,
	          "[D3D] textures created=%lld refused=%lld total=%lldMB"
	          " locks=%lld unlocks=%lld refused=%lld\n",
	          g_Created.load(std::memory_order_relaxed),
	          g_Refused.load(std::memory_order_relaxed),
	          g_Bytes.load(std::memory_order_relaxed) / (1024 * 1024),
	          g_Locked.load(std::memory_order_relaxed),
	          g_Unlocked.load(std::memory_order_relaxed),
	          g_LockRefused.load(std::memory_order_relaxed));
	Log(line);

	// Who asked, biggest first: the point of the whole watch is to name the code
	// that fills Direct3D up, and one sorted list says it more plainly than the
	// refusals do.
	std::lock_guard<std::mutex> hold(g_CallersLock);
	for (int printed = 0; printed < kCallerSlots; ++printed) {
		int best = -1;
		for (int i = 0; i < kCallerSlots; ++i) {
			if (!g_Callers[i].address || g_Callers[i].bytes < 0) {
				continue;
			}
			if (best < 0 || g_Callers[i].bytes > g_Callers[best].bytes) {
				best = i;
			}
		}
		if (best < 0) {
			break;
		}
		char where[128];
		DescribeAddress(g_Callers[best].address, where, sizeof(where));
		sprintf_s(line, "[D3D] from %s: calls=%lld total=%lldMB\n", where,
		          g_Callers[best].calls, g_Callers[best].bytes / (1024 * 1024));
		Log(line);
		// Marked as spent so the next pass picks the one below it.
		g_Callers[best].bytes = -1;
	}
}
