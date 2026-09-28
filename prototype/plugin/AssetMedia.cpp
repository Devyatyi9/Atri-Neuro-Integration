#include "AssetMedia.h"

#include <shlwapi.h>
#include <windows.h>

#include <string>

#pragma warning(push)
#pragma warning(disable : 26495)
#include "tp_stub.h"
#pragma warning(pop)

#include "Logging.h"

namespace {

std::wstring AssetRoot() {
	wchar_t exe[MAX_PATH];
	DWORD const length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring path(exe, length);
	path.resize(path.find_last_of(L'\\') + 1);
	return path + L"neuro-assets\\";
}

class NeuroAssetMedia : public iTVPStorageMedia {
	tjs_uint RefCount = 1;
	std::wstring Root = AssetRoot();

	// "domain/path" with the extension dropped: "./images/overlay_n.png" is
	// images\overlay_n under the root.
	std::wstring LocalPath(ttstr const &name) const {
		std::wstring path = name.c_str();
		size_t const slash = path.find(L'/');
		path = slash == std::wstring::npos ? path : path.substr(slash + 1);
		size_t const dot = path.find_last_of(L'.');
		size_t const lastSlash = path.find_last_of(L'/');
		if (dot != std::wstring::npos &&
		    (lastSlash == std::wstring::npos || dot > lastSlash)) {
			path.resize(dot);
		}
		for (wchar_t &c : path) {
			if (c == L'/')
				c = L'\\';
		}
		return Root + path;
	}

 public:
	void TJS_INTF_METHOD AddRef() override { ++RefCount; }
	void TJS_INTF_METHOD Release() override {
		if (--RefCount == 0)
			delete this;
	}
	void TJS_INTF_METHOD GetName(ttstr &name) override { name = TJS_W("neuro"); }
	void TJS_INTF_METHOD NormalizeDomainName(ttstr &) override { }
	void TJS_INTF_METHOD NormalizePathName(ttstr &) override { }

	bool TJS_INTF_METHOD CheckExistentStorage(ttstr const &name) override {
		DWORD const attributes = GetFileAttributesW(LocalPath(name).c_str());
		return attributes != INVALID_FILE_ATTRIBUTES &&
		       (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}

	tTJSBinaryStream *TJS_INTF_METHOD Open(ttstr const &name,
	                                       tjs_uint32 flags) override {
		if ((flags & TJS_BS_ACCESS_MASK) != TJS_BS_READ) {
			TVPThrowExceptionMessage(TJS_W("neuro:// storage is read-only"));
		}
		IStream *stream = nullptr;
		HRESULT const hr = SHCreateStreamOnFileEx(
		    LocalPath(name).c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
		    FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &stream);
		if (FAILED(hr) || !stream) {
			ttstr const message = ttstr(TJS_W("cannot open neuro://")) + name;
			TVPThrowExceptionMessage(message.c_str());
		}
		// The adapter keeps its own reference.
		tTJSBinaryStream *adapter = TVPCreateBinaryStreamAdapter(stream);
		stream->Release();
		return adapter;
	}

	void TJS_INTF_METHOD GetListAt(ttstr const &, iTVPStorageLister *) override {
	}

	// Not handed out as a local file: a caller given the real name would open
	// it by that name, and the name has no extension.
	void TJS_INTF_METHOD GetLocallyAccessibleName(ttstr &name) override {
		name.Clear();
	}
};

bool g_Mounted = false;

}  // namespace

bool MountAssetMedia() {
	if (g_Mounted) {
		return true;
	}
	// The engine's media table takes no reference of its own, so ours is kept
	// for as long as the process lives.
	TVPRegisterStorageMedia(new NeuroAssetMedia());
	g_Mounted = true;
	Log("[Assets] neuro:// storage mounted\n");
	return true;
}
