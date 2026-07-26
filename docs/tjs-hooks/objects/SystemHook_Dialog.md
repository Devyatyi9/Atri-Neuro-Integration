# SystemHook, Dialog, Current, DelayConductor, TagQueue

Источник: дампы ключей `GameObjectsKeys/`

---

## SystemHook (SystemManager, SystemScript)

Система перехвата сценариев. Позволяет добавлять, выполнять и прыгать между сценариями. Состоит из трёх взаимосвязанных объектов:

- **SystemManager** — управление выполнением сценариев (exec, jump, call)
- **SystemScript** (SystemRegistory) — реестр загруженных сценариев
- **SystemHook** — агрегат, делегирующий на SystemManager и SystemScript

Подходит для hook'ов в поток выполнения игры: можно перехватывать `add`, `exec`, `jump`, `call`, отслеживать `currentState`/`lastState`.

### SystemHook

| property | type | description | neuro |
|----------|------|-------------|-------|
| `SystemManager` | Object | Делегат управления выполнением | Через него exec/jump/call |
| `SystemRegistory` | Object | Делегат реестра сценариев | Реестр загруженных сценариев |
| `_AliasItem_` | — | Внутреннее: элемент алиаса | — |
| `_JumpOrCall` | — | Внутреннее: переход/call | — |
| `_cached` | — | Внутреннее: кэш | — |
| `_exec` | — | Внутреннее: exec | — |
| `_jump` | — | Внутреннее: jump | — |
| `_map` | — | Внутреннее: карта | — |
| `_pending` | — | Внутреннее: ожидающие | — |
| `_searchParent` | — | Внутреннее: поиск родителя | — |
| `add` | Function | Добавить сценарий | Hook на добавление скрипта |
| `addCall` | Function | Добавить call-переход | Перехват вызова подпрограммы |
| `addJump` | Function | Добавить jump-переход | Перехват прыжка |
| `addJumpOrCall` | Function | Добавить jump/call | Перехват перехода |
| `alias` | Function | Работа с алиасами | Метаданные сценария |
| `clear` | Function | Очистить | Сброс состояния |
| `clearCache` | Function | Очистить кэш | Сброс кэша скриптов |
| `cobjs` | — | Объекты conductor? | — |
| `currentState` | Dictionary | Текущее состояние | Состояние на момент паузы |
| `disableClickable` | Function | Отключить кликабельность | Блокировка ввода |
| `erase` | Function | Удалить сценарий | Удаление из реестра |
| `exec` | Function | Выполнить сценарий | **Hook на exec** |
| `execDelay` | Function | Отложенный exec | **Hook на отложенный exec** |
| `get` | Function | Получить значение | Чтение из реестра |
| `getAlias` | Function | Получить алиас | Получить имя алиаса |
| `getArgs` | Function | Получить аргументы | Параметры сценария |
| `getJumpOrCall` | Function | Получить переход | Информация о переходе |
| `getValue` | Function | Получить значение | — |
| `isDefined` | Function → bool | Проверить определение | Проверка наличия |
| `jump` | Function | Jump-переход | **Hook на прыжок** |
| `kagAdd` | Function | Добавить через kag | Альтернативное добавление |
| `kagCurrent` | Function | Текущий kag | Доступ к KAGEnvironment |
| `kagExec` | Function | Exec через kag | **Hook на exec (kag)** |
| `kagJump` | Function | Jump через kag | **Hook на jump (kag)** |
| `lastState` | Dictionary | Предыдущее состояние | Состояние до последнего действия |
| `load` | Function | Загрузить сценарий | **Hook на загрузку** |
| `pending` | Function | Ожидающие операции | — |
| `search` | Function | Поиск в реестре | Поиск сценария |
| `set` | Function | Установить значение | Запись в реестр |
| `setCache` | Function | Установить кэш | — |
| `sorted` | — | Отсортированный список | — |
| `store` | Function | Сохранить состояние | Сериализация |
| `tagApply` | Function | Применить тег | Hook на применение тега |
| `tagNormalize` | Function | Нормализовать тег | Hook на нормализацию тега |

### SystemManager (управление выполнением)

| property | type | description | neuro |
|----------|------|-------------|-------|
| `_JumpOrCall` | — | Внутреннее | — |
| `_exec` | — | Внутреннее | — |
| `_jump` | — | Внутреннее | — |
| `add` | Function | Добавить операцию | Hook |
| `addCall` | Function | Добавить call | Hook на call |
| `addJump` | Function | Добавить jump | Hook на jump |
| `addJumpOrCall` | Function | Добавить jump/call | Hook |
| `disableClickable` | Function | Блокировка кликов | Блокировка ввода во время exec |
| `erase` | Function | Удалить | — |
| `exec` | Function | Выполнить сейчас | **Hook на немедленный exec** |
| `execDelay` | Function | Отложить exec | **Hook на отложенный exec** |
| `getArgs` | Function | Параметры | — |
| `getJumpOrCall` | Function | Инфо о переходе | — |
| `jump` | Function | Jump | **Hook на jump** |
| `kagAdd` | Function | Добавить через kag | — |
| `kagCurrent` | Function | Текущий kag | — |
| `kagExec` | Function | Exec через kag | **Hook на kag.exec** |
| `kagJump` | Function | Jump через kag | **Hook на kag.jump** |
| `pending` | Function | Ожидающие | — |
| `store` | Function | Сохранить | Сериализация |

### SystemScript / SystemRegistory (реестр сценариев)

| property | type | description | neuro |
|----------|------|-------------|-------|
| `_AliasItem_` | — | Элемент алиаса | — |
| `_cached` | — | Кэш | — |
| `_map` | — | Карта скриптов | — |
| `_searchParent` | — | Поиск родителя | — |
| `add` | Function | Добавить скрипт | Hook на регистрацию |
| `alias` | Function | Алиас скрипта | — |
| `clear` | Function | Очистить реестр | Сброс |
| `clearCache` | Function | Очистить кэш | — |
| `get` | Function | Получить скрипт | Чтение реестра |
| `getAlias` | Function | Алиас по имени | — |
| `getValue` | Function | Значение | — |
| `isDefined` | Function → bool | Проверить наличие | Проверка загружен ли скрипт |
| `kagAddScript` | Function | Добавить скрипт через kag | Hook |
| `load` | Function | Загрузить скрипт | **Hook на загрузку файла** |
| `script` | — | Сам скрипт | — |
| `search` | Function | Поиск | Поиск в реестре |
| `set` | Function | Установить | — |
| `setCache` | Function | Кэшировать | — |
| `tagApply` | Function | Применить тег | Hook |
| `tagNormalize` | Function | Нормализовать тег | Hook |

---

## Dialog / Current (DialogBox)

Система диалоговых окон (да/нет, OK, выбор). Один объект используется для разных типов диалогов.

> Для Neuro: полезно для перехвата диалогов подтверждения, отслеживания ответов игрока, автоматизации типовых диалогов (например, "загрузка без подтверждения").

| property | type | description | neuro |
|----------|------|-------------|-------|
| `_callDialogFunction` | — | Внутренний вызов функции диалога | — |
| `_entries` | Array | Записи диалога | Список кнопок/опций |
| `_getDialogPropertyString` | — | Получить строку свойств | — |
| `_now` | — | Текущее состояние | — |
| `_object` | Object | Внутренний объект | — |
| `action` | Function | Действие | **Hook на действие диалога** |
| `add` | Function | Добавить элемент | Hook на добавление варианта |
| `addClass` | Function | Добавить CSS-класс | — |
| `cmd` | Function | Команда | Hook на команду |
| `cmd_down` | Function | Команда вниз | Навигация |
| `cmd_func` | Function | Функция команды | — |
| `cmd_prop` | Function | Свойство команды | — |
| `cmd_up` | Function | Команда вверх | Навигация |
| `defaultLayerName` | string | Слой по умолчанию | — |
| `done` | Function | Завершено | **Hook на завершение диалога** |
| `dummyFunc` | Function | Фиктивная функция | Заглушка |
| `dummyProp` | — | Фиктивное свойство | Заглушка |
| `enter` | Function | Вход в диалог | **Hook на открытие диалога** |
| `func` | Function | Функция | — |
| `get` | Function | Получить значение | — |
| `hasDefined` | Function → bool | Проверить определено | — |
| `layerName` | string | Имя слоя | — |
| `leave` | Function | Выход из диалога | **Hook на закрытие диалога** |
| `nowent` | — | Текущий entry | — |
| `page` | — | Страница | — |
| `prop` | Function | Свойство | — |
| `propget` | Function | Получить свойство | — |
| `run` | Function | Запустить | **Hook на запуск диалога** |
| `select` | Function | Выбор | **Hook на выбор игрока** |
| `start` | Function | Старт | **Hook на старт диалога** |
| `storage` | string | Хранилище | Ресурс диалога |
| `uiEval` | Function | UI eval | — |
| `uiSort` | Function | UI сортировка | — |
| `uiload` | Function | UI загрузка | — |
| `valid` | bool | Валидность | — |

---

## DelayConductor

Управление отложенными тегами (задержки, анимации, тайминги). Используется в MainWindow и MainConductor.

> Для Neuro: позволяет отслеживать/отменять задержки, контролировать тайминг выполнения тегов.

| property | type | description | neuro |
|----------|------|-------------|-------|
| `addTagExtractor` | Function | Добавить экстрактор тегов | Hook на парсинг тега |
| `callOwnerMethod` | Function | Вызвать метод владельца | Вызов callback владельца |
| `cancelDelay` | Function | Отменить задержку | Отмена анимации/ожидания |
| `checkNextParseTag` | Function | Проверить следующий тег | Проверка очереди |
| `clearInfo` | Function | Очистить информацию | Сброс состояния |
| `extractDelay` | Function | Извлечь отложенные теги | Получение pending-тегов |
| `hasDelay` | Function → bool | Проверить наличие задержки | Ожидание завершения |
| `onError` | Function | Обработчик ошибки | Hook на ошибку |
| `onScenarioLoaded` | Function | Сценарий загружен | Hook на загрузку |
| `onTagExtract` | Function | Извлечение тега | Hook на извлечение |
| `removeTagExtractor` | Function | Удалить экстрактор | Отписка |
| `startDelay` | Function | Начать задержку | **Hook на старт задержки** |

---

## TagQueue

Очередь тегов. Используется для буферизации команд, ожидающих выполнения.

> Для Neuro: позволяет перехватывать и инспектировать теги до их обработки, добавлять свои теги в начало/конец очереди.

| property | type | description | neuro |
|----------|------|-------------|-------|
| `addNextTag` | Function | Добавить в начало очереди | Вставка приоритетного тега |
| `addTag` | Function | Добавить тег | Добавление в очередь |
| `clear` | Function | Очистить очередь | Сброс |
| `enqueueNextTag` | Function | Поставить в начало (enqueue) | Приоритетная вставка |
| `enqueueTag` | Function | Поставить в очередь | Стандартная вставка |
| `getNextTag` | Function → tag | Взять следующий тег | **Hook на чтение тега из очереди** |
| `hasData` | Function → bool | Есть ли данные | Проверка очереди |
| `store` | Function | Сохранить состояние | Сериализация |

---

## MyYesNoDialog (sysscn/MyYesNoDialog.tjs)

Кастомная система диалогов Yes/No для игры. Заменяет стандартный `kag.yesnobox` на стилизованные окна с анимацией.

### Классы

- **MyYesNoDialog** — главный управляющий класс
- **MyYesNoDialogWindow** — окно диалога
- **MyYesNoDialogLayer** — слой диалога
- **CursorMoveAction** — анимация курсора

### MyYesNoDialog

**Свойства (runtime):**
- `forceYesMap` — Dictionary карта `{storage_label: true}`, диалоги где принудительно выбирается "Да"
- `defaultFocusItem` — string, элемент фокуса по умолчанию
- `yesnoMap` — Dictionary кастомных маппингов

**Методы:**

| Сигнатура | Описание |
|-----------|----------|
| `askYesNo(params, callback, extra)` | Открыть диалог с кастомным колбэком |
| `forceYes()` | Принудительно выбрать "Да" |
| `openAskWindow(params)` | Открыть окно вопроса |
| `closeMsgWindow()` | Закрыть окно |
| `initYesNoMap()` | Инициализировать карту |
| `initForceYesMap()` | Инициализировать force-yes карту |
| `openAskTitle()` | Диалог "Выйти в титульный экран?" |
| `openAskExit()` | Диалог "Выйти из игры?" |
| `openAskLoad()` | Диалог "Загрузить сохранение?" |
| `openAskOverwrite()` | Диалог "Перезаписать сохранение?" |
| `openAskJump()` | Диалог "Перейти к сцене?" |
| `openAskMenu()` | Меню диалога |
| `getWindow()` → Window | Получить окно диалога |

### Перехват диалогов

Каждый диалог можно переопределить:

```javascript
// Принудительный "Да" для всех диалогов загрузки
kag.MyYesNoDialog.forceYesMap["*load*"] = true;

// Перехват конкретного диалога
kag.MyYesNoDialog.openAskLoad = function() {
    Debug.message("Load dialog triggered");
    return true; // автоматически подтвердить
};

// Кастомный колбэк
kag.MyYesNoDialog.askYesNo(
    {text: "Продолжить?", title: "Подтверждение"},
    function(result) {
        Debug.message("User chose: " + (result ? "Yes" : "No"));
    }
);
```

> **Для Neuro:** `forceYesMap` позволяет автоматизировать подтверждение диалогов без хуков.
> Каждый `openAsk*` метод можно переопределить для перехвата конкретного типа диалога.

---

## Примеры использования в интеграции

```javascript
// SystemHook — перехват exec
kag.system.hook.kagExec = function(storage, label) {
    Debug.message("exec: " + storage + " @ " + label);
};

// Dialog — перехват открытия/закрытия
kag.dialog.enter = function() {
    Debug.message("Dialog opened: " + this.storage);
};
kag.dialog.done = function(result) {
    Debug.message("Dialog closed: " + result);
};

// TagQueue — инспекция тегов
var origGetNext = kag.tagQueue.getNextTag;
kag.tagQueue.getNextTag = function() {
    var tag = origGetNext.call(this);
    Debug.message("Next tag in queue");
    return tag;
};
```
