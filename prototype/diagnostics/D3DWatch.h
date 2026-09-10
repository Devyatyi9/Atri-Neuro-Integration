#pragma once

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
// It watches. It does not change what the game does: every call is forwarded
// untouched and the answer is passed back as it came.
bool D3DWatchInstall();
void D3DWatchReport();
