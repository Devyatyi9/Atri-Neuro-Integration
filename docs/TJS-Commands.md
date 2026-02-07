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

### Поиск активных выборов (попытка 1)

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
