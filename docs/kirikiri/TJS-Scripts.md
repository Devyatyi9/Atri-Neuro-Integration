# TJS Скрипты для исследования

> **Важно:** Эти скрипты предназначены для Scripting Window (многострочный режим)
> Для однострочных команд используйте `docs/TJS-Commands.md`

## Безопасная итерация объектов

### Безопасный дамп свойств объекта

```tjs
(function(){
    var objName = "kag.fore";  // Изменить на нужный объект
    try {
        var obj = eval(objName);
        if(!obj) return objName + " not found";
        
        var props = [];
        for(var k in obj) {
            if(typeof k === "string") props.push(k);
        }
        return objName + " has " + props.length + " properties:\n" + props.join(", ");
    } catch(e) {
        return "Error: " + e.message;
    }
})()
```

### Проверка существования объекта

```tjs
(function(){
    var objNames = ["sf", "f", "kag.characterData", "System.characters"];
    var results = [];
    
    for(var i = 0; i < objNames.length; i++) {
        var name = objNames[i];
        try {
            var exists = typeof eval(name) !== "undefined";
            results.push(name + ": " + (exists ? "exists" : "not found"));
        } catch(e) {
            results.push(name + ": error - " + e.message);
        }
    }
    return results.join("\n");
})()
```

### Детальный анализ объекта с типами свойств

```tjs
(function(){
    var objName = "kag.conductor";  // Изменить на нужный объект
    try {
        var obj = eval(objName);
        if(!obj) return objName + " not found";
        
        var props = [];
        for(var k in obj) {
            var v = obj[k];
            var t = typeof v;
            
            if(t === "undefined") {
                props.push(k + ": undefined");
            } else if(t === "function") {
                props.push(k + "()");
            } else if(t === "object") {
                if(v === null) {
                    props.push(k + ": null");
                } else if(Array.isArray(v)) {
                    props.push(k + ": Array[" + v.length + "]");
                } else {
                    props.push(k + ": Object");
                }
            } else {
                var valStr = String(v);
                if(valStr.length > 50) valStr = valStr.substring(0, 47) + "...";
                props.push(k + ": " + t + " = " + valStr);
            }
        }
        return objName + " (" + props.length + " properties):\n" + props.join("\n");
    } catch(e) {
        return "Error: " + e.message;
    }
})()
```

## Исследование персонажей

### Поиск characterData в различных местах

```tjs
(function(){
    var searchPaths = [
        "kag.characterData",
        "kag.characters", 
        "System.characterData",
        "System.characters",
        "sf.characters",
        "f.characters"
    ];
    
    var results = [];
    for(var i = 0; i < searchPaths.length; i++) {
        var path = searchPaths[i];
        try {
            var obj = eval(path);
            if(typeof obj !== "undefined") {
                results.push(path + ": FOUND");
                
                // Если это объект, попробуем получить его ключи
                if(typeof obj === "object" && obj !== null) {
                    var keys = [];
                    for(var k in obj) keys.push(k);
                    if(keys.length > 0 && keys.length < 20) {
                        results.push("  Keys: " + keys.join(", "));
                    } else {
                        results.push("  Keys: " + keys.length + " items");
                    }
                }
            } else {
                results.push(path + ": not found");
            }
        } catch(e) {
            results.push(path + ": error - " + e.message);
        }
    }
    return results.join("\n");
})()
```

### Анализ системных флагов (безопасный)

```tjs
(function(){
    try {
        // Проверяем существует ли sf
        if(typeof sf === "undefined") return "sf not found";
        
        if(typeof sf !== "object" || sf === null) {
            return "sf is not an object, type: " + typeof sf;
        }
        
        var props = [];
        var count = 0;
        for(var k in sf) {
            if(count < 50) { // Ограничиваем вывод
                var v = sf[k];
                var t = typeof v;
                if(t === "string" && v.length > 30) {
                    props.push(k + ": \"" + v.substring(0, 27) + "...\"");
                } else if(t !== "function") {
                    props.push(k + ": " + t + " = " + String(v));
                }
                count++;
            }
        }
        
        var result = "sf has " + props.length + " accessible properties (showing first " + count + "):\n";
        return result + props.join("\n");
        
    } catch(e) {
        return "Error accessing sf: " + e.message;
    }
})()
```

## Исследование слоев

### Анализ слоев kag.fore и kag.back

```tjs
(function(){
    try {
        var results = [];
        
        // kag.fore анализ
        results.push("=== kag.fore ===");
        results.push("Type: " + typeof kag.fore);
        if(kag.fore && typeof kag.fore === "object") {
            if(kag.fore.messages) {
                results.push("Messages: " + kag.fore.messages.length + " layers");
                for(var i = 0; i < Math.min(3, kag.fore.messages.length); i++) {
                    var msg = kag.fore.messages[i];
                    if(msg) {
                        results.push("  Message[" + i + "]: " + (msg.name || "no name"));
                    }
                }
            }
            if(kag.base) results.push("Base layer exists");
            if(kag.back) results.push("Back layer exists");
        }
        
        // kag.back анализ
        results.push("\n=== kag.back ===");
        results.push("Type: " + typeof kag.back);
        if(kag.back && typeof kag.back === "object") {
            if(kag.back.messages) {
                results.push("Messages: " + kag.back.messages.length + " layers");
            }
        }
        
        return results.join("\n");
    } catch(e) {
        return "Error: " + e.message;
    }
})()
```

## Мониторинг событий

### Перехват и логирование тегов

```tjs
(function(){
    if(global.tagLogActive) return "Tag logging already active";
    
    global.tagLogActive = true;
    global.tagLogCount = 0;
    global.tagLogMax = 100; // Максимум записей
    
    // Сохраняем оригинальный обработчик
    global.originalOnTag = kag.conductor.onTag;
    
    // Устанавливаем новый обработчик
    kag.conductor.onTag = function(tag, elm) {
        if(global.tagLogCount < global.tagLogMax) {
            var info = "Tag #" + global.tagLogCount + ": " + tag;
            if(elm && typeof elm === "object") {
                // Добавляем важные атрибуты
                if(elm.href) info += " href=" + elm.href;
                if(elm.name) info += " name=" + elm.name;
                if(elm.storage) info += " storage=" + elm.storage;
                if(elm.target) info += " target=" + elm.target;
            }
            Debug.message(info);
            global.tagLogCount++;
        }
        
        // Вызываем оригинальный обработчик
        if(global.originalOnTag) {
            return global.originalOnTag(tag, elm);
        }
    };
    
    return "Tag logging started (max " + global.tagLogMax + " tags)";
})()
```

### Остановка логирования тегов

```tjs
(function(){
    if(!global.tagLogActive) return "Tag logging not active";
    
    // Восстанавливаем оригинальный обработчик
    if(global.originalOnTag) {
        kag.conductor.onTag = global.originalOnTag;
        global.originalOnTag = null;
    }
    
    var result = "Tag logging stopped. Logged " + global.tagLogCount + " tags.";
    global.tagLogActive = false;
    global.tagLogCount = 0;
    
    return result;
})()
```

## Утилиты

### Создание безопасной оболочки для выполнения кода

```tjs
// Создаём безопасную функцию выполнения
global.safeExec = function(code, context) {
    try {
        var result;
        if(context) {
            // Выполняем в контексте указанного объекта
            result = (function() { return eval(code); }).call(context);
        } else {
            result = eval(code);
        }
        return { success: true, result: String(result) };
    } catch(e) {
        return { success: false, error: e.message };
    }
};

// Пример использования:
// var result = safeExec("this.name", kag.conductor);
// return result.success ? result.result : "Error: " + result.error;
```

---

**Использование:**
1. Скопируйте нужный скрипт в Scripting Window
2. Измените названия объектов при необходимости
3. Запустите через кнопку Execute
4. Результаты будут выведены в консоль

**Предостережения:**
- Всегда используйте try-catch блоки
- Ограничивайте вывод при итерации больших объектов
- Сохраняйте оригинальные обработчики перед перехватом
- Используйте эти скрипты на свой риск - могут вызывать лаги
