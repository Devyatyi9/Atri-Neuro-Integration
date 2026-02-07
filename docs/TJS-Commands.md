# TJS команды для KiriKiri-Z

> **Однострочные команды** для использования в NeuroConsole
> **Многострочные скрипты** для исследования см. `docs/TJS-Scripts.md`

## Работа с файлами и VFS

### Чтение файлов из архива

```js
// Получение содержимого текстового файла
(function() { var a = []; if(!a.load("main/Config.tjs")) return "FAIL"; return a.join("\n"); })()
```

### Протоколы путей

- `storage://` — корень данных/архивов
- `appdata://` — папка настроек пользователя  
- `file://` — физический файл на диске
- `archive://` — для xp3 архивов

### Проверка существования файлов

```
Debug.message(Storages.isExistentStorage("main/Config.tjs"))
```

### Расположение файлов

```
Debug.message(Storages.getPlacedPath("Config.tjs"))
```

## Работа с объектами игры

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

### Проверка KAG готовности

```cpp
TVPExecuteExpression(TJS_W("typeof global.kag != 'undefined'"), &result);
```

## Полезные команды

### Отладка

```
Debug.message("Сообщение");  // вывод в консоль отладки
```
