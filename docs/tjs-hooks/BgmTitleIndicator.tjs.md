# BgmTitleIndicator.tjs — BGM Title On-Screen Display

Источник: байткод игры `sysscn/bgmtitleindicator.tjs`

**Подтверждено декомпилятором tjs2dec.** См. `disasm/bgmtitleindicator.tjs.txt`.

Показывает название текущей BGM-композиции на экране при смене трека.

---

## Классы

### BgmTitleIndicatorBase

Базовый индикатор названия BGM.

**Свойства:**
- `visible` — bool, видимость
- `alpha` — int (0-255), прозрачность
- `_autoHideTimer` — Timer для авто-скрытия
- `_fadeTimer` — Timer для fade-анимации
- `_moveTimer` — Timer для move-анимации
- `_currentTitle` — string, текущее отображаемое название

**Методы:**
- `show(title)` — показать с заголовком
- `hide()` — скрыть
- `setTitle(title)` — установить заголовок
- `move(x, y)` — переместить
- `fadeIn(time)` / `fadeOut(time)` — анимации появления/исчезания
- `startAutoHide(ms)` — авто-скрытие через ms миллисекунд
- `stopAutoHide()` — отменить авто-скрытие

**Хуки:**
- `onMusicState(state, title)` — **вызывается при смене BGM** (state: "play"/"stop"/"change")

### SimpleIndicatorLayer

Визуальный слой индикатора (текст + фон).

### OptionBgmTitleIndicatorModule extends BgmTitleIndicatorBase

Модуль с дополнительными опциями конфигурации.

---

## Использование для интеграции

```javascript
// Доступ к индикатору
var indicator = kag.BgmTitleIndicator;

// Перехват смены трека
indicator.onMusicState = function(state, title) {
    Debug.message("BGM " + state + ": " + title);
};

// Принудительно показать трек
indicator.show("bgm/theme.ogg");
```

> **Для Neuro:** `onMusicState` даёт альтернативный (не через BGM.tjs) способ отслеживания текущего трека.
> Полезно когда BGM.tjs напрямую недоступен или заблокирован.
