# TJS команды для KiriKiri-Z

> **Однострочные команды** для использования в NeuroConsole
> **Многострочные скрипты** для исследования см. `docs/TJS-Scripts.md`

---

## Работа с файлами и VFS

### Чтение файлов из архива (короткий способ)

```js
// Получение содержимого текстового файла
(function() { var a = []; if(!a.load("main/Config.tjs")) return "FAIL"; return a.join("\n"); })()
```

### Чтение текстовых файлов (поток)

```tjs
// Получить поток для чтения
var stream = TVPCreateIStream("path/to/file.txt", TJS_BS_READ);

// Читать строку за строкой
var line;
while (!stream.EOS) {
    line = stream.ReadLine();
    // обработка строки
}

// Закрыть поток
stream.Destroy();
```

### Чтение бинарных файлов

```tjs
// Получить бинарный поток
var stream = TVPCreateIStream("path/to/file.bin", TJS_BS_READ);
var size = stream.Size;

// Читать байты
var buf = stream.Read(1024); // читать 1024 байта

stream.Destroy();
```

### Протоколы путей

- `storage://` — корень данных/архивов
- `appdata://` — папка настроек пользователя
- `file://` — физический файл на диске
- `archive://` — для xp3 архивов

### Проверка существования файлов

```js
Debug.message(Storages.isExistentStorage("main/Config.tjs"))
```

### Расположение файлов

```js
Debug.message(Storages.getPlacedPath("Config.tjs"))
```

---

## Работа с объектами игры

### Получить ключи объекта

```tjs
var keys = Scripts.getObjectKeys(kag);
for (var i = 0; i < keys.length; i++) {
    System.inform(keys[i]);
}
```

### Проверить существование свойства

```tjs
if ("conductor" in kag) {
    // свойство существует
}
```

### Получить значение свойства

```tjs
var value = kag.someProperty;
```

### Вызвать метод объекта

```tjs
kag.someMethod(param1, param2);
```

### Дамп объектов

```js
Debug.message(Scripts.dump(kag));  // создаёт текстовый файл с дампом объекта kag
```

### Безопасная проверка существования функции

```javascript
(function(){ try { return kag.storageName; } catch(e) { return "Error: " + e.message; } })()
```

### История диалогов

```javascript
// Получение последней записи в истории
(function(){ try { return kag.historyLayer.data[kag.historyLayer.data.count-1]; } catch(e) { return "History unavailable"; } })()
```

### Проверка KAG готовности (из C++)

```cpp
TVPExecuteExpression(TJS_W("typeof global.kag != 'undefined'"), &result);
```

---

## Исследование объектов

### Безопасное перечисление ключей объекта

```javascript
(function(){ var k = Scripts.getObjectKeys(objectName); k.sort(); return k.join(", "); })()
```

### Перечисление ключей kag.fore

```javascript
(function(){ var k = Scripts.getObjectKeys(kag.fore); k.sort(); return k.join(", "); })()
```

### Перечисление ключей kag.back

```javascript
(function(){ var k = Scripts.getObjectKeys(kag.back); k.sort(); return k.join(", "); })()
```

### Поиск активных выборов

```javascript
(function(){ var k = Scripts.getObjectKeys(kag); k.sort(); return k.filter(function(key){ return key.toLowerCase().indexOf('select')>=0; }).join(", "); })()
```

### Проверка kag.selectList (альтернативное имя)

```javascript
(function(){ try { var k = Scripts.getObjectKeys(kag.selectList); return "selectList found: " + k.join(", "); } catch(e) { return "selectList not found: " + e.message; } })()
```

### Поиск объектов с 'select' в названии

```javascript
(function(){ var keys = Scripts.getObjectKeys(kag); var selects = []; for(var i=0;i<keys.length;i++){ if(keys[i].toLowerCase().indexOf('select')!=-1){ selects.push(keys[i]); } } return selects.join(", "); })()
```

### Проверка текущего состояния игры

```javascript
(function(){ var keys = Scripts.getObjectKeys(kag); var status = []; for(var i=0;i<keys.length;i++){ var key = keys[i]; if(typeof kag[key]==='object' && kag[key]!==null){ var subkeys = Scripts.getObjectKeys(kag[key]); if(subkeys.length>0 && subkeys.length<10){ status.push(key+": ["+subkeys.join(", ")+"]"); } } } return status.slice(0,5).join("\n"); })()
```

---

## Полезные объекты

### kag - KAG система

```tjs
kag.conductor      // Кондуктор для управления воспроизведением
kag.tagHandlers    // Обработчики тегов
kag.layers        // Слои
kag.fore          // Передний слой
kag.back          // Задний слой
```

### global - глобальные переменные

```tjs
global.f           // Temporary flags (время жизни сцены)
global.tf          // Temporary flags (время жизни игры)
global.sf          // System flags (постоянные)
```

### System - системные функции

```tjs
System.version     // Версия TJS
System.userAgent   // Информация о системе
```

---

## Отладка

### Вывод в консоль

```tjs
Debug.message("текст");      // Обычное сообщение
Debug.notice("текст");       // Уведомление
Debug.warning("текст");      // Предупреждение
Debug.error("текст");       // Ошибка
```

### Проверка типов

```tjs
typeof variable     // "string", "number", "object", "function", "undefined"
```
