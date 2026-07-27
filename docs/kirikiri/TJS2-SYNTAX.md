# TJS2 Syntax Reference (vs JavaScript)

> Ключевые отличия TJS2 от JavaScript, выявленные в процессе разработки.
> Для плагинов KiriKiri-Z/KiriKiri2.

## Операторы

| Оператор | TJS2 | JS | Примечание |
|----------|------|-----|-----------|
| `===` / `!==` | есть | есть | **Работают в TJS2** (не путать с моими старыми коммитами) |
| `||` / `&&` | есть | есть | OK |
| `==` / `!=` | есть | есть | С приведением типов |
| `void` | **есть** как выражение | есть | `x === void`, `x != void` — **работает** (см. custom.tjs: `vo === void`) |
| `typeof` | есть | есть | Возвращает: `"undefined"`, `"string"`, `"number"`, `"boolean"`, `"object"`, `"userdata"` (как в JS). **Регистр lowercase!** |
| `in` | **нет** | есть | ❌ **Нет оператора `key in obj`**. ❌ **Нет `for...in`** (крашит). Используй `Scripts.getObjectKeys(obj)` + стандартный `for`. Для проверки: `typeof obj[key] != 'undefined'` |
| `+` (строки) | есть | есть | `'' + expr` для конвертации в строку |
| `,` (comma) | есть | есть | `(a, b)` — последнее значение |

## Типы

| TJS2 | JS | Примечание |
|------|-----|-----------|
| `void` (tvtVoid) | `undefined` | `typeof x == 'undefined'` или `x === void` для проверки |
| `null` | `null` | ❌ **НЕТ в TJS2**. Используй `void` вместо `null` |
| `Integer` | `number` | 32-bit integer |
| `Real` | `number` | double |
| `String` | `string` | ❌ `String(value)` не существует. Используй `'' + value` |
| `Object` | `object` | ❌ `System.toJSONString(obj)` не существует. JSON вручную |

## Выполнение кода

### `TVPExecuteExpression()`
**Принимает ОДНО выражение**, НЕ последовательность:

```tjs
// ✅ OK — одно выражение
(function(){ var x = 1; return x; })()

// ✅ OK — одно выражение (присваивание)
x = 5

// ❌ ОШИБКА — два выражения через ;
x = 5; (function(){ return x; })()
```

Если нужно несколько statement — оберни всё в IIFE.

### IIFE
Работает как в JS:
```tjs
(function() {
  try {
    // код
  } catch(e) {
    return 'error: ' + ('' + e);
  }
})()
```

### `try-catch`
Синтаксис как в JS:
```tjs
try {
  // код
} catch(e) {
  // e — объект исключения
  '' + e  // строковое представление (конкатенация)
}
```

## Встроенные функции

| Функция | Описание |
|---------|----------|
| `'' + expr` | Конвертация в строку (конкатенация) — ❌ `String(value)` не существует в TJS2 |
| `Integer(value)` | Конвертация в integer |
| `Real(value)` | Конвертация в real |
| `System.toJSONString(obj)` | ❌ **не существует** в ATRI. Для JSON: собирай вручную через `'{\"key\":' + val + '}'` или через C++ плагин |

## Объекты (Dictionary)

В TJS2 объекты создаются через `%[]`, **не через `{}`** (фигурные скобки — это блок, не объект):

```tjs
// Создание
var obj = %[];                    // пустой dictionary
var obj = %[ key1: value1, key2: value2 ];  // с ключами
var obj = %[ 'key' => value ];    // альтернативный синтаксис

// Доступ
obj.key
obj["key"]
obj[expr]                         // динамический ключ (работает)

// Проверка существования ключа
if (typeof obj[key] != 'undefined')    // ✅ OK — typeof безопасен
// if (key in obj)                // ❌ НЕТ такого оператора в TJS2!

// Итерация — используем стандартный for + Scripts.getObjectKeys()
var keys = Scripts.getObjectKeys(obj);
for (var i = 0; i < keys.count; i++) {
  var k = keys[i];
  // obj[k] — доступ к значению
}
```

## Массивы

```tjs
var arr = [];
arr[0] = value;
arr[arr.length] = value;  // добавление в конец
arr[arr.count] = value;   // count и length — синонимы

// Итерация
for (var i = 0; i < arr.count; i++)
```

## KAG Runtime API (подтверждено в игре)

```tjs
// Готовность движка
kag                    // KAG engine
kag.languageType       // 0=JP, 1=EN, 2=CN
kag.getCurrentInfo()   // {file, label, line, lineStr}

// Диалоги
kag.historyLayer.currentName   // локализованный speaker label
kag.historyLayer.currentChara  // внутреннее имя (яп.)
kag.historyLayer.currentText   // японский текст
kag.historyLayer.data          // массив {disp, name, text, plaintext}
kag.historyLayer.data[i].voice // void (нет голоса) или Object (есть)

// ❌ kag.addHook НЕ СУЩЕСТВУЕТ (см. Hook-Mechanisms.md)
// В KAGParserEx события вызываются через прямые методы kag:
kag.onLabel = function(label) { ... };            // ✅
kag.onScenarioLoaded = function(storage) { ... };  // ✅
// onTag и onVoiceStart НЕ поддерживаются движком

// Конфигурация
CurrentLanguageTag      // 'jp' | 'en' | 'cn'
characterVoiceInfo      // { name: { file: prefix } }
```

## import / include

```tjs
// В TJS нет import как в JS.
// Файлы подключаются через KAG: [iscript] или загрузка движком.

// В XP3-архивах файлы ищутся автоматически.
```

## Важные отличия от JS

1. **`void` — валидное выражение** (вопреки ранним записям). `x === void`, `x != void` — работают (подтверждено кодом игры)
2. ❌ **`String()` не существует** — используй `'' + value` для конвертации в строку
3. **`TVPExecuteExpression()`** — принимает только одно выражение. Множественные statement через IIFE
4. **Глобальные переменные** — доступны через `global.xxx`. Чтение несуществующего свойства глобал — **исключение** (Member does not exist)
5. **Нет `undefined`** и **нет `null`** — только `void` (и тип, и значение "пустота")
6. **Доступ к несуществующему свойству — исключение**, а не `undefined`:
   ```tjs
   // ❌ ОШИБКА: Member "foo" does not exist
   if (obj.foo) { ... }

   // ✅ OK — typeof безопасен для несуществующих свойств
   if (typeof obj.foo != 'undefined') { ... }

   // ✅ OK — сравнение с void (только для свойств, которые существуют)
   if (obj.foo === void) { ... }
   ```
7. **`{}` — это блок, не объект**. Для создания объекта используй `%[]`:
   ```tjs
   // ❌ ОШИБКА: var obj = {}; — Syntax error
   // ✅ OK: var obj = %[];
   ```
8. **Нет оператора `in`** и **нет `for...in`**. `key in obj` — **Syntax error**. `for (var k in obj)` — **Syntax error** (или краш). Подтверждено грамматикой `tjs.y`: токен `T_IN` определён, но не используется ни в одном правиле. Используй `Scripts.getObjectKeys(obj)` + стандартный `for`:
   ```tjs
   var keys = Scripts.getObjectKeys(obj);
   for (var i = 0; i < keys.count; i++) {
     var k = keys[i];
   }
   ```
   Для проверки существования ключа: `typeof obj[key] != 'undefined'`.
9. **`obj[expr]++` работает** — инкремент через динамический ключ валиден.
10. **Присваивание без `var`** внутри функции — ошибка `Member "x" does not exist`, если переменная не объявлена. Используй `global.x = value` для глобальных:
    ```tjs
    // ❌ ОШИБКА внутри функции:
    x = 1;  // Member "x" does not exist

    // ✅ OK:
    var x = 1;     // локальная
    global.x = 1;  // глобальная
    ```
11. **RegExp поддерживается** (вопреки ранним записям). Движок ATRI собран с поддержкой регулярных выражений:
    ```tjs
    // ✅ Регулярные выражения работают
    /[\\]/g.match("test\\path");
    "hello world".replace(/world/, "there");
    /[a-z]+/.exec("abc123");
    ```
    **Важно:** tjscheck собран с `TJS_NO_REGEXP` и не проверяет regex-синтаксис. Для валидации regex-кода используй tjscheck без этого флага или проверяй в игре. Сама игра regex поддерживает.
12. **`tf` (kag.tflags) не сохраняется** в файлы сейвов. Это runtime-only словарь. Для персистентности используй `sf` (kag.sflags) — он сохраняется целиком — или добавляй поля в `flags` (kag.flags = `f`) через хуки `onStore`/`onRestore`.
