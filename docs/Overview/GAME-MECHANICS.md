# KiriKiri-Z ATRI Game Mechanics Reference

## Overview

This document contains key game objects, methods, and KAG tags useful for integrating Neuro-sama with the ATRI visual novel.

---

## Core Game Objects

### `kag` - Main Game Object

The `kag` object is the central interface for game control:

#### Selection/Choice System
| Method | Description |
|--------|-------------|
| `kag.addSelect(label, text)` | Add a choice option |
| `kag.addMapSelect(layer, positions)` | Add map-based choices |
| `kag.addMapSelectButton(layer, btn)` | Add button for map selection |
| `kag.afterSelect(callback)` | Callback after selection made |
| `kag.doneselect()` | End selection mode |
| `kag.seldone()` | Mark selection as done |

#### Tag Management
| Method | Description |
|--------|-------------|
| `kag.addTag(name, params)` | Add a KAG tag to queue |
| `kag.insertTag(name, params)` | Insert tag at current position |
| `kag.clear()` | Clear all queued tags |

#### Game State
| Property/Method | Description |
|-----------------|-------------|
| `kag.curLabel` | Current label name |
| `kag.curLine` | Current line number |
| `kag.curLineStr` | Current line string |
| `kag.curStorage` | Current scenario file |

#### Example: Intercept Choices
```javascript
// Store original addSelect
var origAddSelect = kag.addSelect;
kag.addSelect = function(label, text) {
    Debug.message("Choice offered: " + text);
    return origAddSelect(label, text);
};
```

---

### `kag.conductor` - Script Conductor

Controls script execution and timing:

| Method | Description |
|--------|-------------|
| `getAliasName()` | Get character alias/name |
| `curLabel` | Current label |
| `curLine` | Current line number |
| `run()` | Start/continue execution |
| `stop()` | Stop execution |
| `status` | Conductor status |
| `sleep(ms)` | Sleep for milliseconds |
| `wait(tag)` | Wait for tag completion |
| `addTag(tag)` | Add tag to queue |

---

### `SelectLayer` - Selection UI Layer

| Method | Description |
|--------|-------------|
| `addSelect(label, text)` | Add selection option |
| `clearButtons()` | Clear all buttons |
| `start()` | Start selection display |
| `onButtonClick(callback)` | Handle button clicks |
| `getSelectPositions()` | Get button positions |

---

---

## Event Hooks & Interception

### KAG Hooks (from custom.tjs)

**Tag Execution Hooks:**
```javascript
addKagHookCallback("onTag", function(tagName, params) {
    Debug.message("Tag: " + tagName);
    return true; // Continue execution
}, false); // false = after execution
```

**Scenario Hooks:**
```javascript
addKagHookCallback("onScenarioLoaded", function(storage) {
    Debug.message("Loaded: " + storage);
}, true); // true = before execution
```

**Message Color Hook:**
```javascript
addKagHookCallback("onGetCurrentMessageColor", function(elm, read) {
    // read = true for already read text
    if (read && SystemConfig.readedTextColorChange) {
        elm.color = SystemConfig.readedTextColor;
    }
}, false);
```

### System Hooks (from custom.ks)

Intercept game state changes:
| Hook Name | Trigger |
|-----------|---------|
| `exit.begin` | Game exit starts |
| `title.game` | Transition to game |
| `scenemode.view.init` | Scene mode view |
| `afterstory.start` | After story mode |
| `option.back` | Option menu back |

```javascript
// In TJS (custom.tjs style)
addKagHookCallback("onExitBegin", function() {
    Debug.message("Exit initiated");
}, false);
```

---

## Game Plugins (from custom.tjs)

### Core Plugins
| Plugin | Purpose |
|--------|---------|
| `speech.tjs` | Speech/voice processing |
| `voicematch.tjs` | Voice matching |
| `sysvoice.tjs` | System sounds |
| `multilang.tjs` | Multi-language support |
| `gesture.tjs` | Touch gestures |
| `emotion.tjs` | E-mote emotion system |
| `emotetalk.tjs` | E-mote lip sync |

### UI Plugins
| Plugin | Purpose |
|--------|---------|
| `msghack.tjs` | Message window hacks |
| `quickmenu.tjs` | Quick menu |
| `advancedmenu.tjs` | Advanced menu |
| `bgmtitleindicator.tjs` | BGM title display |

### Custom Classes
```javascript
// Custom message layer with voice replay
class MyCustomMessageLayer extends CustomMessageLayer {
    // Has voice replay, save buttons
    // Multi-language support
}

// Message frame changer
class MessageFrameChanger extends KAGPlugin {
    // Switch between message window types
}
```

### Utility Functions
```javascript
openWebPage(url, vo)           // Open web page
checkAdult()                    // Check adult content
checkDMMCloud()                 // Check DMM Cloud
PlayIntroVoice(tag)             // Play intro voice
HoldPsdFile(file)              // Load PSD file
```

---

## KAG Tags Reference

### Text Display
| Tag | Description |
|-----|-------------|
| `[l]` | Wait for click |
| `[s]` | Auto-wait (page break) |
| `[p]` | Paragraph (clear page) |
| `[r]` | Ruby text |

### Character Display
| Tag | Description |
|-----|-------------|
| `[ch name]` | Show character |
| `[ch name, time]` | Fade character in |
| `[ch name, -1]` | Hide character |

### Backgrounds & Layers
| Tag | Description |
|-----|-------------|
| `[bg storage, time]` | Change background |
| `[layopt layer, visible]` | Show/hide layer |
| `[move layer, ...]` | Move layer |

### Audio
| Tag | Description |
|-----|-------------|
| `[playbgm file]` | Play BGM |
| `[stopbgm]` | Stop BGM |
| `[playse file]` | Play sound effect |
| `[playvoice file]` | Play voice |

### Selection
| Tag | Description |
|-----|-------------|
| `[select]` | Start selection mode |
| `[seladd label, text]` | Add option |
| `[seldone]` | End selection |

###跳转
| Tag | Description |
|-----|-------------|
| `[jump target]` | Jump to label |
| `[call target]` | Call label (with return) |

---

## Character Names (from SystemConfig)

### Character List
Character names are stored in `SystemConfig.voiceNames`:
```javascript
// Original Japanese names (with ● prefix in game)
SystemConfig.voiceNames = [
    "●アトリ",      // Atri (protagonist)
    "●キャサリン",  // Catherine
    "●水菜萌",      // Mizuna Moe
    "●竜司",        // Ryuji
    "●凜々花",      // Ririka
    "●他男",        // Other male
    "●他女"         // Other female
];

// Voice type mapping (for audio files)
SystemConfig.SystemVoiceTypes = {
    "アトリ" => "アトリ",
    "キャサリン" => "キャサリン",
    "水菜萌" => "水菜萌",
    "竜司" => "竜司",
    "凜々花" => "凜々花"
};
```

### Getting Current Character
```javascript
// From conductor (original alias)
kag.conductor.getAliasName()

// From message layer
kag.messageLayer.current.name

// Check SystemConfig for display names
SystemConfig.voiceNames
```

### Localization Note
Russian/modified names are likely in `custom.tjs` or via `kag.scflags` language settings. Multi-language support exists for: jp, en, cn, tw.

---

## Game Settings (from SystemConfig)

### Speed Settings
| Property | Default | Description |
|-----------|---------|-------------|
| `kag.textspeed` | 80 | Text speed (100=instant, 0=slow) |
| `kag.autospeed` | 50 | Auto mode speed |
| `kag.skipspeed` | 0 | Skip speed |
| `kag.voicespeed` | 1 | Voice playback speed |
| `kag.drawspeed` | 1 | Game playback speed |

### Volume Settings
| Property | Default | Description |
|-----------|---------|-------------|
| `kag.bgmvolume` | 40 | Background music (0-100) |
| `kag.sevolume` | 60 | Sound effects (0-100) |
| `kag.voicevolume` | 100 | Voice (0-100) |
| `kag.wavevolume` | 100 | Wave/sfx (0-100) |
| `kag.bgv2volume` | 50 | Background voice (0-100) |

### Game State Flags
| Property | Default | Description |
|-----------|---------|-------------|
| `kag.bgmenable` | true | BGM enabled |
| `kag.seenable` | true | SE enabled |
| `kag.voiceenable` | true | Voice enabled |
| `kag.allskip` | false | Skip unread |
| `kag.noForceSkipUnread` | false | CTRL skip type |
| `kag.dramaticMode` | false | Dramatic mode |

---

## Event Hooks

### Tag Execution Hook
```javascript
kag.addHook("onTag", function(tagName, params) {
    Debug.message("Tag executed: " + tagName);
    return true; // Continue execution
});
```

### Scenario Load Hook
```javascript
kag.addHook("onScenarioLoaded", function(storage) {
    Debug.message("Loaded: " + storage);
});
```

### Label Jump Hook
```javascript
kag.addHook("onLabel", function(labelName) {
    Debug.message("Jumped to: " + labelName);
});
```

---

## Useful Code Snippets

### Check if Choice is Active
```javascript
typeof kag.selectLayer !== "undefined" && kag.selectLayer.visible
```

### Get Current Scenario
```javascript
kag.conductor.curStorage + ":" + kag.conductor.curLabel
```

### Pause Game
```javascript
kag.conductor.stop();
```

### Resume Game
```javascript
kag.conductor.run();
```

### Execute KAG Tag
```javascript
kag.addTag("playbgm", { storage: "theme.ogg" });
kag.addTag("wait", { canskip: true });
```

---

## Debug Commands for Console

```javascript
// List all kag methods
Scripts.getObjectKeys(kag)

// List conductor methods
Scripts.getObjectKeys(kag.conductor)

// Check if selection is active
typeof kag.selectLayer !== "undefined"

// Get current game position
kag.conductor.curStorage + " [" + kag.conductor.curLabel + "]@" + kag.conductor.curLine
```

---

## Neuro-sama Integration Points

1. **Choice Detection**: Monitor `kag.addSelect()` and `kag.selectLayer`
2. **Text Updates**: Use `kag.historyLayer` for dialogue history
3. **Game State**: Track `kag.conductor.status` and position
4. **Audio Events**: Hook `playbgm`, `playvoice` tags
5. **Character Changes**: Monitor `ch` tag execution

---

## File Paths (for reference)

- Game data: `D:\Games\ATRI-TestVersion\data.xp3`
- Extracted text: `D:\Games\ATRI-TestVersion\extract_dataxp3\main\`
- Russian patch: `D:\Games\ATRI-TestVersion\patch_ru\`
