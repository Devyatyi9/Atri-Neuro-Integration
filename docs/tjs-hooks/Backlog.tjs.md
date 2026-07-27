# Backlog.tjs — Dialog History System

Источник: байткод игры `sysscn/backlog.tjs`

**Декомпилятор tjs2dec:** все классы и методы подтверждены в байткоде. `currentText`/`currentName`/`currentChara` — свойства `HistoryTextStore`. См. `disasm/backlog.tjs.txt`.

Содержит классы для хранения и отображения истории диалогов: `BacklogBase`, `DragScrollableBacklog`, `HistoryTextStore`.

---

## HistoryTextStore

Хранилище истории диалогов в памяти.

**Свойства (runtime):**
- `currentText` — string, текущий отображаемый текст
- `currentName` — string, текущее имя персонажа
- `currentChara` — string, ID персонажа (chara key)
- `freezePageFlg` — bool, заморозка страницы истории

**Методы:**
- `getHistoryCount()` → int — количество записей в истории
- `getHistoryData(idx)` → Dictionary — получить запись истории по индексу (содержит `text`, `name`, `chara`, `storage`, `label` и т.д.)
- `clearPageData()` — очистить текущую страницу
- `addHistory(disptext, name, chara)` — добавить запись (вызывается при каждом новом тексте)
- `store()` / `restore(dic)` — сериализация для сейвов

---

## BacklogBase

Базовый класс окна просмотра истории (экран "Просмотр истории").

**Свойства:**
- `_historyTextStore` — HistoryTextStore (инстанс)
- `_currentPage` — int, текущая страница
- `_maxPage` — int, всего страниц
- `_charaFaceMap` — Dictionary, лица персонажей

**Методы:**
- `show()` — открыть экран истории
- `hide()` — закрыть
- `prevPage()` / `nextPage()` — листание страниц
- `scrollUp(count)` / `scrollDown(count)` — скролл
- `jumpToPage(page)` — перейти на страницу
- `jumpToStorage(storage, label)` — прыгнуть к сцене из истории
- `reDraw()` — перерисовать
- `update()` — обновить

---

## DragScrollableBacklog extends BacklogBase

Как BacklogBase, но с drag-to-scroll. Добавляет:
- `onMouseDown`, `onMouseMove`, `onMouseUp` — drag scrolling
- Поддержка сенсорного ввода

---

## Использование для интеграции

```javascript
// Получение истории диалогов
var store = kag.backlogWindow._historyTextStore;
var count = store.getHistoryCount();

// Итерация по истории
for (var i = 0; i < count; i++) {
    var entry = store.getHistoryData(i);
    // entry.text — текст строки
    // entry.name — имя персонажа
    // entry.chara — ID персонажа
    // entry.storage — файл сценария
    Debug.message(entry.name + ": " + entry.text);
}

// Текущие значения (доступны всегда)
store.currentText  // последний отображённый текст
store.currentName  // последнее имя персонажа
store.currentChara // последний ID персонажа
```

> **Для Neuro:** `HistoryTextStore` даёт прямой программный доступ ко всей истории диалогов без перехвата хуков. Идеально для сбора контекста.
