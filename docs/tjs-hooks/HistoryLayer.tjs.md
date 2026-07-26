# HistoryLayer.tjs — История диалогов (лог)

Источник: байткод игры `system/HistoryLayer.tjs`
Сравнение: `docs/original-kag-tjs/data/kag3/HistoryLayer.tjs` (source)

Слой для просмотра истории прочитанного текста.

---

## LButtonLayer

Кнопка в интерфейсе истории.

| Сигнатура | Описание |
|-----------|----------|
| `onClick()` | Обработка клика |
| `onExecute()` | Выполнение |

## HistoryLayer

Слой истории сообщений.

### Свойства

| Свойство | Тип | Описание |
|----------|-----|----------|
| `data` | Array | Массив записей истории |
| `data[i].name` | string | Внутреннее имя персонажа (夏生) |
| `data[i].disp` | string | Локализованный speaker label (Natsuki) |
| `data[i].text` | string | Локализованный текст диалога (English) |
| `data[i].plaintext` | string | Текст без разметки |
| `name` | string | ? |

### Методы

| Сигнатура | В исходнике |
|-----------|:---:|
| `clear()` | ✅ |
| `save(name, message)` — сохранить запись в историю | ✅ |
| `load()` — загрузить историю | ✅ |
| `clearBack()` — очистить фон | ❌ |
| `dispInit()` — инициализация отображения | ✅ |
| `dispUninit()` — деинициализация | ✅ |
| `drawLine()` — отрисовать строку истории | ❌ |
| `drawPage()` — отрисовать страницу | ✅ |
| `reline()` — перевод строки | ✅ |
| `repage()` — смена страницы | ✅ |
| `prevPage()` — предыдущая страница | ✅ |
| `nextPage()` — следующая страница | ✅ |
| `scrollUp()` / `scrollDown()` | ✅ |
| `hide()` — скрыть | ✅ |
| `setOptions(params)` | ✅ |
| `makeButtons()` — создать кнопки | ❌ |
| `store()` — сохранить состояние | ✅ |
| `onMouseDown(x, y, button)` | ✅ |
| `onMouseMove(x, y)` | ✅ |
| `onMouseLeave()` | ✅ |
| `onKeyDown(key)` | ✅ |
| `onKeyPress(key)` | ✅ |
| `onButtonClick(button)` | ✅ |
| `windowMouseWheel(delta)` | ✅ |
| `getLine(index)` — получить строку | ✅ |
| `getPage(index)` — получить страницу | ✅ |
| `getLineStart(index)` — начало строки | ✅ |
| `beginIndent()` / `endIndent()` | ✅ |
| `highlightAction()` | ❌ |
| `updateButtonState()` | ❌ |

### Свойства текущего состояния (Neuro)

| Свойство | Тип | Описание |
|----------|-----|----------|
| `currentName` | string | **Локализованный speaker label** (Natsuki) |
| `currentChara` | string | Внутреннее имя персонажа (夏生) |
| `currentText` | string | Текст оригинала (японский) |
| `currentLang` | string | Текущий язык отображения |
| `currentInfo` | object | Позиция сценария: `{file, label, line, lineStr}` |
| `data` | Array | Массив записей истории |
| `data[i].name` | string | Внутреннее имя (夏生) |
| `data[i].disp` | string | **Локализованный speaker label** (Natsuki) |
| `data[i].text` | string | **Локализованный текст** (English) |
| `data[i].plaintext` | string | Текст без разметки |

### Использование для Neuro

```javascript
// Локализованный speaker label (Natsuki)
kag.historyLayer.currentName

// Внутреннее имя персонажа (夏生)
kag.historyLayer.currentChara

// Текст оригинала (японский)
kag.historyLayer.currentText

// Локализованный текст диалога (английский)
(function(){ var d=kag.historyLayer.data; return d[d.count-1].text; })()

// Локализованный speaker label из data
(function(){ var d=kag.historyLayer.data; return d[d.count-1].disp; })()

// Полная запись: disp + text
(function(){ var d=kag.historyLayer.data; var l=d[d.count-1]; return l.disp + ': ' + l.text; })()

// Любая запись по индексу (0 = самая первая)
(function(){ var d=kag.historyLayer.data; return d[0].disp + ': ' + d[0].text; })()
```
