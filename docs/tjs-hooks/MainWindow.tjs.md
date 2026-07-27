# MainWindow.tjs — Главное окно/Контейнер

Источник: байткод игры `system/MainWindow.tjs` (427 KB)

**Подтверждено декомпилятором tjs2dec.** См. `disasm/MainWindow.tjs.txt`.
Сравнение: `docs/original-kag-tjs/data/kag3/MainWindow.tjs` (source)

Главный класс-контейнер для всех слоёв и управления окном.

---

## DelayConductor

Управление отложенными тегами (задержки, анимации).

| Сигнатура | Описание |
|-----------|----------|
| `cancelDelay()` | Отменить задержку |
| `startDelay()` | Начать задержку |
| `extractDelay()` | Извлечь отложенные теги |
| `hasDelay()` → bool | Есть ли задержка |
| `callOwnerMethod()` | Вызвать метод владельца |
| `onError(e)` | Ошибка |
| `addTagExtractor(ext)` | Добавить экстрактор тегов |
| `removeTagExtractor(ext)` | Удалить экстрактор |
| `onTagExtract()` | Извлечение тега |
| `onScenarioLoaded()` | Сценарий загружен |

## MainConductor

Основной кондуктор. Наследует `DelayConductor`.

| Сигнатура | В исходнике |
|-----------|:---:|
| `MainConductor(owner)` | ✅ |
| `getAliasName()` → string | ✅ |
| `loadScenario(storage, label)` | ✅ |
| `loadCharacterVoiceInfo()` | ❌ |
| `getCharacterVoiceConfig()` | ❌ |
| `getCharacterVoicePer()` | ❌ |
| `getCharacterVoiceLevel()` | ❌ |
| `getCharacterTextColor()` | ❌ |
| `onLabel(label)` | ✅ |
| `onJump()` | ✅ |
| `onCall()` | ✅ |
| `onReturn()` | ✅ |
| `onAfterReturn()` | ✅ |
| `onScript(script, scriptname, lineofs)` | ✅ |
| `onCheckVoice()` | ❌ |
| `onRestoreLabelError()` | ❌ |
| `onLabelModifiedError()` | ❌ |
| `onScenarioModifiedError()` | ❌ |
| `eval(code)` | ✅ |
| `setLineModeOption(opt)` | ❌ |
| `checkPending()` / `checkNextTag()` / `checkNextParseTag()` | ✅ |

## KAGWindow

Главный класс окна.

**Свойства (из дампа global):**
- `kag` — ссылка на KAGEnvironment
- `conductor` — MainConductor
- `layers` — менеджер слоёв
- `messageLayer` — MessageLayer
- `selectLayer` — SelectLayer
- `bgm` — BGM
- `se` — SE
- `voice` — Voice
- `visible` — видимость
- `temporaryLayer` — временный слой

**Методы:**
| Сигнатура | Описание |
|-----------|----------|
| `captureScreen()` | Скриншот |
| `setPrimaryFocus(layer)` | Установить фокус |
| `resetPrimaryFocus()` | Сбросить фокус |
| `getPrimaryLayerAt(x, y)` | Слой в позиции |
| `local(x, y)` → позиция | Локальные координаты |
| `addTagExtractor(ext)` | Добавить экстрактор |
| `removeTagExtractor(ext)` | Удалить экстрактор |
| `eval(code)` | Выполнить код |
| `exec(func)` | Выполнить функцию |
| `getVoicePlayingScript()` → string | Сценарий голоса |
| `resetVoicePlayingRatio()` | Сброс пропорции голоса |

### Доступ к подсистемам

```javascript
// Через глобальный kag
kag.conductor       // MainConductor
kag.bgm             // BGM
kag.se              // SE  
kag.voice           // Voice
kag.messageLayer    // MessageLayer
kag.selectLayer     // SelectLayer
kag.systemLayer     // системный слой
kag.historyLayer    // HistoryLayer

// Глобальные переменные
kag.curStorage      // текущий сценарий
kag.curLabel        // текущая метка
kag.curLine         // текущая строка
kag.textspeed       // скорость текста
kag.autospeed       // скорость авто
```

## KAGMenuItem / PopupMenuWindow

Система меню. Обработка кликов по пунктам меню, всплывающие окна.
