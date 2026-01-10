# TODO - KiriKiri-Z Neuro Integration

## Completed

- [x] Thread-safe TJS execution via Window Subclass
- [x] Use-After-Free fix in ExecuteTJSSync (heap allocation + processed flag)
- [x] Race condition fix - thread.join() instead of Sleep(100)
- [x] Buffered logging with mutex
- [x] IsMainThread() check to avoid deadlock
- [x] IsWindow() validation
- [x] tp_stub connected
- [x] Debug console check (F9 hotkey)
- [x] KAG ready check (F12 hotkey)

## In Progress

- [ ] Integrate libneurosdk for WebSocket communication

## Planned

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

### Другие улучшения
- [ ] SetWindowSubclass вместо SetWindowLongPtr (более безопасный API)
- [ ] Обработка TJS исключений через tTJSVariantClosure

## Notes

### Thread Safety
TJS2 НЕ потокобезопасен. Все вызовы TVPExecuteExpression должны быть из главного потока.
Текущее решение: Window Subclass + PostMessage для cross-thread communication.

### KAG Ready Check
```cpp
TVPExecuteExpression(TJS_W("typeof global.kag != 'undefined'"), &result);
```
