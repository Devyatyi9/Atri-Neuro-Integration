# Архитектура плагина Neuro-Integration для KiriKiri Z

## Общая схема загрузки

```
Игра (ATRI.exe)
    │
    ├── Загружает version.dll (наш proxy DLL)
    │       │
    │       ├── DllMain() → инициализация proxy
    │       │
    │       ├── Проксирует вызовы к C:\Windows\system32\version.dll
    │       │
    │       └── Запускает std::thread(LoadAndLinkPlugin).detach()
    │               │
    │               ├── WaitForExporterInit() — ждёт до 10 сек
    │               │   пока iTVPFunctionExporter появится в памяти
    │               │   по адресу RVA 0x003B4538
    │               │
    │               ├── LoadLibraryW("Neuro-integration.dll")
    │               │
    │               ├── GetProcAddress(hPlugin, "V2Link")
    │               │
    │               └── v2link(exporter) — вызов V2Link плагина
    │
    └── Движок KiriKiri Z инициализирует TJS...
```

## Цепочка инициализации плагина

### Этап 1: DLL загружена (DllMain)
```cpp
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LogToFile("=== NEURO PLUGIN SESSION STARTED ===\n");
    }
    return TRUE;
}
```
- DLL загружена в память
- Никакой инициализации движка ещё нет

### Этап 2: V2Link вызван proxy DLL
```cpp
extern "C" __declspec(dllexport) HRESULT _stdcall V2Link(iTVPFunctionExporter* exporter)
{
    // 1. Инициализируем tp_stub
    TVPInitImportStub(exporter);

    // 2. Запускаем фоновый поток для ожидания TJS
    StartDeferredTJSInit();

    return S_OK;
}
```
- Получаем указатель на `iTVPFunctionExporter`
- `TVPInitImportStub()` сохраняет exporter для доступа к функциям движка
- **ВАЖНО**: TJS ещё НЕ готов! `TVPGetScriptDispatch()` вернёт NULL или зависнет

### Этап 3: Фоновый поток ждёт TJS
```cpp
void DeferredTJSInitThread() {
    const int maxAttempts = 100;  // 10 секунд
    const int intervalMs = 100;

    for (int i = 0; i < maxAttempts && !g_TJSReady; i++) {
        Sleep(intervalMs);

        // Пробуем получить глобальный объект TJS
        iTJSDispatch2* dispatch = TVPGetScriptDispatch();
        if (dispatch) {
            g_ScriptDispatch = dispatch;
            g_TJSReady = true;
        }
    }

    if (g_TJSReady) {
        RegisterAllFunctions();  // Регистрируем функции
    }
}
```
- Поток работает параллельно с инициализацией движка
- Периодически проверяет готовность TJS
- Когда TJS готов — регистрирует функции

### Этап 4: Регистрация функций в TJS
```cpp
bool RegisterAllFunctions() {
    iTJSDispatch2* global = TVPGetScriptDispatch();

    // Создаём обёртку для C++ функции
    auto* func = new tTJSFunctionWrapper<NeuroTestFunction>();

    // Регистрируем в глобальном объекте TJS
    tTJSVariant val(func);
    func->Release();
    global->PropSet(TJS_MEMBERENSURE, TJS_W("neuroTest"), NULL, &val, global);

    global->Release();
    return true;
}
```
- `tTJSFunctionWrapper` — шаблонный класс-обёртка
- `PropSet` с флагом `TJS_MEMBERENSURE` создаёт свойство если его нет
- После регистрации функции доступны из TJS скриптов

## Ключевые компоненты

### iTVPFunctionExporter
Интерфейс движка для доступа к внутренним функциям:
```cpp
struct iTVPFunctionExporter {
    virtual bool QueryFunctions(const tjs_char** name, void** function, tjs_uint count) = 0;
    virtual bool QueryFunctionsByNarrowString(const char** name, void** function, tjs_uint count) = 0;
};
```
- `QueryFunctions` — получить указатели на функции движка по имени
- **НЕ** для регистрации своих функций!

### tp_stub
SDK для плагинов KiriKiri:
- `TVPInitImportStub(exporter)` — инициализация
- `TVPGetScriptDispatch()` — получить глобальный объект TJS
- `TVPExecuteExpression(code, result)` — выполнить TJS код
- `TVPUninitImportStub()` — очистка при выгрузке

### tTJSDispatch
Базовый класс для TJS-объектов. Для создания функции наследуемся и переопределяем `FuncCall`:
```cpp
class MyFunction : public tTJSDispatch {
    tjs_error TJS_INTF_METHOD FuncCall(
        tjs_uint32 flag,
        const tjs_char* membername,
        tjs_uint32* hint,
        tTJSVariant* result,
        tjs_int numparams,
        tTJSVariant** param,
        iTJSDispatch2* objthis) override
    {
        if (membername) return TJS_E_MEMBERNOTFOUND;
        // ... реализация функции ...
        return TJS_S_OK;
    }
};
```

## Вызов TJS из C++

```cpp
tTJSVariant result;
TVPExecuteExpression(TJS_W("neuroGetVersion()"), &result);
int version = (int)(tTVInteger)result;
```

## Вызов C++ из TJS

После регистрации функции доступны напрямую:
```tjs
var ver = neuroGetVersion();  // вызывает NeuroGetVersion()
neuroEcho("Hello!");          // вызывает NeuroEcho()
neuroTest();                  // вызывает NeuroTestFunction()
```

## Структура файлов

```
KiriKiri-Test/
├── dllmain.cpp              # Proxy DLL (version.dll)
├── KiriKiri-linker/         # Проект proxy DLL
│
├── integration-test/        # Проект плагина
│   ├── dllmain.cpp          # Основной код плагина
│   ├── NeuroConsole.cpp     # TJS консоль (в разработке)
│   ├── neuro.def            # Экспорт V2Link/V2Unlink
│   └── integration-test.vcxproj
│
└── krkr-sdk/                # KiriKiri SDK
    ├── tp_stub.h
    └── tp_stub.cpp
```

## Важные моменты

1. **Timing**: V2Link вызывается ДО полной инициализации TJS
2. **Threading**: Регистрация функций должна происходить после готовности TJS
3. **Reference counting**: tTJSDispatch использует подсчёт ссылок, Release() обязателен
4. **Memory**: Адреса в памяти специфичны для версии игры (см. .CT файл)

---

## TJS2 Threading Model — КРИТИЧЕСКИ ВАЖНО!

### TJS2 НЕ потокобезопасен!

Все вызовы TJS API должны происходить из **главного потока** игры.

| Безопасно из любого потока | ТОЛЬКО из главного потока |
|---------------------------|---------------------------|
| `TVPPostEvent()` | `TVPExecuteExpression()` |
| C++ код (сеть, файлы) | `FuncCall()`, `PropGet()`, `PropSet()` |
| Windows API | Любое взаимодействие с TJS объектами |
| `TVPAddLog()` | Регистрация/удаление функций |

### Механизмы синхронизации KiriKiri

1. **tTJSCriticalSection** — обёртка над Windows CRITICAL_SECTION
2. **tTJSCriticalSectionHolder** — RAII holder для критических секций
3. **TVPPostEvent()** — thread-safe постинг события в главный поток
4. **tTVPContinuousEventCallbackIntf** — callback вызываемый каждый кадр

### Message Window Pattern (экспериментальный)

Для безопасного вызова TJS из worker threads используется hidden window:

```cpp
// Worker thread (hotkey, websocket) отправляет команду:
PostMessage(g_MessageWindow, WM_TJS_EXECUTE, (WPARAM)codeCopy, 0);

// Message Window обрабатывает в контексте главного потока:
case WM_TJS_EXECUTE:
    TVPExecuteExpression(ttstr((wchar_t*)wParam), nullptr);
    delete[] (wchar_t*)wParam;
    return 0;
```

Доступны две функции:
- `ExecuteTJSAsync(code)` — fire-and-forget, не ждёт результата
- `ExecuteTJSSync(code, &result, timeout)` — ждёт результат (НЕ вызывать из главного потока!)

### TJS исключения в плагинах

**eTJS НЕ работает в плагинах!** Исключения обрабатываются внутри движка.
Не используйте `try { } catch (const eTJS& e) { }` — это бесполезно.

```cpp
// НЕПРАВИЛЬНО:
try {
    TVPExecuteExpression(code, &result);
} catch (const eTJS& e) {  // Никогда не сработает!
    ...
}

// ПРАВИЛЬНО:
TVPExecuteExpression(code, &result);
// Ошибки выводятся в Debug.console движка
```

---

## Ресурсы

- TJS2 Reference: https://krkrz.github.io/docs/tjs2/j/contents/index.html
- KAG System: https://krkrz.github.io/krkr2doc/kag3/
- ncbind: https://github.com/wamsoft/ncbind
- ScriptEnginePlugin: https://github.com/khjxiaogu/ScriptEnginePlugin
