# TODO - KiriKiri-Z Neuro Integration

## Completed

- [x] Thread-safe TJS execution via Window Subclass
- [x] Use-After-Free fix in ExecuteTJSSync (heap allocation + processed flag)
- [x] Race condition fix - thread.join() instead of Sleep(100)
- [x] Buffered logging with mutex
- [x] IsMainThread() check to avoid deadlock
- [x] IsWindow() validation
- [x] tp_stub connected
- [x] NeuroConsole — своя консоль для TJS команд (F12)
- [x] libneurosdk интеграция (C++ функции готовы, TJS обёртки pending)
- [x] Проверить компиляцию и работу NeuroConsole

## In Progress

- [ ] 

## Planned

### Исследование игры

- [ ] Изучить какие команды нужно использовать для получения необходимых значений и команды воздействия на контролы игры
- [ ] Распаковать .tjs и .ks файлы для более глубокого анализа игры чтобы определить недостающие команды

### Batching for async TJS calls

При множественных командах от Neuro SDK возможны подвисания окна игры.
Решение: собирать команды в batch и выполнять за один message pump cycle.

```cpp
// Идея: ring buffer или std::queue с mutex
// WM_TJS_EXECUTE_BATCH - обработать все команды за раз
struct TJSCommandBatch {
    std::vector<std::wstring> commands;
    std::mutex mutex;
};
```

### ncbind и дополнительные заголовки

Для глубокой интеграции (не просто добавление функций, а взаимодействие с внутренними структурами движка) могут понадобиться:

- ncbind - для привязки C++ классов к TJS

- Заголовки из krkrz_dev для доступа к внутренним классам

- Возможно WindowEx, KAGParser и другие модули
  
  ```
  ncbind
    └── tjsVariant.h
          └── tjsString.h
                └── tjsCommHead.h
                      └── tjs.h (iTJSDispatch2)
  ```

### Другие улучшения

- [ ] SetWindowSubclass вместо SetWindowLongPtr (требует вызова из main thread — через SendMessage)
- [ ] Обработка TJS исключений через tTJSVariantClosure (некритично)

### TJS функции для Neuro API

- [ ] neuroConnect(url, gameName) - подключение к Neuro API
- [ ] neuroDisconnect() - отключение
- [ ] neuroSendContext(message, silent) - отправка контекста
- [ ] neuroRegisterAction(name, description, schema) - регистрация action
- [ ] neuroSendActionResult(id, success, message) - результат action
- [ ] neuroIsConnected() - проверка подключения

## Notes

### Thread Safety

TJS2 НЕ потокобезопасен. Все вызовы TVPExecuteExpression должны быть из главного потока.
Текущее решение: Window Subclass + PostMessage для cross-thread communication.

### KAG Ready Check

```cpp
TVPExecuteExpression(TJS_W("typeof global.kag != 'undefined'"), &result);
```

### Чтение файлов из VFS (XP3 архивов)

Через TJS можно читать файлы напрямую из архивов игры:

```js
(function() { var a = []; if(!a.load("main/Config.tjs")) return "FAIL"; return a.join("\n"); })()
```

**Протоколы путей:**

- `storage://` — корень данных/архивов
- `appdata://` — папка настроек пользователя
- `file://` — физический файл на диске

**Проверка существования в консоли:**

```
Debug.message(Storages.isExistentStorage("main/Config.tjs"))
```

**Для бинарных файлов:** `TVPCreateIStream` → `tTJSBinaryStream*`

#### Проверка расположения по имени в консоли:

`Debug.message(Storages.getPlacedPath("Config.tjs"))`

#### Получение содержимого файла через TJS-скрипт:

```js
(function() { var a = []; if(!a.load("main/Config.tjs")) return "FAIL"; return a.join("\n"); })()
```
