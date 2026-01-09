connect `tp_stub` and `ncbind` to the project;

add console for TJS debuging;

firstly i need to check ingame console:

```cpp
void TryShowBuiltInConsole() {
 ExecuteTJSCode(L"Debug.console.visible = true;");
}
```

use libnuerosdk in project;


