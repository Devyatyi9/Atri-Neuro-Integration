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

## 3. `neuroStartHooks()` — установка хуков

```javascript
// Использует глобальные переменные для флагов:
//   _neuroHooksInstalled — установлены ли хуки
//   _neuroHooksEnabled   — активны ли хуки (для вкл/выкл без удаления)
//   _neuroPendingSend    — флаг "нужно отправить контекст"
//   _neuroLastTag        — последний тег
//   _neuroLastScene      — последняя загруженная сцена

(function(){
  try {
    if (typeof kag === 'undefined') { return 'kag not ready'; }
    if (_neuroHooksInstalled) { return 'already installed'; }
    _neuroHooksEnabled = true;
    _neuroLastTag = '';
    _neuroLastScene = '';
    _neuroPendingSend = false;

    kag.addHook('onTag', function(tag, params) {
      if (!_neuroHooksEnabled) { return true; }
      _neuroLastTag = tag;
      /* релевантные теги пока не определены */
      return true;
    });
    kag.addHook('onScenarioLoaded', function(storage) {
      if (!_neuroHooksEnabled) { return; }
      _neuroLastScene = storage;
      _neuroPendingSend = true;
    });
    kag.addHook('onLabel', function(label) {
      if (!_neuroHooksEnabled) { return; }
      _neuroPendingSend = true;
    });
    kag.addHook('onVoiceStart', function(name, file) {
      if (!_neuroHooksEnabled) { return; }
      _neuroPendingSend = true;
    });

    _neuroHooksInstalled = true;
    return 'hooks installed';
  } catch(e) {
    return 'error: ' + (e === null || e === void ? 'unknown' : e);
  }
})()
```

## 4. `neuroStopHooks()` — отключение хуков

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

```javascript
(function(){
  try {
    if (typeof kag === 'undefined') return 'kag not ready';
    if (_neuroTagLoggerRunning) return 'already running';
    _neuroTagLoggerStats = {};
    _neuroTagLoggerEntries = [];
    _neuroTagLoggerRunning = true;
    kag.addHook('onTag', function(tag, params) {
      if (!_neuroTagLoggerRunning) return true;
      var info = kag.getCurrentInfo();
      var pstr = '';
      if (params) {
        var pkeys = Scripts.getObjectKeys(params);
        for (var pi = 0; pi < pkeys.count; pi++) {
          var k = pkeys[pi];
          pstr += k + '=' + params[k] + ' ';
        }
      }
      var entry = tag + '@' + (info ? info.file : '?')
        + '.' + (info ? info.label : '?')
        + ':' + (info ? info.line : '?');
      if (pstr) entry += ' {' + pstr + '}';
      _neuroTagLoggerEntries[_neuroTagLoggerEntries.length] = entry;
      if (typeof _neuroTagLoggerStats[tag] != 'undefined')
        _neuroTagLoggerStats[tag]++;
      else
        _neuroTagLoggerStats[tag] = 1;
      return true;
    });
    return 'tag logger started, file=' + "neuro_tag_log.txt";
  } catch(e) {
    return 'error: ' + String(e);
  }
})()
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
