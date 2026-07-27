# Hook Mechanisms в ATRI (KiriKiri-Z)

> Полная карта: какие хуки существуют, какие работают, какие нет.

## 1. `kag.addHook()` — НЕ СУЩЕСТВУЕТ

`kag` (KAGEnvironment) не имеет метода `addHook()`. Все вызовы вида `kag.addHook("onTag", ...)` просто падают с `undefined is not a function`.

## 2. `window.addHook(hookName, callback)` — РАБОТАЕТ (MainWindow)

Реализован в `MainWindow.tjs` на `hookMap[hookName]` — массиве колбэков. Вызывается через `callHook()`.

```javascript
window.addHook("onConductorStop", function(arg0, arg1) {
    // arg1 = this.conductor
});
window.addHook("onConductorJump", function(arg0) {
    // arg0 — параметры jump
});
window.addHook("onConductorRestoreTarget", function() {
    // цель восстановления
});
```

**Полный список hookable событий:**

| Имя хука | Вызов | Источник |
|----------|-------|----------|
| `"onConductorStop"` | `callHook("onConductorStop", arg0, arg1)` | MainWindow.onConductorStop |
| `"onConductorJump"` | `callHook("onConductorJump", arg0)` | MainWindow.onConductorJump |
| `"onConductorRestoreTarget"` | `callHook("onConductorRestoreTarget")` | MainWindow.onConductorRestoreTarget |
| `"onResetAll"` | — | Разное |
| `"onCacheClear"` | — | Разное |
| `"onWindowResizeHook"` | — | Разное |
| `"onModeChangeHook"` | — | Menus |
| `"onSELabel"` | — | AnimationLayer |

## 3. Override `kag.onConductor*()` — РАБОТАЕТ (методы)

Conductor делегирует события через `owner.onConductor*()` → `kag.onConductor*()`.

**Что можно переопределить:**

| Метод | Сигнатура | Когда вызывается |
|-------|-----------|-----------------|
| `onConductorLabel` | `(label, page)` | Достигнута метка (Label tag) |
| `onConductorScenarioLoaded` | `(storage, loadParam)` | Сценарий загружен |
| `onConductorScenarioLoad` | `(loadParam)` | Загрузка сценария началась |
| `onConductorStop` | `(arg0, arg1)` | Остановка кондуктора |
| `onConductorJump` | `(arg0)` | Jump tag |
| `onConductorCall` | `(arg0)` | Call tag |
| `onConductorReturn` | `(arg0)` | Return tag |
| `onConductorAfterReturn` | `()` | После return |
| `onConductorScript` | `(script, name, line)` | iscript block |

**Что НЕЛЬЗЯ переопределить (C++ ncbind):**

| Метод | Уходит в |
|-------|----------|
| `onConductorBeforeTag` | `forEachFunctionHook("onBefore", ...)` — C++ ncbind |
| `onConductorUnknownTag` | `forEachFunctionHook("onUnknown", ...)` — C++ ncbind |

```javascript
// Пример: отслеживание label
kag.onConductorLabel = function(label, page) {
    Debug.message("Label reached: " + label);
};

// Пример: отслеживание загрузки сценария
kag.onConductorScenarioLoaded = function(storage, loadParam) {
    Debug.message("Scenario loaded: " + storage);
};
```

## 4. `forEachFunctionHook(hookName, ...)` — C++ ncbind (НЕДОСТУПЕН)

Вызывается из Conductor через `owner.onConductorBeforeTag()` → `kag.forEachFunctionHook("onBefore", ...)`.
Хранится и управляется C++ стороной через ncbind. Нельзя добавить/изменить из TJS.

```javascript
// В TJS-коде:
// kag.forEachFunctionHook("onBefore", tagName, tag)
// → internal C++ hookMap, недоступный TJS
```

## 5. `forEachEventHook(eventName, ...)` — C++ ncbind (НЕДОСТУПЕН)

Вызывается из KAGEnvironment.onTagExtract(). Читает из внутреннего C++ реестра.

```javascript
// Вызывается в:
forEachEventHook("onEnvParseMsgOnOff", ...)
forEachEventHook("onEnvParseStartLine", ...)
forEachEventHook("onEnvParseStartText", ...)
forEachEventHook("onEnvParseSelect", ...)
forEachEventHook("onScenarioLoaded", ...)
forEachEventHook("onSceneLabel", ...)
forEachEventHook("onPlayerJump", ...)
forEachEventHook("onEnvInit", ...)
```

## 6. SystemHook — kag.system.hook (РАБОТАЕТ)

```javascript
// Перехват exec
kag.system.hook.kagExec = function(storage, label) {
    Debug.message("exec: " + storage + " @ " + label);
};
```

**Доступные для перехвата:** `add`, `exec`, `execDelay`, `jump`, `kagExec`, `kagJump`, `load`, `addCall`, `addJump`.

## 7. Dialog overrides — РАБОТАЕТ

```javascript
// Dialog, Current — kag.dialog
kag.dialog.enter = function() {
    Debug.message("Dialog opened: " + this.storage);
};
kag.dialog.done = function(result) {
    Debug.message("Dialog closed: " + result);
};
```

**Доступные методы:** `enter`, `leave`, `action`, `cmd`, `done`, `run`, `select`, `start`.

## 8. TagQueue override — РАБОТАЕТ

```javascript
var origGetNext = kag.tagQueue.getNextTag;
kag.tagQueue.getNextTag = function() {
    var tag = origGetNext.call(this);
    Debug.message("Next tag: " + tag.tagname);
    return tag;
};
```

## 9. MyYesNoDialog — РАБОТАЕТ

```javascript
// Принудительный "Да" для всех диалогов
kag.MyYesNoDialog.forceYesMap["*load*"] = true;

// Переопределение конкретного диалога
kag.MyYesNoDialog.openAskLoad = function() {
    return true; // автоматически подтвердить
};
```

## 10. ContextMonitor (C++ polling) — ЕДИНСТВЕННЫЙ ДЛЯ TAG PROCESSING

```cpp
// ContextMonitor.cpp, каждые 200ms:
neuroExecFile("neuro_scripts/context_monitor.tjs", 1);
```

Отслеживает:
- `kag.conductor.lastTagName` — последний обработанный тег
- `kag.conductor.status` — состояние кондуктора
- `kag.historyLayer.currentText/Name/Chara` — диалог
- `kag.voicePlayingRatio` — голос
- `kag.bgm.playingStorage/inPlaying` — BGM

## Сводная таблица

| Механизм | Тип | Для чего | Работает? |
|----------|-----|----------|:---------:|
| `window.addHook()` | callback registry | conductor stop/jump/restore | ✅ |
| `kag.onConductorLabel = fn` | method override | label events | ✅ |
| `kag.onConductorScenarioLoaded = fn` | method override | scenario loaded | ✅ |
| `kag.system.hook.* = fn` | method override | exec/jump/call | ✅ |
| `kag.dialog.* = fn` | method override | dialog events | ✅ |
| `kag.tagQueue.* = fn` | method override | tag queue inspection | ✅ |
| `kag.MyYesNoDialog.* = fn` | method override | confirmation dialogs | ✅ |
| `kag.addHook()` | — | generic hooks | ❌ не существует |
| `onConductorBeforeTag` | C++ ncbind | pre-tag hook | ❌ переопределить нельзя |
| `forEachEventHook()` | C++ ncbind | game events | ❌ добавить нельзя |
| `kag.conductor.onTag = fn` | Dictionary lookup | tag handler | ❌ не hook |

**Вывод:** для получения уведомлений о тегах — только ContextMonitor polling. Для label/scenario/stop/jump — override методов. Для conductor stop/jump — `window.addHook`.
