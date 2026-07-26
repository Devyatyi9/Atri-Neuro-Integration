# Menus.tjs — Система меню

Источник: байткод игры `system/Menus.tjs`
Сравнение: `docs/original-kag-tjs/data/kag3/Menus.tjs` (source)

---

## KAGMenuItem

Базовый пункт меню.

**Свойства** (из дампа):
- `caption` — string, название
- `enabled` — bool, активен
- `checked` — bool, отмечен

**Методы:**

| Сигнатура | В исходнике |
|-----------|:---:|
| `KAGMenuItem()` | ✅ |
| `click()` | ✅ |
| `onClick()` | ✅ |
| `createSoundExpression()` | ✅ |
| `removeSeparator()` | ✅ |
| `resetChildItemGrouping()` | ❌ |

## KAGAutoFlagMenuItem extends KAGMenuItem

Пункт меню с авто-флагом. Переключает флаги при клике.

| Сигнатура | В исходнике |
|-----------|:---:|
| `KAGAutoFlagMenuItem()` | ✅ |
| `setup()` | ❌ |
| `update()` | ❌ |
| `onUserClicked()` — пользователь кликнул | ❌ |
| `strictCompare()` / `normalCompare()` | ❌ |
| `getValueFromString()` / `setValueFromString()` | ❌ |
| `getValueFromProperty()` / `setValueFromProperty()` | ❌ |

## PopupMenuWindow

Всплывающее окно меню.

| Сигнатура | В исходнике |
|-----------|:---:|
| `onWindowsMessageHook(msg)` | ✅ |
| `close()` | ✅ |
| `onCloseQuery()` | ✅ |
| `copyMenu()` | ❌ |
| `onMouseLeave()` | ✅ |
| `onExitMenuLoop()` | ✅ |
| `superKeyDown(key)` | ❌ |
| `SetWindowControlMenu()` | ❌ |
