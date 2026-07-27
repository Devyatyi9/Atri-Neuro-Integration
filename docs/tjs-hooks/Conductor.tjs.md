# Conductor.tjs — Script Conductor

Источник: байткод игры `system/Conductor.tjs` (20 108 bytes)
Сравнение: `docs/original-kag-tjs/data/kag3/Conductor.tjs` (12 672 bytes, source)

**Файл модифицирован:** игра добавила класс `OneShotAsyncTrigger` и ~20 функций.

**Декомпилятор tjs2dec:** все перечисленные хуки (`onTag`, `onStop`, `onScenarioLoad`, `onScenarioLoaded`, `onLabel`, `onJump`, `onCall`, `onReturn`, `onScript`) подтверждены в байткоде. См. `disasm/Conductor.tjs.txt`.

---

## Классы

### ConductorException extends Exception
Исключение conductor.

### OneShotAsyncTrigger extends AsyncTrigger *(только в игре)*
Одноразовый триггер. `cached = true`. Методы: `trigger()`, `cancel()`, `onFire()`, `assign(src)`.

### BaseConductor extends BaseConductorExtendClass (= KAGParser)
Базовый класс управления сценарием. `BaseConductorExtendClass` — динамический алиас на `KAGParser` (инициализируется в top-level коде: `if (typeof global.BaseConductorExtendClass == "undefined") global.BaseConductorExtendClass = KAGParser;`).

**Свойства:**
- `timer` — Timer
- `oneshot` — AsyncTrigger
- `_interrupted: bool` — флаг прерывания
- `timerEnabled: bool` — таймер активен
- `pendings: Array` — отложенные теги
- `fasttags: Array` — быстрые теги
- `nexttags: Array` — следующие теги
- `inProcessing: bool` — обработка timerCallback
- `reentered: bool` — реентерабельность
- `nextTimerTick: int` — следующий тик таймера
- `parseLine: int` — текущая строка парсинга
- `parseLineStr: string` — текущая строка (текст)
- `loadParam` — параметр загрузки сценария

**Методы:**

| Сигнатура | В исходнике |
|-----------|:---:|
| `clear()` | ✅ |
| `clearInfo()` | ❌ |
| `timerCallback()` | ✅ |
| `checkPending()` | ❌ |
| `checkNextTag()` | ❌ |
| `getNextParseTag()` | ❌ |
| `checkNextParseTag()` | ❌ |
| `onError(e)` | ❌ |
| `waitAndBreak()` | ❌ |
| `onTag(tag)` — вызов handler или onUnknownTag | ✅ |
| `onStop(tag)` — остановка выполнения | ✅ |
| `startProcess(immediate = false)` | ✅ |
| `start()` | ✅ |
| `stop()` | ✅ |
| `assign(src)` — копировать состояние | ✅ |
| `store()` — сохранить в Dictionary | ✅ |
| `restore(dic)` — восстановить из Dictionary | ✅ |
| `loadScenario(storage, label)` | ✅ |
| `goToLabel(label)` | ✅ |
| `enqueueTag(tag)` | ✅ |
| `enqueueFastTag(tag)` | ❌ |
| `enqueueNextTag(tag)` | ❌ |
| `pushTag(tag)` | ❌ |
| `pushFastTag(tag)` | ❌ |
| `pushNextTag(tag)` | ❌ |
| `addTag(tag)` | ❌ |
| `insertTag(tag)` | ❌ |
| `addFastTag(tag)` | ❌ |
| `insertFastTag(tag)` | ❌ |
| `addNextTag(tag)` | ❌ |
| `insertNextTag(tag)` | ❌ |

### Conductor extends BaseConductor

**Свойства:**
- `mStop`, `mRun`, `mWait` — константы статуса (0, 1, 2)
- `owner` — Object (KAGEnvironment, т.е. `kag`)
- `handlers` — Dictionary обработчиков тегов
- `status` — int (mStop=0, mRun=1, mWait=2), начальное `mStop`
- `timeOutTimer` — Timer таймаута
- `waitUntil` — Dictionary сигналов ожидания (`%[]`)
- `waitAll` — Dictionary всех сигналов (`%[]`)
- `lastTagName` — string, имя последнего тега (устанавливается в `onTag()`)
- `pendingTriggers` — Array отложенных триггеров
- `waitStartTick` — int, время начала таймаута
- `waitTimeout` — int, длительность таймаута
- `interrupted` — property (getter/setter), управляет `_interrupted` + timer/oneshot

**Методы:**

| Сигнатура | В исходнике |
|-----------|:---:|
| `Conductor(owner, handlers)` | ✅ |
| `run(immediate = false)` | ✅ |
| `sleep(ms)` | ✅ |
| `wait(until)` | ✅ |
| `waitWithTimeOut(until, timeout)` | ✅ |
| `cancelTimeOut()` | ❌ |
| `onTimeOut()` | ✅ |
| `flushPendingTriggers()` | ❌ |
| `trigger(name)` — отправить сигнал, возобновить выполнение | ✅ |
| `_trigger(name)` | ❌ |
| `onTag(tagname, tag)` — ищет handler, вызывает onUnknownTag | ✅ |
| `onStop(tag)` | ✅ |
| `onScript(script, scriptname, lineofs)` | ✅ |
| `store()` | ✅ |
| `restore(dic)` | ✅ |
| `onBeforeTag(tagname, tag)` — ПЕРЕД обработкой тега | ❌ |

---

## Event Hooks

Все функции Conductor делегируют вызов на `owner.onConductor*(...)`.

| Функция | Вызов | Когда |
|---------|-------|-------|
| `onScenarioLoad(loadParam)` | `owner.onConductorScenarioLoad(loadParam)` | Загрузка сценария началась |
| `onScenarioLoaded(loadParam)` | `owner.onConductorScenarioLoaded(loadParam)` | Сценарий загружен |
| `onLabel()` | `owner.onConductorLabel()` | Метка в сценарии |
| `onJump()` | `owner.onConductorJump()` | Jump-переход |
| `onCall()` | `owner.onConductorCall()` | Call-вызов |
| `onReturn()` | `owner.onConductorReturn()` | Return из call |
| `onAfterReturn()` | `owner.onConductorAfterReturn()` | После return |
| `onScript()` | `owner.onConductorScript()` | Выполнение скрипта |
| `onBeforeTag(tagname, tag)` | `owner.onConductorBeforeTag(tagname, tag)` | Перед любым тегом |
| `onUnknownTag(tagname, tag)` | `owner.onConductorUnknownTag(tagname, tag)` | Неизвестный тег |

### Использование для интеграции

```javascript
// В custom.tjs или через neuroExecFile:
kag.onConductorLabel = function(label, page) {
    Debug.message("Label: " + label);
};
kag.onConductorScenarioLoaded = function(storage, loadParam) {
    Debug.message("Loaded: " + storage);
};
```

**⚠️ `kag.onConductorBeforeTag` переопределять бессмысленно.** Он делегирует в `owner.onConductorBeforeTag()` → `kag.forEachFunctionHook("onBefore", ...)` — это C++ ncbind метод, недоступный для переопределения из TJS. Tag processing перехватывается только через ContextMonitor polling + `kag.conductor.lastTagName`.
