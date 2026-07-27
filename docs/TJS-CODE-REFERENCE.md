# TJS Inline Code Reference (из C++ функций)

> Черновик: TJS код, использовавшийся внутри C++ функций плагина.
> Вынесен для последующего переписывания на ncbind.

## 1. `neuroGetCurrentContext()` — сбор контекста игры

```javascript
(function(){
  try {
    var r = {};
    r.languageType = typeof kag !== 'undefined' ? kag.languageType : -1;
    r.languageTag = typeof CurrentLanguageTag !== 'undefined' ? CurrentLanguageTag : '';

    // Хуки (через флаги, без global.__)
    r.hooksActive = !!(_neuroHooksInstalled && _neuroHooksEnabled);
    r.lastTag = _neuroLastTag || '';
    r.lastScene = _neuroLastScene || '';
    r.pendingSend = !!_neuroPendingSend;
    _neuroPendingSend = false;

    // Текущий диалог через kag.historyLayer (runtime-подтверждено)
    if (typeof kag !== 'undefined' && kag.historyLayer) {
      var hl = kag.historyLayer;
      r.currentSpeaker = hl.currentName || '';     // локализованный speaker label
      r.currentChara   = hl.currentChara || '';    // внутреннее японское имя
      r.currentText    = hl.currentText || '';     // японский оригинал

      // Последние записи из data[] (локализованы)
      var d = hl.data;
      var cnt = d.count;
      r.backlogCount = cnt;
      var entries = [];
      var start = cnt > 5 ? cnt - 5 : 0;
      for (var i = start; i < cnt; i++) {
        var e = d[i];
        entries.push({disp:e.disp, text:e.text, name:e.name});
      }
      r.recentEntries = entries;

      // Последняя запись — локализованный текст
      if (cnt > 0) {
        var last = d[cnt - 1];
        r.localizedText = last.text;
        r.localizedSpeaker = last.disp;
        r.localizedName = last.name;

        // Voice-префикс
        if (last.name && typeof characterVoiceInfo !== 'undefined') {
          var cinfo = characterVoiceInfo[last.name];
          if (cinfo && cinfo.file) r.voicePrefix = cinfo.file;
        }
      }
    }

    // Карта voice prefix → имя персонажа
    if (typeof characterVoiceInfo !== 'undefined') {
      var vm = {};
      var names = Scripts.getObjectKeys(characterVoiceInfo);
      for (var i = 0; i < names.count; i++) {
        var n = names[i];
        var f = characterVoiceInfo[n].file;
        if (f) vm[f] = n;
      }
      r.voiceMap = vm;
    }

    // Сценарий
    var info = typeof kag !== 'undefined' ? kag.getCurrentInfo() : null;
    if (info) {
      r.currentScene = info.file;
      r.currentLabel = info.label;
      r.currentLine  = info.line;
    }

    return System.toJSONString(r);
  } catch(e) {
    return '{"error":"' + (('' + e).replace('"','\\"')) + '"}';
  }
})()
```

## 2. `neuroBuildVoiceMap()` — карта префиксов озвучки

```javascript
(function(){
  try {
    if (typeof characterVoiceInfo === 'undefined') return '{}';
    var vm = {};
    var names = Scripts.getObjectKeys(characterVoiceInfo);
    for (var i = 0; i < names.count; i++) {
      var n = names[i];
      var f = characterVoiceInfo[n].file;
      if (f) vm[f] = n;
    }
    return System.toJSONString(vm);
  } catch(e) {
    return '{}';
  }
})()
```

## 3. Система перехвата (Hooks)

**Важно: `kag.addHook()` не существует.** В ATRI нет универсального TJS-level hook механизма для tag processing. Работают следующие альтернативы:

### 3a. `window.addHook(hookName, callback)` — для событий с callHook()

Работает для строго определённых событий, где в коде есть явный `window.callHook()`:

| Событие | Вызывается в | Описание |
|---------|-------------|----------|
| `"onConductorStop"` | `MainWindow.onConductorStop()` | Остановка кондуктора |
| `"onConductorJump"` | `MainWindow.onConductorJump()` | Jump-переход |
| `"onConductorRestoreTarget"` | `MainWindow.onConductorRestoreTarget()` | Восстановление цели |
| `"onResetAll"` | — | Сброс всего |
| `"onCacheClear"` | — | Очистка кэша |
| `"onWindowResizeHook"` | — | Изменение размера окна |
| `"onModeChangeHook"` | — | Смена режима |
| `"onSELabel"` | AnimationLayer | Ожидание SE |
| `"onEnvEvent"` + имя | ч/з `forEachEventHook()` | События окружения |

```javascript
window.addHook("onConductorStop", function(arg0, arg1) {
    Debug.message("Conductor stopped: " + arg0);
});
```

### 3b. Override методов `kag.onConductor*()` — для label, scenario, beforeTag

Эти методы **реально существуют** и вызываются из Conductor (см. `Conductor.tjs`):

| Метод kag | Вызывается из | Статус |
|-----------|---------------|--------|
| `onConductorLabel(label, page)` | Conductor.onLabel() | ✅ Работает (есть в MainWindow) |
| `onConductorScenarioLoaded(storage, loadParam)` | Conductor.onScenarioLoaded() | ✅ Работает |
| `onConductorScenarioLoad(loadParam)` | Conductor.onScenarioLoad() | ✅ Работает |
| `onConductorBeforeTag(tagname, tag)` | Conductor.onBeforeTag() | ⚠️ **Уходит в C++ ncbind forEachFunctionHook — не переопределяется** |
| `onConductorUnknownTag(tagname, tag)` | Conductor.onUnknownTag() | ⚠️ **Аналогично — C++ side** |
| `onConductorStop()` | Conductor.onStop() | ✅ Работает |
| `onConductorJump()` | Conductor.onJump() | ✅ Работает |

```javascript
kag.onConductorLabel = function(label, page) {
    _neuroLastLabel = label;
    _neuroPendingSend = true;
};
kag.onConductorScenarioLoaded = function(storage, loadParam) {
    _neuroLastScene = storage;
    _neuroPendingSend = true;
};
```

### 3c. `window.forEachEventHook(eventName, ...)` — для событий окружения

Вызывается из `KAGEnvironment.onTagExtract()` и world-объектов:
- `"onEnvParseMsgOnOff"` — msgon/msgoff
- `"onEnvParseStartLine"` — начало строки
- `"onEnvParseStartText"` — начало текста
- `"onEnvParseSelect"` — выбор опции
- `"onScenarioLoaded"` — сценарий загружен
- `"onSceneLabel"` — метка сцены

**Но: `addEventHook()` не существует** — `forEachEventHook` читает из внутреннего C++ реестра (ncbind). Нельзя добавить свою подписку из TJS.

### 3d. ContextMonitor (C++ polling) — универсальный fallback

Единственный работающий способ для tag processing (см. `ContextMonitor.cpp`):
```cpp
// Вызывается каждые 200ms:
neuroExecFile("neuro_scripts/context_monitor.tjs", 1);
```

### Итог: что для чего использовать

| Цель | Механизм | Работает? |
|------|----------|:---------:|
| Отследить label | `kag.onConductorLabel = fn` | ✅ |
| Отследить сцену | `kag.onConductorScenarioLoaded = fn` | ✅ |
| Поймать tag | ContextMonitor polling | ✅ |
| Поймать voice start | `kag.voicePlayingRatio` (poll) | ✅ |
| Поймать BGM start | `kag.bgm.inPlaying` (poll) | ✅ |
| Перехватить обработку тега | — | ❌ |
| `kag.addHook("onTag")` | — | ❌ не существует |
| `kag.conductor.onTag = fn` | — | ❌ не hook, Dictionary lookup |

## 4. `neuroStopHooks()` — деактивация перехвата

```javascript
(function(){
  _neuroHooksEnabled = false;
  return 'hooks stopped';
})()
```

## 5. `NeuroShowConsole()` — показать Debug.console

```javascript
typeof Debug != 'undefined' && typeof Debug.console != 'undefined'
Debug.console.visible = true
```

## 6. `NeuroIsKAGReady()` — проверка готовности kag

```javascript
typeof global.kag != 'undefined'
```

## 7. История (runtime-подтверждённые команды)

```javascript
// Локализованный speaker label
kag.historyLayer.currentName

// Внутреннее имя персонажа
kag.historyLayer.currentChara

// Текст оригинала (японский)
kag.historyLayer.currentText

// Локализованный текст (английский)
(function(){ var d=kag.historyLayer.data; return d[d.count-1].text; })()

// Полная запись: disp: text
(function(){ var d=kag.historyLayer.data; var l=d[d.count-1]; return l.disp + ': ' + l.text; })()

// Структура data[i]: { disp, name, plaintext, text }
```

## 8. `neuroStartTagLogger([fileName])` — логгер KAG тегов

**⚠️ `kag.addHook("onTag")` не работает.** Реализация через ContextMonitor (C++ polling, см. ContextMonitor.cpp).
```javascript
// Логика логгера выполняется в context_monitor.tjs:
//   — опрашивает kag.conductor.lastTagName каждые 200ms
//   — собирает статистику тегов в _neuroTagLoggerStats
//   — пишет изменения в neuro_tag_log.txt через LogTagFile()
```

## 9. `neuroFlushTagLog()` — сброс буфера тегов в файл

```javascript
(function(){
  if (typeof _neuroTagLoggerEntries == 'void') return '[]';
  var e = System.toJSONString(_neuroTagLoggerEntries);
  _neuroTagLoggerEntries = [];
  return e;
})()
```

## 10. `neuroStopTagLogger()` — остановка и сброс

```javascript
(function(){
  _neuroTagLoggerRunning = false;
  return 'stopped';
})()
```
