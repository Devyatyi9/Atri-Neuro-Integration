# ATRI -My Dear Moments- Neuro-sama Game Integration

A plugin for integrating the visual novel *ATRI -My Dear Moments-* (KiriKiri-Z engine) with the Neuro-sama AI streamer API.
Enables Neuro-sama to interact with the game in real time — read dialogue, detect scene changes, and respond to in-game events.

## Architecture

```
ATRI.exe (KiriKiri-Z)
  └─ version.dll (proxy DLL injector)
       └─ Neuro-Integration.dll (plugin)
            ├─ ncbind TJS functions (18 registered)
            ├─ ContextMonitor thread (polling, 200ms)
            ├─ NeuroConsole (F12, Win32 debug UI)
            └─ TCP client for Neuro API
```

- **DLL Proxy Injection** — `version.dll` in the game directory loads `Neuro-Integration.dll`
- **TJS2 Plugin** — registers native functions into the KiriKiri-Z scripting engine via ncbind
- **Context Monitoring** — C++ polling thread calls external `.tjs` scripts every 200ms to track game state
- **No TJS-level tag hooks** — `kag.addHook()`, `kag.conductor.onTag` etc. don't fire in ATRI; polling is the only viable approach

## Key Findings

- Correct dialogue source: `kag.historyLayer.currentText` / `currentName` / `currentChara`
  (`kag.fore.message` is `void`)
- Voice detection: `kag.voicePlayingRatio` (float 0→1 during speech)
- Conductor status: `kag.conductor.status` (0=idle, 1=loading, 2=running)
- BGM: `kag.bgm.playingStorage` / `kag.bgm.inPlaying`
- All scripts are UTF-16 LE with BOM
- TJS2 differs from JS: `{}` is a block (use `%[]` for dicts), `typeof` is lowercase, no `for...in`, `void` instead of `null`
- ATRI `.tjs` fragment files (e.g. `function(x){...}` without trailing `;`) are valid in-game via eval/module but rejected by standalone checkers

## Components

| Component               | Location                          | Description                    |
| ----------------------- | --------------------------------- | ------------------------------ |
| `Neuro-Integration.dll` | `KiriKiri-Test/integration-test/` | Main plugin DLL                |
| `KiriKiri-linker`       | `KiriKiri-Test/KiriKiri-linker/`  | Proxy DLL (version.dll)        |
| `context_monitor.tjs`   | `ATRI-TestVersion\neuro_scripts/` | Runtime game state tracking    |
| `tjscheck`              | `krkrz-tjscheck/`                 | Standalone TJS2 syntax checker |

## Tools

- `Tools/neuro_tcp_client.ps1` — PowerShell TCP client for sending TJS code to the game and receiving results
- Cheat Engine table (`ATRI-MyDearMoments-.CT`) — function addresses for engine internals
- `gen_ccdb.ps1` — generates `compile_commands.json` for clang-tidy/clangd

## Building

- **Solution:** `KiriKiri-Test/KiriKiri-Test.sln` (Visual Studio, Windows x86, 32-bit)
  - Debug|Win32 → plugin log to `plugin_log.txt`
  - Release|Win32 → minimal logging
- **tjscheck:** `krkrz-tjscheck\vcproj\tjscheck.vcxproj` (standalone, no KiriKiri runtime)

## Documentation

- `docs/TODO.md` — current task list
- `docs/ARCHITECTURE.md` — plugin architecture overview
- `docs/GAME-EVENTS-REFERENCE.md` — all discovered runtime events with TJS sources
- `docs/TJSCHECK.md` — tjscheck usage and edge cases
- `docs/kirikiri/` — TJS2 syntax, KAG commands, PSB format, batch processing ideas
- `docs/GameObjectsKeys/` — property/method keys for game objects
- `docs/Overview/GAME-MECHANICS.md` — KAG tags and game methods
