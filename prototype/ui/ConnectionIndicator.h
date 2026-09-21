#pragma once

namespace ConnectionIndicator {

// A red dot in the corner of the game's UI while the link to Neuro is down,
// blinking so it is noticed without covering anything. Call from the periodic
// upkeep, on the game's main thread; it does nothing while the link is up.
void Update(bool connected);

}  // namespace ConnectionIndicator
