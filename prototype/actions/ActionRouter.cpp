#include "ActionRouter.h"

#include <windows.h>

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "ActionCommon.h"
#include "ActionJson.h"
#include "ActionProbes.h"
#include "ActionSchemas.h"
#include "ActionSet.h"
#include "Logging.h"
#include "NeuroClient.h"
#include "ScreenBacklog.h"
#include "ScreenGallery.h"
#include "ScreenMenu.h"
#include "ScreenPlay.h"
#include "ScreenSlots.h"
#include "ScreenTitle.h"

namespace {

std::mutex g_ActionIdsMutex;
std::deque<std::string> g_ActionIds;
std::unordered_set<std::string> g_ActionIdSet;
constexpr size_t kMaxRememberedActionIds = 256;

// An action of Neuro's is being answered right now. The server takes one
// action at a time and refuses a force while a result is still owed, so the
// force below waits for this to clear.
std::atomic<bool> g_ActionInFlight(false);

// A dialog appeared and its force has not gone out yet. Set when the dialog
// state is entered, cleared when it is sent or when any other screen takes
// over.
std::atomic<bool> g_DialogForcePending(false);

// A force of ours the server has not worked through yet. It takes one at a
// time and warns that a second while the first is in progress causes problems,
// and there is no message to cancel one. The dialog closing is not enough to
// call it done: a dialog dismissed by hand leaves the force sitting in the
// server's queue, and piling a second one on top is exactly what the
// specification warns against. Only the arrival of the forced action itself
// clears this -- including a late one, answered with a failure because the
// dialog is long gone, which is what drains the queue on the server side.
std::atomic<bool> g_ForceOutstanding(false);

// Kind in the high half, payload in the low half. They were packed into one
// int before, and the payloads collided with the kind constants: the
// backlog's voice mask reached the gallery's base bit, so a backlog with a
// voiced line registered the Extra actions instead (2026-08-20). Splitting
// the halves makes that impossible rather than merely unlikely.
std::atomic<long long> g_SyncedCapabilityState(-1);
// The kind alone, for the places that ask "am I already on this screen" without
// caring about the payload.
int SyncedCapabilityKind();
long long CapabilityKey(int kind, int payload) {
	return (static_cast<long long>(kind) << 32) |
	       static_cast<unsigned int>(payload);
}
// Whether the "still starting up" notice reached the server. It cannot simply
// be sent once on entering the loading state, because that happens before the
// SDK connects.
std::atomic<bool> g_TitleLoadingNoticeSent(false);
// The same problem for the first-run language screen, which comes up in the
constexpr int kCapabilityStateUnknown = -1;
// The title screen. What it offers lives in ScreenTitle; only its identity
// is needed here.
constexpr int kCapabilityStateTitle = 0;
constexpr int kCapabilityStateMenu = 2;
// Startup and the title screen are distinct capability states. The plugin
// initialises long before the title exists -- splashes run for roughly 24s --
// so registering continue_game at init advertised an action that was certain to
// fail for that whole window, and an autonomous engine does call it (observed
// in Gary: randy dispatched continue_game on its own). This state carries no
// actions and explains the wait through context instead.
constexpr int kCapabilityStateTitleLoading = 3;
// A confirmation dialog is modal: nothing else can succeed while it is up, so
// it gets its own state carrying only the answer action.
constexpr int kCapabilityStateDialog = 4;
// The story. What it offers lives in ScreenPlay; only its identity is needed
// here.
constexpr int kCapabilityStatePlay = 17;
// The text window hidden away by the bar's hide button: the artwork is all
// that is left on screen, the hide button itself goes enabled = 0, and a click
// brings the window back instead of advancing. Only one action can apply.
constexpr int kCapabilityStateTextHidden = 8;
// 9 was a second title state, for a title with nothing to continue. Three of
// its entries turned out to be conditional, not one, so they are flags on the
// one title state instead.
// Any screen that is not the title and not play. Which actions apply is
// decided by the controls the screen actually carries rather than by a list of
// state names: the backlog has a "back" control but no "title" one, and states
// nobody has visited yet (cgmode, scenemode, soundmode, search) would
// otherwise fall through every branch and leave the previous set registered.
constexpr int kCapabilityStateMenuBackOnly = 10;
// A recognised screen carrying none of the controls we drive. Registering
// nothing is the honest answer; the alternative was stale actions.
constexpr int kCapabilityStateBare = 11;
// The load screen. It carries the same back/title controls as the other menu
// screens, but also the save slots, and entering it is the moment to say what
// is in them -- the slot numbers mean nothing on their own.
constexpr int kCapabilityStateLoad = 12;
// The save screen. Same list as load, different mode: occupied slots are
// writable there rather than loadable, so what is worth saying on entry is
// which slots are free.
constexpr int kCapabilityStateSave = 13;
// The voice tab: the same list again, holding recorded lines instead of saves.
// Pressing one replays it, the way the backlog replays a line.
constexpr int kCapabilityStateVoice = 14;
// The backlog. What it offers lives in ScreenBacklog; only its identity is
// needed here.
constexpr int kCapabilityStateBacklog = 15;
// The Extra screen. What it offers lives in ScreenGallery; only its identity
// is needed here.
constexpr int kCapabilityStateGallery = 16;
// The language screen the game opens once, on the very first launch. It reports
// itself as "option" like the settings screen and carries the same "back"
// control, but that control is the OK of a first-run prompt, not a way back to
// a screen that does not exist yet.
constexpr int kCapabilityStateLanguageSelect = 18;

// The per-slot edit menu on a save screen. A state of its own rather than a
// flag on the slot screens: it takes the screen the way a dialog does, so it
// carries its own set and its choice is forced.
constexpr int kCapabilityStateEditMenu = 19;

bool ClaimActionId(std::string const &id) {
	std::lock_guard<std::mutex> lock(g_ActionIdsMutex);
	if (!g_ActionIdSet.insert(id).second) {
		return false;
	}
	g_ActionIds.push_back(id);
	if (g_ActionIds.size() > kMaxRememberedActionIds) {
		g_ActionIdSet.erase(g_ActionIds.front());
		g_ActionIds.pop_front();
	}
	return true;
}

// Reads a window of the conversation history. Registered only while the
// backlog is open: the history is readable at any time, but advertising it
// everywhere would put an action in front of Neuro on every screen for
// something she asked for by opening the log.

void HandleNeuroAction(std::string const &id,
                       std::string const &name,
                       std::string const &data) {
	if (id.empty()) {
		Log("[Action] Ignoring action without an ID\n");
		return;
	}
	if (!ClaimActionId(id)) {
		Log("[Action] Ignoring duplicate action ID\n");
		return;
	}
	// The force is retired when the answer has been carried out, not when it
	// arrives: the server retires its own only once the action is complete, and
	// clearing on arrival let a fast open-close-open send the next force while
	// the server still held the first.
	bool const answersForce =
	    name == "answer_dialog" || name == "close_edit_menu" ||
	    name == "edit_comment" || name == "edit_copy" || name == "edit_move" ||
	    name == "edit_erase";
	struct ForceGuard {
		bool active;
		~ForceGuard() {
			if (active) {
				g_ForceOutstanding.store(false, std::memory_order_release);
			}
		}
	} forceGuard{answersForce};

	// Handlers answer before returning, so the flag covers exactly the window
	// in which a result is owed. The refresh reads it to hold back a force.
	g_ActionInFlight.store(true, std::memory_order_release);
	struct InFlightGuard {
		~InFlightGuard() {
			g_ActionInFlight.store(false, std::memory_order_release);
		}
	} guard;

	// Every action belongs to the screen that offers it, and each module answers
	// for its own names. An action nobody claims is answered here rather than
	// left silent: the specification asks for a failing result on an unknown
	// name, and silence would leave Neuro waiting.
	if (ScreenTitle::Handle(id, name, data) ||
	    ScreenPlay::Handle(id, name, data) ||
	    ScreenMenu::Handle(id, name, data) ||
	    ScreenSlots::Handle(id, name, data) ||
	    ScreenBacklog::Handle(id, name, data) ||
	    ScreenGallery::Handle(id, name, data)) {
		return;
	}
	NeuroSendActionResult(id.c_str(), false, "Unsupported action.");
}

int SyncedCapabilityKind() {
	long long const key = g_SyncedCapabilityState.load(std::memory_order_acquire);
	return key < 0 ? kCapabilityStateUnknown : static_cast<int>(key >> 32);
}

// Names the screen for the context feed. Neuro is otherwise navigating blind:
// the action set changes, but nothing says what is now in front of her.
char const *ScreenName(int kind) {
	switch (kind) {
		case kCapabilityStateTitle:
			return "the title screen";
		case kCapabilityStatePlay:
			return "the story";
		case kCapabilityStateLoad:
			return "the load screen";
		case kCapabilityStateSave:
			return "the save screen";
		case kCapabilityStateBacklog:
			return "the backlog";
		case kCapabilityStateGallery:
			return "the Extra screen";
		case kCapabilityStateVoice:
			return "the voice collection";
		case kCapabilityStateMenu:
		case kCapabilityStateMenuBackOnly:
			return "a settings screen";
		case kCapabilityStateDialog:
			return "a confirmation dialog";
		// Named but not announced through this path: the branch that registers
		// it sends its own line, which has to say what the screen wants rather
		// than only where we are.
		case kCapabilityStateLanguageSelect:
		case kCapabilityStateEditMenu:
			return nullptr;
		case kCapabilityStateTextHidden:
			return "the story with the interface hidden";
		default:
			return nullptr;
	}
}

void SyncCapabilities(int kind, int payload = 0) {
	long long const key = CapabilityKey(kind, payload);
	int const previousKind = SyncedCapabilityKind();
	if (key == g_SyncedCapabilityState.load(std::memory_order_acquire)) {
		return;
	}
	g_DialogForcePending.store(kind == kCapabilityStateDialog ||
	                               kind == kCapabilityStateEditMenu,
	                           std::memory_order_release);
	if (kind == kCapabilityStateTitleLoading) {
		// No action can succeed yet, so advertise none. The explanatory context
		// is not sent here: this state is entered during plugin init, before the
		// SDK connects, and NeuroSendContext drops anything sent while not ready.
		// RefreshPrototypeActionState retries it until it lands.
		NeuroSetRegisteredActions(nullptr, 0);
		Log("[Action] Capabilities: title-loading (none)\n");
	} else if (kind == kCapabilityStateTitle) {
		ActionSet set;
		ScreenTitle::AppendActions(set, payload);
		NeuroSetRegisteredActions(set.data(), set.count());
		ScreenTitle::SendEnteringContext(payload);
		Log(("[Action] Capabilities: title (" + set.names() + ")\n").c_str());
	} else if (kind == kCapabilityStatePlay) {
		ActionSet set;
		ScreenPlay::AppendActions(set, payload);
		NeuroSetRegisteredActions(set.data(), set.count());
		std::string summary = "[Action] Capabilities: ";
		summary += ScreenPlay::HasChoice(payload) ? "choice" : "game";
		if (ScreenPlay::HasPanel(payload)) {
			summary += "+panel";
		}
		Log((summary + " (" + set.names() + ")\n").c_str());
	} else if (kind == kCapabilityStateTextHidden) {
		ActionSet set;
		ScreenPlay::AppendHiddenActions(set);
		NeuroSetRegisteredActions(set.data(), set.count());
		Log(("[Action] Capabilities: ui-hidden (" + set.names() + ")\n").c_str());
	} else if (kind == kCapabilityStateDialog) {
		ActionSet set;
		ScreenMenu::AppendDialogActions(set);
		NeuroSetRegisteredActions(set.data(), set.count());
		// A confirmation is modal: the game stands still and offers nothing
		// else on screen, so the answer is forced rather than merely offered.
		// The force itself is left to the refresh, which waits until no action
		// result is owed.
		Log(("[Action] Capabilities: dialog (" + set.names() + ")\n").c_str());
	} else if (kind == kCapabilityStateEditMenu) {
		ActionSet set;
		ScreenSlots::AppendEditMenuActions(set, payload);
		NeuroSetRegisteredActions(set.data(), set.count());
		ScreenSlots::SendEditMenuContext(payload);
		// Like the dialog: the force itself is left to the refresh, which waits
		// until no action result is owed. The menu is usually opened by an
		// action of Neuro's own, so one always is at this moment.
		Log(("[Action] Capabilities: edit menu (" + set.names() + ")\n").c_str());
	} else if (kind == kCapabilityStateMenu ||
	           kind == kCapabilityStateMenuBackOnly) {
		// The settings screen carries tabs and a way back to the title; the
		// other menu screens carry neither, and their tab count is zero.
		ActionSet set;
		ScreenMenu::AppendMenuActions(set, payload, kind == kCapabilityStateMenu);
		NeuroSetRegisteredActions(set.data(), set.count());
		Log(("[Action] Capabilities: menu (" + set.names() + ")\n").c_str());
	} else if (kind == kCapabilityStateLoad) {
		ActionSet set;
		ScreenSlots::AppendActions(set, ScreenSlots::Mode::Load, payload);
		NeuroSetRegisteredActions(set.data(), set.count());
		ScreenSlots::SendEnteringContext(ScreenSlots::Mode::Load, payload);
		Log(("[Action] Capabilities: load (" + set.names() + ")\n").c_str());
	} else if (kind == kCapabilityStateSave) {
		ActionSet set;
		ScreenSlots::AppendActions(set, ScreenSlots::Mode::Save, payload);
		NeuroSetRegisteredActions(set.data(), set.count());
		ScreenSlots::SendEnteringContext(ScreenSlots::Mode::Save, payload);
		Log(("[Action] Capabilities: save (" + set.names() + ")\n").c_str());
	} else if (kind == kCapabilityStateGallery) {
		ActionSet set;
		ScreenGallery::AppendActions(set, payload);
		NeuroSetRegisteredActions(set.data(), set.count());
		Log(("[Action] Capabilities: extra (" + set.names() + ")\n").c_str());
	} else if (kind == kCapabilityStateBacklog) {
		// Only on the way in, not on every scroll: the flags change constantly
		// while paging, and the size of the history does not.
		if (SyncedCapabilityKind() != kCapabilityStateBacklog) {
			ScreenBacklog::SendEnteringContext();
		}
		ActionSet set;
		ScreenBacklog::AppendActions(set, payload);
		NeuroSetRegisteredActions(set.data(), set.count());
		Log(("[Action] Capabilities: backlog (" + set.names() + ")\n").c_str());
	} else if (kind == kCapabilityStateVoice) {
		ActionSet set;
		ScreenSlots::AppendActions(set, ScreenSlots::Mode::Voice, payload);
		NeuroSetRegisteredActions(set.data(), set.count());
		ScreenSlots::SendEnteringContext(ScreenSlots::Mode::Voice, payload);
		Log(("[Action] Capabilities: voice (" + set.names() + ")\n").c_str());
	} else if (kind == kCapabilityStateLanguageSelect) {
		ActionSet set;
		ScreenMenu::AppendLanguageActions(set);
		NeuroSetRegisteredActions(set.data(), set.count());
		// The notice goes through the retry in the refresh rather than being
		// sent here: this screen is up while the SDK may still be connecting.
		Log(("[Action] Capabilities: language-select (" + set.names() + ")\n")
		        .c_str());
	} else if (kind == kCapabilityStateBare) {
		NeuroSetRegisteredActions(nullptr, 0);
		Log("[Action] Capabilities: bare (none)\n");
	}
	// Only when the screen itself changed, not when its actions merely shifted:
	// the quick menu sliding in and out must not narrate itself.
	if (previousKind != kind) {
		char const *const name = ScreenName(kind);
		if (name) {
			NeuroSendContext((std::string("Now on ") + name + ".").c_str(), true);
		}
	}
	g_SyncedCapabilityState.store(key, std::memory_order_release);
}

}  // namespace

void InitializePrototypeActionRouter() {
	NeuroSetActionHandler(HandleNeuroAction);
	// Init runs during the startup splashes, so nothing is dispatchable yet;
	// the monitor promotes this to the real title state once the menu is usable.
	SyncCapabilities(kCapabilityStateTitleLoading);
}

void ShutdownPrototypeActionRouter() {
	NeuroSetActionHandler(nullptr);
	g_SyncedCapabilityState.store(kCapabilityStateUnknown,
	                              std::memory_order_release);
	g_TitleLoadingNoticeSent.store(false, std::memory_order_release);
	ScreenMenu::ResetLanguageNotice();
	// A reconnect starts a server that knows nothing of our earlier force.
	g_DialogForcePending.store(false, std::memory_order_release);
	g_ForceOutstanding.store(false, std::memory_order_release);
	g_ActionInFlight.store(false, std::memory_order_release);
	std::lock_guard<std::mutex> lock(g_ActionIdsMutex);
	g_ActionIds.clear();
	g_ActionIdSet.clear();
}

// Retried every poll until accepted: the loading state starts before the SDK
// connects, and NeuroSendContext drops anything sent while not ready rather
// than queueing it, so a single send on entering the state would be lost.
void TrySendTitleLoadingNotice() {
	if (g_TitleLoadingNoticeSent.load(std::memory_order_acquire)) {
		return;
	}
	if (NeuroSendContext(
	        "The game is starting up. The main menu is not available yet.",
	        true)) {
		g_TitleLoadingNoticeSent.store(true, std::memory_order_release);
	}
}

// Resolves any screen that is not the title, play, or one the refresh knows
// by name. Also the fallback when a known screen cannot be read: leaving the
// previous set registered would advertise actions from another screen.
void SyncScreenByControls(bool stateChanged) {
	// Anything that is neither the title nor play. Rather than matching a
	// list of screen names -- which kept missing ones, and left the previous
	// actions registered when it did -- ask the screen which controls it
	// carries. The backlog has "back" but no "title"; option/load/save carry
	// both; cgmode, scenemode, soundmode, voicemode and search have not been
	// visited yet and resolve themselves the same way.
	//
	// Re-probed only when the state string changes, so sitting on a screen
	// costs nothing beyond the one snapshot.
	int const synced = SyncedCapabilityKind();
	bool const settled = synced == kCapabilityStateMenu ||
	                     synced == kCapabilityStateMenuBackOnly ||
	                     synced == kCapabilityStateBare;
	// The tab count is re-read every poll, not once on the way in: the seventh
	// settings tab is not there yet at the moment the screen is entered, and a
	// single early sample fixed the maximum at six for as long as the screen
	// stayed open (found via Tony with validation off, 2026-08-20). The button
	// probes below stay behind the settled check -- only this one is cheap
	// enough to repeat.
	if (synced == kCapabilityStateLanguageSelect && !stateChanged) {
		// Sitting on the first-run screen. Nothing about it can change without
		// leaving it, and leaving changes the state string, so the only work
		// left here is landing the notice if the SDK was not ready for it yet.
		ScreenMenu::TrySendLanguageNotice();
		return;
	}
	if (settled && !stateChanged) {
		SyncCapabilities(synced, ProbeTabCount());
		return;
	} else if (ProbeLanguageScreen("screen language controls")) {
		// Checked before the back/title pair, which cannot tell this screen
		// apart from the backlog: both carry "back" and neither carries
		// "title".
		SyncCapabilities(kCapabilityStateLanguageSelect);
		ScreenMenu::TrySendLanguageNotice();
	} else if (!ProbeScreenButtonReady("back", "screen back button")) {
		SyncCapabilities(kCapabilityStateBare);
	} else if (ProbeScreenButtonReady("title", "screen title button")) {
		SyncCapabilities(kCapabilityStateMenu, ProbeTabCount());
	} else {
		SyncCapabilities(kCapabilityStateMenuBackOnly, ProbeTabCount());
	}
}

// A tick after the set changed, not in the same breath. What raises a modal --
// a confirmation, or the per-slot edit menu -- is usually an action of Neuro's
// own, and the server refuses a force while that action's result is still owed;
// deferring puts the force behind the result without having to guess at thread
// timing.
template <typename Send>
void TryIssuePendingForce(Send send) {
	if (!g_DialogForcePending.load(std::memory_order_acquire)) {
		return;
	}
	if (g_ForceOutstanding.load(std::memory_order_acquire)) {
		g_DialogForcePending.store(false, std::memory_order_release);
		Log("[Action] Not forced: an earlier force is still unanswered\n");
		return;
	}
	if (g_ActionInFlight.load(std::memory_order_acquire)) {
		return;
	}
	g_DialogForcePending.store(false, std::memory_order_release);
	g_ForceOutstanding.store(true, std::memory_order_release);
	send();
}

void RefreshPrototypeActionState() {
	// One probe per poll: this runs at 5 Hz against the game main thread, so
	// extra round trips here would be pure contention for no new information.
	ScreenSnapshot screen;
	if (!ProbeScreen(screen)) {
		return;
	}

	// A modal dialog owns the screen: answering it is the only thing that can
	// succeed, and leaving the other actions registered would invite calls that
	// are certain to fail. Checked before the state branches for that reason.
	ScreenSlots::Mode mode = ScreenSlots::Mode::Load;
	bool slotMode = ScreenSlots::ModeForScenario(screen.storage, mode);
	// The per-slot edit menu runs its own scenario, so while it is up the
	// storage names custom.ks and the screen underneath stops being
	// recognisable. The mode it was entered with is what still applies.
	static ScreenSlots::Mode lastSlotMode = ScreenSlots::Mode::Load;
	static bool lastSlotModeKnown = false;
	if (slotMode) {
		lastSlotMode = mode;
		lastSlotModeKnown = true;
	} else if (screen.editMenuReady && lastSlotModeKnown) {
		mode = lastSlotMode;
		slotMode = true;
	} else if (screen.panelShowing && lastSlotModeKnown) {
		// The panel is up but has not finished building. Rebuilding the set from
		// this reading describes neither the screen nor the menu -- it briefly
		// announced a settings screen -- so the previous set is held instead.
		return;
	}

	if (screen.dialogVisible) {
		SyncCapabilities(kCapabilityStateDialog);
		TryIssuePendingForce([]() { ScreenMenu::ForceDialogAnswer(); });
		return;
	}

	// Every distinct state value is logged once. A screen this code has never
	// seen used to fall through every branch and return without a trace, which
	// was indistinguishable from "capabilities are simply unchanged".
	static std::string lastObservedState;
	bool const stateChanged = screen.state != lastObservedState;
	if (stateChanged) {
		lastObservedState = screen.state;
		Log(("[Action] Observed state: '" + screen.state + "'\n").c_str());
	}

	if (screen.state == "title") {
		bool ready = false;
		int const title = ScreenTitle::ReadPayload(screen, ready);
		if (ready) {
			SyncCapabilities(kCapabilityStateTitle, title);
		} else if (SyncedCapabilityKind() != kCapabilityStateTitle) {
			// Only before the title has resolved. Once it has, a stray
			// not-ready reading holds the set rather than revoking it.
			SyncCapabilities(kCapabilityStateTitleLoading);
			TrySendTitleLoadingNotice();
		}
	} else if (screen.state.empty()) {
		// Still in the startup splashes: SystemHook.currentState is genuinely an
		// empty string until the title exists. This is the bulk of the wait, so
		// the notice has to cover it -- restricting it to the brief
		// title-but-not-ready window made it arrive just as the menu appeared,
		// which read as nonsense ("not available yet" the moment it was).
		//
		// Only believe an empty reading before any real state has been seen.
		// currentState is not a stable field: it was observed holding unrelated
		// values mid-session (e.g. "bgmPlaying" while the option screen was up),
		// so treating a stray empty reading as "back to loading" would revoke the
		// live action set for no reason.
		int const synced = SyncedCapabilityKind();
		if (synced == kCapabilityStateUnknown ||
		    synced == kCapabilityStateTitleLoading) {
			SyncCapabilities(kCapabilityStateTitleLoading);
			TrySendTitleLoadingNotice();
		}
	} else if (screen.state == "game") {
		// Nothing else can succeed while the window is away: the bar goes with
		// it, and a click restores the window instead of advancing.
		if (ScreenPlay::IsTextHidden(screen)) {
			SyncCapabilities(kCapabilityStateTextHidden);
		} else {
			SyncCapabilities(kCapabilityStatePlay, ScreenPlay::ReadPayload(screen));
		}
	} else if (slotMode) {
		// The slot screens go by the scenario they run, not by the state string:
		// the tabs move between the scenarios while the state keeps naming the
		// first one entered.
		int payload = 0;
		ScreenSlots::ReadPayload(screen, payload);
		if (ScreenSlots::PayloadHasEditMenu(payload)) {
			SyncCapabilities(kCapabilityStateEditMenu, payload);
			TryIssuePendingForce(
			    [payload]() { ScreenSlots::ForceEditMenuChoice(payload); });
		} else {
			SyncCapabilities(mode == ScreenSlots::Mode::Load ? kCapabilityStateLoad
			                 : mode == ScreenSlots::Mode::Save
			                     ? kCapabilityStateSave
			                     : kCapabilityStateVoice,
			                 payload);
		}
	} else if (screen.state == "cgmode") {
		int payload = 0;
		if (ScreenGallery::ReadPayload(payload)) {
			SyncCapabilities(kCapabilityStateGallery, payload);
		} else {
			SyncScreenByControls(stateChanged);
		}
	} else if (screen.state == "backlog") {
		int payload = 0;
		if (ScreenBacklog::ReadPayload(payload)) {
			SyncCapabilities(kCapabilityStateBacklog, payload);
		} else {
			SyncScreenByControls(stateChanged);
		}
	} else {
		SyncScreenByControls(stateChanged);
	}

	ScreenTitle::NoteRefresh(screen.state == "title");
}
