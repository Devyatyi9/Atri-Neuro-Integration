# RenderMsgwin.tjs — Message Window Render Plugin

Источник: байткод игры `sysscn/rendermsgwin.tjs`

Плагин для кастомизации рендеринга окна сообщений. Перехватывает этапы отрисовки текста.

---

## RenderMsgwinPlugin

**Методы:**
- `RenderMsgwinPlugin()` — конструктор, регистрирует хуки в системе
- `hasWindow()` → bool — проверка наличия окна

**Регистрируемые хуки (onEraceMessage, onRedrawMessageText и т.д.):**

| Хук | Вызывается | Описание |
|-----|-----------|----------|
| `onEraceMessage` | При стирании сообщения | Можно перехватить момент очистки текста |
| `onRedrawMessageText` | При перерисовке текста | **Текст собирается к отрисовке** |
| `onStopMessage` | При остановке сообщения | Пользователь или скрипт остановил вывод |
| `onSkipNoDispChanged` | При изменении skip/no-disp | Режим пропуска изменился |

---

## Использование для интеграции

```javascript
// Перехват рендеринга текста
kag.renderMsgwinPlugin.onRedrawMessageText = function(str) {
    Debug.message("Rendering text: " + str);
};

// Перехват очистки текста
kag.renderMsgwinPlugin.onEraceMessage = function() {
    Debug.message("Message erased");
};
```

> **Для Neuro:** хуки `onRedrawMessageText` и `onEraceMessage` позволяют отслеживать когда текст появляется и исчезает на экране, без постоянного опроса `messageLayer.message`.
