connect `tp_stub` and `ncbind` to the project;

add console for TJS debuging;

firstly i need to check ingame console:

```cpp
void TryShowBuiltInConsole() {
 ExecuteTJSCode(L"Debug.console.visible = true;");
}
```

use libnuerosdk in project;

Проверить global.kag, kagReady должен быть true

```cpp
if (TVPExecuteExpression(TJS_W("typeof global.kag != 'undefined' && kag.mainLayer.visible"))) { ... }
```

```cpp
tTJSVariant result;
// Проверяем, существует ли глобальный объект 'kag'
TVPExecuteExpression(TJS_W("typeof global.kag != 'undefined'"), &result);

bool kagReady = result.AsInteger() != 0;

if (kagReady) {
    // Теперь можно вызывать функции меню, showAbout() и т.д.
    TVPExecuteExpression(TJS_W("System.showAbout()"), NULL);
} else {
    // Еще слишком рано, нужно подождать следующий кадр или цикл
    TVPExecuteExpression(TJS_W("Debug.message('KAG not ready yet, waiting...')"), NULL);
}
```

### Note:

```cpp
TVPExecuteExpression(TJS_W("System.showAbout()"), (tTJSVariant*)NULL);
```

```cpp

```
