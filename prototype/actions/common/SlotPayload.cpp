#include "SlotPayload.h"

namespace {

// The layout, in one table. Every field is named once here and nowhere else,
// which is the point of the file: a new field that overlaps an old one stops
// the build instead of quietly rewriting it.
struct Field {
	int shift;
	int width;
};

constexpr Field kVisibleTabs{0, 4};
constexpr Field kEnabledTabs{4, 4};
constexpr Field kPageCount{8, 6};
constexpr Field kPage{14, 6};
constexpr Field kEditMenuOpen{20, 1};
constexpr Field kEditItems{21, 4};
constexpr Field kEditItemsShown{25, 4};
constexpr Field kCopyMode{29, 2};

constexpr int Mask(Field field) {
	return (1 << field.width) - 1;
}

constexpr int End(Field field) {
	return field.shift + field.width;
}

// Each field starts where the one before it ends, and the last stops short of
// the sign bit. Stated as a chain so that widening any field is caught here
// rather than in whatever field it grew into.
static_assert(kVisibleTabs.shift == 0, "layout starts at bit zero");
static_assert(kEnabledTabs.shift == End(kVisibleTabs), "enabled tabs overlap");
static_assert(kPageCount.shift == End(kEnabledTabs), "page count overlaps");
static_assert(kPage.shift == End(kPageCount), "page overlaps");
static_assert(kEditMenuOpen.shift == End(kPage), "edit menu flag overlaps");
static_assert(kEditItems.shift == End(kEditMenuOpen), "edit items overlap");
static_assert(kEditItemsShown.shift == End(kEditItems), "shown items overlap");
static_assert(kCopyMode.shift == End(kEditItemsShown), "copy mode overlaps");
static_assert(End(kCopyMode) <= 31, "payload does not fit in a signed int");

int Put(int value, Field field) {
	return (value & Mask(field)) << field.shift;
}

int Get(int payload, Field field) {
	return (payload >> field.shift) & Mask(field);
}

}  // namespace

int PackSlotPayload(SlotPayload const &value) {
	return Put(value.visibleTabs, kVisibleTabs) |
	       Put(value.enabledTabs, kEnabledTabs) |
	       Put(value.pageCount, kPageCount) | Put(value.page, kPage) |
	       Put(value.editMenuOpen ? 1 : 0, kEditMenuOpen) |
	       Put(value.editItems, kEditItems) |
	       Put(value.editItemsShown, kEditItemsShown) |
	       Put(value.copyMode, kCopyMode);
}

SlotPayload UnpackSlotPayload(int payload) {
	SlotPayload value;
	value.visibleTabs = Get(payload, kVisibleTabs);
	value.enabledTabs = Get(payload, kEnabledTabs);
	value.pageCount = Get(payload, kPageCount);
	value.page = Get(payload, kPage);
	value.editMenuOpen = Get(payload, kEditMenuOpen) != 0;
	value.editItems = Get(payload, kEditItems);
	value.editItemsShown = Get(payload, kEditItemsShown);
	value.copyMode = Get(payload, kCopyMode);
	return value;
}

std::string IsoDate(std::string const &text) {
	// yyyy/mm/dd, digits where digits go.
	if (text.size() < 10 || text[4] != '/' || text[7] != '/') {
		return text;
	}
	for (size_t i : {0, 1, 2, 3, 5, 6, 8, 9}) {
		if (text[i] < '0' || text[i] > '9') {
			return text;
		}
	}
	std::string out = text;
	out[4] = '-';
	out[7] = '-';
	return out;
}
