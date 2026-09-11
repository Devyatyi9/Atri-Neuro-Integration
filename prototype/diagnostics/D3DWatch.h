#pragma once

#include <cstddef>

// Watching what the game asks Direct3D for.
//
// The engine logs `Direct3D could not allocate sufficient memory` and nothing
// else: not what it was allocating, not how big, not how many times. That is
// where a run died, and the log alone could not say why -- the request turned
// out to be a modest one while the process had most of its address space free.
//
// The engine's own draw device creates exactly one texture, the screen's, and
// releases it before making another. So whatever fills Direct3D up is created
// somewhere we cannot read: the game's own code, loading its animations. The
// only place both are visible is the interface between them, which is why this
// sits on d3d9 rather than on anything of ours.
//
// It watches, and by default changes nothing: every call is forwarded untouched
// and the answer is passed back as it came. The one exception has to be asked
// for -- see D3DWatchNudgeResetOnRefusal below.
bool D3DWatchInstall();
void D3DWatchReport();

// How hard the frame path is having to push. A lock that has to wait is the
// driver saying its queue of discarded buffers is not coming back fast enough,
// and that wait grows long before a lock is refused outright -- so this is the
// warning the refusal itself arrives too late to give.
void D3DWatchPressure(char *out, size_t size);

// Turns the one intervention on or off; off is how it starts.
//
// Once a lock is refused, every later one is too, and the draw device never
// recovers: it asks the device whether it is lost, hears that it is fine, and
// so never resets it. With this on, the answer to that one question is changed
// to "needs a reset" after a refusal, and the game takes its own path from
// there.
void D3DWatchNudgeResetOnRefusal(bool on);
