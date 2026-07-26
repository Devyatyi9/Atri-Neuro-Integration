# MsgHack.tjs — Message Window Hacks

Источник: байткод игры `sysscn/msghack.tjs`

Кастомные эффекты для окна сообщений: fade-анимации символов, цвета, прорисовка.

---

## MsgHackChFadeManager

Управление fade-эффектами для символов текста.

**Свойства:**
- `canRedrawFrameAndText` — bool, можно ли перерисовать фрейм и текст
- `_layer` — Layer, целевой слой

**Методы:**
- `redrawMessageText()` — принудительная перерисовка текста
- `initMessageLayerProps()` — инициализация свойств слоя сообщения
- `CheckMsgHackTargetLayerId(layer)` — проверка целевого слоя

**Регистрируемые хуки:**

| Хук | Вызывается | Описание |
|-----|-----------|----------|
| `onCheckRedrawFrameAndText` | Проверка необходимости перерисовки | Управление флагом `canRedrawFrameAndText` |
| `onGetCurrentMessageColor` | Получение текущего цвета сообщения | **Перехват цвета текста** |
| `onInitMessageLayerProps` | Инициализация свойств слоя | Настройка параметров слоя |

---

## Использование для интеграции

```javascript
// Перехват цвета текста
kag.msgHackChFadeManager.onGetCurrentMessageColor = function(layer) {
    Debug.message("Message color requested for layer: " + layer);
};

// Принудительная перерисовка
kag.msgHackChFadeManager.redrawMessageText();
```

> **Для Neuro:** вторичный источник информации о состоянии сообщения. `onGetCurrentMessageColor` полезен если нужно определять персонажа по цвету текста.
