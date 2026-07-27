# Стратегия перехвата событий

> Какой механизм использовать для разных типов игровых событий.

## Принципы

1. **Override методов kag** — предпочтительный способ для событий label/scenario/stop
2. **`window.addHook`** — единственный способ для conductor jump/restoreTarget
3. **ContextMonitor (polling)** — fallback для всего, что нельзя перехватить иначе

## Матрица

| Событие              | Механизм                                 | Комментарий                       |
| -------------------- | ---------------------------------------- | --------------------------------- |
| Label достигнут      | `kag.onConductorLabel = fn`              | ✅ Простой override                |
| Сценарий загружен    | `kag.onConductorScenarioLoaded = fn`     | ✅ Простой override                |
| Conductor остановлен | `window.addHook("onConductorStop", fn)`  | ✅ Событие                         |
| Jump-переход         | `window.addHook("onConductorJump", fn)`  | ✅ Событие                         |
| Call/Return          | `kag.onConductorCall/Return = fn`        | ✅ Override, если нужно            |
| **Tag processing**   | **ContextMonitor polling**               | ❌ Нет TJS-level hook, только poll |
| Voice start/stop     | `kag.voicePlayingRatio` poll             | ✅ Runtime свойство                |
| BGM смена            | `kag.bgm.playingStorage` poll            | ✅ Runtime свойство                |
| Выбор опции          | Override `kag.selectLayer.onButtonClick` | ✅ Method override                 |
| Диалог (Yes/No)      | Override `kag.MyYesNoDialog.openAsk*`    | ✅ Method override                 |
| Системные диалоги    | Override `kag.dialog.enter/done`         | ✅ Method override                 |

## Реализация

### Инициализация (один раз при загрузке плагина)

```javascript
// --- Override методов kag ---
var origOnLabel = kag.onConductorLabel;
kag.onConductorLabel = function(label, page) {
    _neuroCurrentLabel = label;
    _neuroContextDirty = true;
    Debug.message("[Neuro] Label: " + label);
    if (origOnLabel) return origOnLabel.call(this, label, page);
};

var origOnScenarioLoaded = kag.onConductorScenarioLoaded;
kag.onConductorScenarioLoaded = function(storage, loadParam) {
    _neuroCurrentScene = storage;
    _neuroContextDirty = true;
    Debug.message("[Neuro] Scenario: " + storage);
    if (origOnScenarioLoaded) return origOnScenarioLoaded.call(this, storage, loadParam);
};

var origOnStop = kag.onConductorStop;
kag.onConductorStop = function(arg0, arg1) {
    _neuroConductorStopped = true;
    Debug.message("[Neuro] Conductor stopped");
    if (origOnStop) return origOnStop.call(this, arg0, arg1);
};

// --- window.addHook (для событий без метода) ---
window.addHook("onConductorJump", function(arg0) {
    Debug.message("[Neuro] Jump: " + arg0);
    return false; // false = не блокировать
});

// --- ContextMonitor (единственный способ для tag processing) ---
// В C++: neuroExecFile("neuro_scripts/context_monitor.tjs", 1) каждые 200ms
// В context_monitor.tjs:
//   - kag.conductor.lastTagName — текущий тег
//   - kag.conductor.status — состояние (0/1/2)
//   - kag.historyLayer.* — диалог
//   - kag.voicePlayingRatio — голос
//   - kag.bgm.* — BGM
```

### Когда что вызывается

```
Метка (label tag)
  → Conductor.onLabel()
    → kag.onConductorLabel()          ← наш override
    → forEachEventHook("onSceneLabel")

Тег (любой)
  → Conductor.onTag()
    → onBeforeTag()
      → kag.onConductorBeforeTag()
        → forEachFunctionHook("onBefore")  ← C++ ncbind, не перехватить
    → handlers[tagName](tag)
    → lastTagName = tagName

Сценарий загружен
  → Conductor.onScenarioLoaded()
    → kag.onConductorScenarioLoaded()   ← наш override

Conductor остановлен
  → Conductor.onStop()
    → kag.onConductorStop()
      → callHook("onConductorStop")    ← addHook сработает

Jump
  → Conductor.onJump()
    → kag.onConductorJump()
      → callHook("onConductorJump")    ← addHook сработает

Polling (200ms)
  → ContextMonitor cчитывает:
    → kag.conductor.lastTagName
    → kag.conductor.status
    → kag.historyLayer.currentText
    → kag.voicePlayingRatio
    → kag.bgm.playingStorage
```

## Сравнение

| Критерий            | Override                     | window.addHook        | Polling            |
| ------------------- |:----------------------------:|:---------------------:|:------------------:|
| Мгновенная реакция  | ✅                            | ✅                     | ❌ 200ms latency    |
| Неблокирующий       | ✅                            | ✅                     | ✅ (C++ thread)     |
| Все события         | ❌ только label/scenario/stop | ❌ только jump/restore | ✅ любые            |
| Простота реализации | ✅                            | ✅                     | ✅                  |
| Tag processing      | ❌                            | ❌                     | ✅ **единственный** |
