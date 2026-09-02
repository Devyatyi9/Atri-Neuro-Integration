#include "SlotPayload.h"

#include "TestMain.h"

namespace {

// A round trip is the whole contract: whatever the screen puts in has to come
// back out unchanged, field by field. Checking the fields one at a time rather
// than comparing packed ints is deliberate -- when a field lands on its
// neighbour, this says which two.
void ExpectRoundTrip(SlotPayload const &value, char const *what) {
	SlotPayload const back = UnpackSlotPayload(PackSlotPayload(value));
	ReportCheck(back.visibleTabs == value.visibleTabs, __FILE__, __LINE__, what);
	ReportCheck(back.enabledTabs == value.enabledTabs, __FILE__, __LINE__, what);
	ReportCheck(back.pageCount == value.pageCount, __FILE__, __LINE__, what);
	ReportCheck(back.page == value.page, __FILE__, __LINE__, what);
	ReportCheck(back.editMenuOpen == value.editMenuOpen, __FILE__, __LINE__,
	            what);
	ReportCheck(back.editItems == value.editItems, __FILE__, __LINE__, what);
	ReportCheck(back.editItemsShown == value.editItemsShown, __FILE__, __LINE__,
	            what);
	ReportCheck(back.copyMode == value.copyMode, __FILE__, __LINE__, what);
}

// Every field at its widest, one at a time, with the rest left at zero. This is
// the case that catches a field spilling into the next: if it does, some other
// field comes back non-zero.
void ExpectAloneAtMaximum(int SlotPayload::*field,
                          int maximum,
                          char const *what) {
	SlotPayload value;
	value.*field = maximum;
	SlotPayload const back = UnpackSlotPayload(PackSlotPayload(value));
	ReportCheck(back.*field == maximum, __FILE__, __LINE__, what);

	// Nothing else may have moved.
	SlotPayload expected;
	expected.*field = maximum;
	ReportCheck(back.visibleTabs == expected.visibleTabs, __FILE__, __LINE__,
	            what);
	ReportCheck(back.enabledTabs == expected.enabledTabs, __FILE__, __LINE__,
	            what);
	ReportCheck(back.pageCount == expected.pageCount, __FILE__, __LINE__, what);
	ReportCheck(back.page == expected.page, __FILE__, __LINE__, what);
	ReportCheck(back.editMenuOpen == false, __FILE__, __LINE__, what);
	ReportCheck(back.editItems == expected.editItems, __FILE__, __LINE__, what);
	ReportCheck(back.editItemsShown == expected.editItemsShown, __FILE__,
	            __LINE__, what);
	ReportCheck(back.copyMode == expected.copyMode, __FILE__, __LINE__, what);
}

}  // namespace

void RunSlotPayloadTests() {
	SlotPayload empty;
	CHECK(PackSlotPayload(empty) == 0);
	ExpectRoundTrip(empty, "empty");

	// The save screen as it actually stands: three tabs on the bar, all
	// pressable, page two of nine.
	SlotPayload save;
	save.visibleTabs = kSlotTabLoad | kSlotTabQuick | kSlotTabVoice;
	save.enabledTabs = save.visibleTabs;
	save.pageCount = 9;
	save.page = 2;
	ExpectRoundTrip(save, "save screen");

	// The edit menu standing over a slot: four entries drawn, two of them
	// refused.
	SlotPayload menu;
	menu.editMenuOpen = true;
	menu.editItemsShown = 0xf;
	menu.editItems = 0x5;
	ExpectRoundTrip(menu, "edit menu");

	// Waiting for a destination slot.
	SlotPayload copying;
	copying.visibleTabs = kSlotTabSave;
	copying.page = 1;
	copying.pageCount = 9;
	copying.copyMode = 2;
	ExpectRoundTrip(copying, "copy destination");

	// Everything at once, every field at its widest. Nothing on this screen
	// looks like this, which is why it is worth checking.
	SlotPayload full;
	full.visibleTabs = 0xf;
	full.enabledTabs = 0xf;
	full.pageCount = 63;
	full.page = 63;
	full.editMenuOpen = true;
	full.editItems = 0xf;
	full.editItemsShown = 0xf;
	full.copyMode = 3;
	ExpectRoundTrip(full, "all fields at maximum");
	// The layout leaves one bit spare, so the fullest payload stays positive.
	CHECK(PackSlotPayload(full) > 0);

	ExpectAloneAtMaximum(&SlotPayload::visibleTabs, 0xf, "visibleTabs alone");
	ExpectAloneAtMaximum(&SlotPayload::enabledTabs, 0xf, "enabledTabs alone");
	ExpectAloneAtMaximum(&SlotPayload::pageCount, 63, "pageCount alone");
	ExpectAloneAtMaximum(&SlotPayload::page, 63, "page alone");
	ExpectAloneAtMaximum(&SlotPayload::editItems, 0xf, "editItems alone");
	ExpectAloneAtMaximum(&SlotPayload::editItemsShown, 0xf,
	                     "editItemsShown alone");
	ExpectAloneAtMaximum(&SlotPayload::copyMode, 3, "copyMode alone");

	// The flag on its own, since it is the one field that is not an int.
	SlotPayload flag;
	flag.editMenuOpen = true;
	SlotPayload const flagBack = UnpackSlotPayload(PackSlotPayload(flag));
	CHECK(flagBack.editMenuOpen);
	CHECK(flagBack.editItems == 0);
	CHECK(flagBack.page == 0);

	// A value too large is cut to its field instead of spilling. A probe that
	// misreads must not be able to rewrite a field it never touched.
	SlotPayload overflow;
	overflow.pageCount = 64;
	SlotPayload const cut = UnpackSlotPayload(PackSlotPayload(overflow));
	CHECK(cut.pageCount == 0);
	CHECK(cut.page == 0);
	CHECK(cut.visibleTabs == 0);

	SlotPayload wide;
	wide.copyMode = 7;
	SlotPayload const cutWide = UnpackSlotPayload(PackSlotPayload(wide));
	CHECK(cutWide.copyMode == 3);
	CHECK(cutWide.editItemsShown == 0);
}
