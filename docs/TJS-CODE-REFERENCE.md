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
    r.hooksActive = !!(global.__neuroHooksInstalled && global.__neuroHooksEnabled);
    r.lastTag = global.__neuroLastTag || '';
    r.lastScene = global.__neuroLastScene || '';
    r.pendingSend = !!(global.__neuroPendingSend);
    global.__neuroPendingSend = false;
    if (typeof kag !== 'undefined' && kag.backlogWindow && kag.backlogWindow._historyTextStore) {
      var store = kag.backlogWindow._historyTextStore;
      var data = store.data;
      var cnt = store.count;
      r.backlogCount = cnt;
      var entries = [];
      var start = cnt > 5 ? cnt - 5 : 0;
      for (var i = start; i < cnt; i++) {
        var e = data[i];
        entries.push({disp:e.disp,text:e.text,voice:e.voice,name:e.name});
      }
      r.recentEntries = entries;
    }
    var ml = (typeof kag !== 'undefined' && kag.messageLayer) ? kag.messageLayer : null;
    if (ml && ml.message) {
      r.currentText = ml.message;
      r.currentSpeaker = ml.name || '';
    } else if (typeof kag !== 'undefined' && kag.backlogWindow && kag.backlogWindow._historyTextStore) {
      var store = kag.backlogWindow._historyTextStore;
      if (store.count > 0) {
        var last = store.data[store.count - 1];
        r.currentText = last.text;
        r.currentSpeaker = last.disp;
      }
    } else {
      r.currentText = '';
      r.currentSpeaker = '';
    }
    if (typeof kag !== 'undefined' && kag.backlogWindow && kag.backlogWindow._historyTextStore) {
      var store = kag.backlogWindow._historyTextStore;
      if (store.count > 0) {
        var last = store.data[store.count - 1];
        r.currentVoice = last.voice;
        r.currentName = last.name;
        if (last.voice && last.name && typeof characterVoiceInfo !== 'undefined') {
          var cinfo = characterVoiceInfo[last.name];
          if (cinfo) r.voicePrefix = cinfo.file;
        }
      }
    }
    if (typeof characterVoiceInfo !== 'undefined') {
      var vm = {};
      for (var n in characterVoiceInfo) {
        var f = characterVoiceInfo[n].file;
        if (f) vm[f] = n;
      }
      r.voiceMap = vm;
    }
    r.currentScene = typeof kag !== 'undefined' ? kag.currentScene : '';
    r.currentPage = typeof kag !== 'undefined' ? kag.currentPage : 0;
    return System.toJSONString(r);
  } catch(e) {
    return '{"error":"' + (e && e.message ? e.message : 'unknown') + '"}';
  }
})()
```

## 2. `neuroBuildVoiceMap()` — карта префиксов озвучки

```javascript
(function(){
  try {
    if (typeof characterVoiceInfo === 'undefined') return '{}';
    var vm = {};
    for (var n in characterVoiceInfo) {
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
(function(){
  try {
    if (typeof kag === 'undefined') return 'kag not ready';
    if (global.__neuroHooksInstalled) return 'already installed';
    global.__neuroHooksEnabled = true;
    global.__neuroLastTag = '';
    global.__neuroLastScene = '';
    global.__neuroPendingSend = false;
    var relevant = {charaNew:1,charaDelete:1,playbgm:1,stopbgm:1,trans:1,playvoice:1,video:1,quake:1};
    kag.addHook('onTag', function(tag, params) {
      if (!global.__neuroHooksEnabled) return true;
      global.__neuroLastTag = tag;
      if (relevant[tag]) global.__neuroPendingSend = true;
      return true;
    });
    kag.addHook('onScenarioLoaded', function(storage) {
      if (!global.__neuroHooksEnabled) return;
      global.__neuroLastScene = storage;
      global.__neuroPendingSend = true;
    });
    kag.addHook('onLabel', function(label) {
      if (!global.__neuroHooksEnabled) return;
      global.__neuroPendingSend = true;
    });
    kag.addHook('onVoiceStart', function(name, file) {
      if (!global.__neuroHooksEnabled) return;
      global.__neuroPendingSend = true;
    });
    global.__neuroHooksInstalled = true;
    return 'hooks installed';
  } catch(e) { return 'error: ' + String(e); }
})()
```

## 4. `neuroStopHooks()` — отключение хуков

```javascript
(function(){
  global.__neuroHooksEnabled = false;
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
