# MessageLayer.tjs — Message/Text Display Layer

Источник: байткод игры `system/MessageLayer.tjs`
Сравнение: `docs/original-kag-tjs/data/kag3/MessageLayer.tjs` (source)

---

## Подклассы

- `LinkButtonLayer` — кнопка-ссылка в тексте
- `LinkEditLayer` — поле ввода
- `LinkSliderLayer` — слайдер
- `SystemButtonLayer` — системная кнопка
- `RelativeLayer` — слой с относительным позиционированием
- `NameLayer` — слой имени персонажа
- `FaceLayer` — слой лица персонажа
- `MessageLineLayer` — строка сообщения
- `MessageTextLayer` — текст сообщения
- `MessageLayer` — главный слой сообщений

---

## MessageLayer

### Свойства

| Свойство | Тип | Описание |
|----------|-----|----------|
| `name` | string | Имя текущего персонажа |
| `message` | string | Текущее сообщение |
| `text` | string | Текст сообщения |
| `nameLayer` | NameLayer | Подслой имени |
| `faceLayer` | FaceLayer | Подслой лица |
| `comp` | MessageLayer | Композит (для трансформаций) |
| `window` | MainWindow | Родительское окно |
| `visible` | bool | Видимость |
| `font` | Font | Текущий шрифт |
| `vertical` | bool | Вертикальный текст |
| `currentStorage` | string | Текущий storage |
| `links` | Array | Ссылки/кнопки |
| `selects` | Array | Кнопки выбора |

### Методы

#### Управление текстом

| Сигнатура | В исходнике |
|-----------|:---:|
| `clear()` — очистить текст и сбросить позицию | ✅ |
| `clear2()` — дополнительная очистка | ✅ |
| `clearLayer()` — очистить слой полностью | ✅ |
| `processCh(ch)` — отрисовать символ (typewriter) | ✅ |
| `drawTextToLayer()` — нарисовать текст на слой | ✅ |
| `redrawText()` — перерисовать текст | ✅ |
| `commit()` — завершить отрисовку ссылок | ✅ |
| `reline()` — перевод строки | ✅ |
| `changeLineSize(size)` | ✅ |
| `beginChDraw()` / `endChDraw()` / `preChDraw(ch)` | ✅ |

#### Имя персонажа

| Сигнатура | В исходнике |
|-----------|:---:|
| `processName(name)` — обработать имя (через nameLayer) | ✅ |
| `setNameImage(storage, default)` — установить изображение имени | ✅ |
| `getExistName()` — проверить наличие имени | ❌ |
| `initNameLayer()` | ✅ |
| `invalidateNameLayer()` | ✅ |
| `initFaceLayer()` | ✅ |
| `invalidateFaceLayer()` | ✅ |
| `swapFaceLayer()` — обменять слой лица | ✅ |

#### Клики и ввод

| Сигнатура | В исходнике |
|-----------|:---:|
| `onMouseDown(x, y, button)` | ✅ |
| `onMouseUp(x, y, button)` | ✅ |
| `onMouseMove(x, y)` | ✅ |
| `onMouseLeave()` | ✅ |
| `processClick()` — обработка клика | ✅ |
| `addClick(button)` — добавить ожидание клика | ❌ |
| `clearClick()` — очистить ожидание клика | ❌ |
| `hasClick()` — есть ли ожидающие клики | ❌ |
| `processReturn()` — обработка return | ❌ |

#### Время и авто

| Сигнатура | В исходнике |
|-----------|:---:|
| `setVisibleTime(time)` — время автоперехода | ✅ |
| `addTimeout(time, layer)` — добавить таймаут | ❌ |
| `stopTimeout()` | ❌ |
| `startTimeout()` | ❌ |
| `clearTimeout()` | ❌ |
| `timeoutHandler()` | ❌ |
| `fadeDone()` / `fadeHandler()` | ❌ |
| `setHiddenStateByUser(hidden)` | ✅ |

#### Стилизация

| Сигнатура | В исходнике |
|-----------|:---:|
| `setFont(elm)` — шрифт из params | ✅ |
| `setStyle(elm)` — стиль из params | ✅ |
| `setDefaultFont(src)` | ✅ |
| `setDefaultStyle(src)` | ✅ |
| `resetFont()` | ✅ |
| `resetStyle()` | ✅ |
| `resetLineSize()` | ✅ |

#### Сохранение/загрузка

| Сигнатура | В исходнике |
|-----------|:---:|
| `assign(src)` | ✅ |
| `store()` → Dictionary | ✅ |
| `restore(dic)` | ✅ |
| `exchangeInfo()` — обменять с comp | ✅ |

---

## NameLayer

Управляет отображением имени персонажа.

| Сигнатура | Описание |
|-----------|----------|
| `NameLayer()` | Конструктор |
| `processName(name)` — установить имя | ✅ |
| `processNameText(name)` — текстовое имя | ✅ |
| `processNameImage(storage)` — имя картинкой | ✅ |
| `show()` | ✅ |
| `hide()` | ✅ |
| `draw()` — отрисовка | ✅ |
| `setOption(params)` | ✅ |
| `assignImages()` | ✅ |
| `store()`, `restore()` | ✅ |

---

## Получение текущего диалога

```javascript
// Имя персонажа
kag.messageLayer.name

// Текст сообщения
kag.messageLayer.message

// Через conductors
kag.conductor.getAliasName()
```
