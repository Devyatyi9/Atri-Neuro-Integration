#pragma once

void RegisterTJSBindings();
void UnregisterTJSBindings();
void RetryInstallExitHookIfReady();

// Reads the table of sound descriptions from neuro-assets beside the game.
// Waits on the disk, so not for the game's main thread.
void LoadAudioContextTable();
