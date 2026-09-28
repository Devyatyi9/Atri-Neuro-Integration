#pragma once

// Registers the "neuro" storage media: neuro://. The engine picks
// the image decoder by the extension in the path, so a file stored without
// one still loads, and the extension exists only in the name TJS asks for.
// Call on the game's main thread; later calls do nothing.
bool MountAssetMedia();
