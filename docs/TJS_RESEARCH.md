# TJS Research - ATRI Game Objects

## Полезные команды для консоли

### Получение текущего диалога

```tjs
// Последняя строка из истории (имя + текст):
(function(){
    var d=kag.historyLayer.data;
    if(!d.count) return "Empty";
    var l=d[d.count-1];
    var n=l.name;
    return (n!="" && n!=void ? n+": " : "") + l.text;
})()

// Только текст:
(function(){ var d=kag.historyLayer.data; return d.count ? d[d.count-1].text : ""; })()

// Только имя персонажа:
(function(){ var d=kag.historyLayer.data; return d.count ? d[d.count-1].name : ""; })()
```

### Перебор свойств объекта

```tjs
// Список всех свойств:
(function(){ var a=[]; for(var k in kag.fore.messages[0].nameLayer) a.add(k); return a.join(", "); })()

// С типами и значениями:
(function(){
    var obj = kag.fore.messages[0].nameLayer;
    var props = [];
    for (var k in obj) {
        var v = obj[k];
        var t = typeof v;
        if (t == "Object") props.add(k + ": [Object]");
        else if (t == "String") props.add(k + ": \"" + v.substring(0,30) + "\"");
        else props.add(k + ": " + v);
    }
    return props.join("\n");
})()

// Дамп в файл:
Debug.message(Scripts.dump(kag.fore.messages[0].nameLayer))
```

### Чтение файлов из VFS

```tjs
// Проверка существования:
Storages.isExistentStorage("main/Config.tjs")

// Чтение текстового файла:
(function() { var a = []; if(!a.load("main/Config.tjs")) return "FAIL"; return a.join("\n"); })()
```

### Сохранение изображения слоя

```tjs
kag.fore.messages[0].nameLayer.saveLayerImage("D:/name_dump.bmp")
```

---

## Исследованные объекты

### kag (KAGWindow)

Главный объект игры.

| Свойство                         | Описание                              |
| -------------------------------- | ------------------------------------- |
| `kag.historyLayer`               | Слой истории диалогов                 |
| `kag.historyLayer.data`          | Массив записей истории                |
| `kag.historyLayer.data[i].name`  | Имя персонажа                         |
| `kag.historyLayer.data[i].text`  | Текст реплики                         |
| `kag.fore`                       | Передний план                         |
| `kag.fore.messages[0]`           | Основной слой сообщений               |
| `kag.fore.messages[0].nameLayer` | Слой с именем персонажа (графический) |
| `kag.conductor`                  | Дирижёр сценария                      |
| `kag.tagHandlers`                | Обработчики KAG тегов                 |
| `kag.sflags`                     | Системные флаги                       |

### Глобальные переменные

| Переменная | Описание                                          |
| ---------- | ------------------------------------------------- |
| `f`        | Флаги игры (game flags)                           |
| `sf`       | Системные переменные (сохраняются между сессиями) |
| `tf`       | Временные переменные                              |
| `kag`      | Главное окно KAG                                  |

### kag.fore.messages[0] (MessageLayer)

```tjs
kag.fore.messages[0].name
// Result: "表メッセージレイヤ0" (Front Message Layer 0)

kag.fore.messages[0].nameLayer.visible
// Result: 1 (если имя отображается)

kag.fore.messages[0].nameLayer.parent
// Result: (object ...) - родительский слой
```

**Примечание:** `nameLayer` это графический Layer. После отрисовки текста, сам текст как строка НЕ хранится в слое. Для получения имени используй `kag.historyLayer.data`.

---

### kag.conductor (Conductor)

Дирижёр сценария - управляет выполнением KAG скриптов.

**Ключевые свойства:**
| Свойство | Описание |
|----------|----------|
| `curStorage` | Текущий файл сценария |
| `curLabel` | Текущая метка |
| `curLine` | Текущая строка |
| `curLineStr` | Текст текущей строки |
| `curPos` | Позиция в строке |
| `status` | Статус (run/stop/wait) |
| `macros` | Определённые макросы |
| `mp` / `macroParams` | Параметры текущего макроса |

**Методы навигации:**
| Метод | Описание |
|-------|----------|
| `goToLabel(label)` | Перейти к метке |
| `callLabel(storage, label)` | Вызвать метку (с возвратом) |
| `loadScenario(storage)` | Загрузить сценарий |
| `run()` | Запустить выполнение |
| `stop()` | Остановить |
| `wait()` | Ожидание |
| `interrupt()` | Прервать |

**Методы тегов:**
| Метод | Описание |
|-------|----------|
| `addTag(tag)` | Добавить тег в очередь |
| `addNextTag(tag)` | Добавить следующий тег |
| `addFastTag(tag)` | Добавить быстрый тег |
| `getNextTag()` | Получить следующий тег |

**Callback-и:**
| Callback | Описание |
|----------|----------|
| `onTag` | При обработке тега |
| `onLabel` | При переходе к метке |
| `onJump` | При переходе |
| `onCall` | При вызове |
| `onReturn` | При возврате |
| `onStop` | При остановке |
| `onError` | При ошибке |
| `onScenarioLoad` | При загрузке сценария |

---

### kag.tagHandlers

Обработчики KAG тегов. Каждый ключ - это имя тега, значение - функция-обработчик.

**Управление текстом:**

- `ch` - вывод символа
- `r` - перенос строки
- `l` - ожидание клика (line break)
- `p` - ожидание клика (page break)
- `cm` - очистка сообщения
- `ct` - очистка текста
- `er` - очистка + ожидание
- `font`, `deffont`, `resetfont` - настройки шрифта
- `style`, `defstyle`, `resetstyle` - стили текста
- `ruby` - фуригана
- `indent`, `endindent` - отступы

**Выбор/ветвление:**

- `select`, `selinit`, `seladd`, `selopt`, `seldone` - выбор опций
- `mselect`, `mselinit`, `mseladd`, `mselopt` - map select
- `link`, `endlink` - ссылки
- `button` - кнопки
- `branch` - ветвление

**Переменные:**

- `eval` - выполнить TJS код
- `set` - установить переменную
- `clearvar` - очистить переменные
- `sflag` - системный флаг

**Звук:**

- `playbgm`, `stopbgm`, `fadebgm` - BGM
- `playse`, `stopse`, `fadese` - звуковые эффекты
- `playvoice`, `stopvoice`, `fadevoice` - голос

**Видео:**

- `playvideo`, `stopvideo`, `pausevideo`, `resumevideo`
- `openvideo`, `preparevideo`

**Слои/графика:**

- `layopt` - опции слоя
- `backlay`, `forelay` - задний/передний план
- `trans`, `stoptrans` - переходы
- `move`, `stopmove` - движение
- `quake`, `stopquake` - тряска

**Система:**

- `wait`, `delay` - ожидание
- `jump` (через `*`) - переход
- `call`, `return` - вызов/возврат
- `history`, `showhistory` - история
- `save`, `load` - сохранение/загрузка

---

## Практические примеры

### Получить текущую позицию в сценарии

```tjs
(function(){
    var c = kag.conductor;
    return "File: " + c.curStorage + "\nLabel: " + c.curLabel + "\nLine: " + c.curLine;
})()
```

### Перехват тегов через onTag

```tjs
// Сохраняем оригинальный callback
var origOnTag = kag.conductor.onTag;

// Устанавливаем свой
kag.conductor.onTag = function(tag, elm) {
    Debug.message("Tag: " + tag + " " + Scripts.dump(elm));
    // Вызываем оригинальный
    if (origOnTag !== void) origOnTag(tag, elm);
};
```

### Перехват [p] тега для получения диалога

```tjs
// Однострочник для консоли (с защитой от повторного хука):
(function(){
    if(global["oldP"] !== void) return "Already hooked";
    global["oldP"] = kag.tagHandlers["p"];
    kag.tagHandlers["p"] = function(p){
        var d = kag.historyLayer.data;
        if(d.count > 0) Debug.message("HOOK: " + d[d.count-1].name + ": " + d[d.count-1].text);
        return global["oldP"](p);
    };
    return "Success";
})()
```

Для интеграции с Neuro SDK:

```tjs
// Полная версия с вызовом C++ функции
kag.tagHandlers["p"] = function(p) {
    var d = kag.historyLayer.data;
    if(d.count > 0) {
        global.NeuroSDK_OnText(d[d.count-1].name, d[d.count-1].text);
    }
    return global.oldP(p);
};
```

### Перехват [dispname] тега

```tjs
// Проверить существует ли тег и посмотреть его код:
(function(){
    if(kag.tagHandlers["dispname"]) {
        Debug.message("DispName Code: " + kag.tagHandlers["dispname"]);
        return "Code dumped to console";
    }
    return "Tag not found";
})()

// Установить хук на dispname:
(function(){
    if(!kag.tagHandlers["dispname"]) return "No tag";
    if(global["oldDisp"] !== void) return "Already hooked";
    global.oldDisp = kag.tagHandlers["dispname"];
    kag.tagHandlers["dispname"] = function(p) {
        if(p.text !== void) Debug.message("DISPNAME: " + p.text);
        return global.oldDisp(p);
    };
    return "Hook installed";
})()
```

**Примечание:** `dispname` может содержать оригинальное имя до локализации.

### Программное продвижение диалога

```tjs
// Вариант 1: Имитация клика
kag.onPrimaryClick();

// Вариант 2: Через процесс (если есть ожидание)
kag.process("", "*next");

// Вариант 3: Продолжить conductor
kag.conductor.run();
```

### Проверка статуса conductor

```tjs
kag.conductor.status
// Возможные значения: "run", "stop", "wait"
```

### Получить список макросов

```tjs
(function(){
    var m = kag.conductor.macros;
    var names = [];
    for (var k in m) names.add(k);
    return names.join(", ");
})()
```

### Перехват выбора (selections)

```tjs
// Проверить есть ли активный выбор
kag.selects  // массив текущих опций выбора

// Выбрать опцию программно (если есть селекты)
kag.selectItem(0)  // выбрать первую опцию
```

---

## TODO: Исследовать

- [x] `kag.conductor` - дирижёр сценария (задокументировано)
- [x] `kag.tagHandlers` - обработчики тегов (задокументировано)
- [ ] Механизм выбора опций (selections) - частично
- [ ] Как перехватить момент появления нового диалога (через onTag?)
- [ ] Тестировать `kag.onPrimaryClick()` для продвижения

---

## Известные проблемы

### Syntax Error вызывает deadlock

При синтаксической ошибке в TJS движок может зависнуть из-за внутренней синхронизации (ждёт критическую секцию которая уже захвачена).

**Стек при зависании:**

```
RtlpWaitOnCriticalSection
RtlpEnterCriticalSectionContended
RtlEnterCriticalSection
ATRI-MyDearMoments-.exe
```

**Решение:** Оборачивать пользовательский ввод в try-catch на стороне TJS:

```tjs
(function(){ try { return eval(userCode); } catch(e) { return "Error: " + e.message; } })()
```

### Storages.getText() не существует

В этой версии KiriKiri (1.2.0.3) нет метода `Storages.getText()`. Используй `Array.load()`:

```tjs
var lines = [];
lines.load("path/to/file.txt");
var content = lines.join("\n");
```
