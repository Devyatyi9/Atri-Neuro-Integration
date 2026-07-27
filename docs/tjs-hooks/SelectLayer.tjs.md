# SelectLayer.tjs — Choice Selection Layer

Источник: байткод игры `system/SelectLayer.tjs`

**Подтверждено декомпилятором tjs2dec.** См. `disasm/SelectLayer.tjs.txt`.
Исходник: только в KAGeXpress (`original-kag-tjs/data/KAGeXpress_Sample/`), в kag3 отсутствует.

---

## SelectButtonLayer

Кнопка выбора. Наследует `ButtonLayer`.

### Методы

| Сигнатура | Описание |
|-----------|----------|
| `SelectButtonLayer(params)` | Конструктор |
| `drawTextForRender()` | Отрисовка текста |
| `drawMessage()` | Отрисовка сообщения |
| `updateRenderOption()` | Обновление опций рендера |
| `redraw()` | Перерисовка |
| `reloadButtonImage()` | Перезагрузка изображения |
| `createTextLayer()` | Создание слоя текста |
| `modifyOpacity()` | Изменение прозрачности |
| `onMouseEnter()` | При наведении |
| `onMouseLeave()` | При уходе |
| `onMouseDown(x, y, button)` | При клике |
| `onButtonClick()` | Вызывает `owner.onButtonClick(this)` |
| `onExecute()` | Выполнение (проверка nodeEnabled, mbLeft) |
| `onKeyDown()` | Клавиша |
| `onFocus()` | Фокус |
| `getButtonHotSpot()` | Область кнопки |
| `getSelectButtonLayer()` | Получение себя |

---

## SelectLayer

Главный слой выбора. Наследует `AnimKAGLayer`.

### Свойства

| Свойство | Тип | Описание |
|----------|-----|----------|
| `selects` | Array | Массив кнопок выбора |
| `visible` | bool | Видимость |
| `window` | MainWindow | Родительское окно |
| `closese` | string | Звук закрытия |
| `closesebuf` | string | Буфер звука закрытия |
| `kag` | Object | Ссылка на `kag` |

### Методы

| Сигнатура | Описание |
|-----------|----------|
| `SelectLayer()` | Конструктор |
| `setOptions(params)` | Установить опции слоя |
| `getUIName()` → string | Имя UI |
| `getUIInfo()` | Информация UI |
| `createFontColorList()` | Список цветов шрифта |
| `getFontColor()` | Получить цвет шрифта |
| `drawTextToLayer()` | Отрисовка текста |
| `drawText()` | Отрисовка |
| `setInnerProps(key, val)` | Установить внутренние свойства |
| `setInnerOpacity(opacity)` | Установить прозрачность |
| `setInnerVisible(visible)` | Установить видимость |
| `assign(src)` | Копировать состояние |
| `beginAction()` | Начать анимацию |
| `stopAction()` | Остановить анимацию |
| `doneActFadeIn()` | Завершить fade-in |
| `setVisibleTime(time)` | Время автозакрытия |
| `clearButtons()` | Очистить все кнопки |
| `redrawButtons()` | Перерисовать кнопки |
| `addSelect(params)` | Добавить вариант |
| `getSelectButtonLayer(index)` | Получить кнопку |
| `getSelectPositions()` | Позиции кнопок |
| `start()` | Показать выбор |
| `end(selectInfo)` | Завершить выбор |
| `insertTag(selectInfo)` | Вставить тег выбора |
| `done()` | Закрыть (playSE, clearButtons) |
| `playSE(storage, buf)` | Проиграть звук |
| `onButtonEnter(button)` | Наведение на кнопку |
| `onButtonLeave(button)` | Уход с кнопки |
| `onButtonClick(button)` | **Клик по кнопке → вызывает `end()`** |
| `setHiddenStateByUser(hidden)` | Скрыть/показать |
| `lockFocus()` / `unlockFocus()` | Блокировка фокуса |
| `setSelProcessLock(lock)` | Блокировка обработки |

---

## Жизненный цикл выбора

```
addSelect(params) → создаёт SelectButtonLayer
  ↓
start() → показывает кнопки
  ↓
[игрок кликает]
  ↓
SelectButtonLayer.onButtonClick()
  → SelectLayer.onButtonClick(button)
    → если есть onclick → Scripts.eval(onclick)
    → end(selectInfo)
      → insertTag(selectInfo)
      → selectInfo = {tag, text, storage, target, language}
  ↓
done() → playSE(closese) → clearButtons() → visible = false
```

### Перехват выбора

```javascript
// Через подмену addSelect (как уже описано в GAME-MECHANICS.md):
var origAddSelect = kag.selectLayer.addSelect;
kag.selectLayer.addSelect = function(params) {
    Debug.message("Choice: " + params.text);
    return origAddSelect.call(this, params);
};

// Или через onButtonClick:
var origOnClick = kag.selectLayer.onButtonClick;
kag.selectLayer.onButtonClick = function(button) {
    Debug.message("Selected: " + button.text);
    return origOnClick.call(this, button);
};
```
