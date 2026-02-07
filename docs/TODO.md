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
- [x] libneurosdk интеграция (C++ функции готовы)
- [x] Проверить компиляцию и работу NeuroConsole
- [x] Self-linking — RVA константы перенесены в плагин, proxy DLL упрощена
- [x] Консоль: Esc для скрытия, Ctrl+A без звуков
- [x] Распаковать .tjs и .ks файлы (нескомпилированные)

## In Progress

- [ ] Исследование kag.conductor и других объектов игры
- [ ] Обработка уже завёрнутых TJS функций (не заворачивать повторно)

### Улучшения консоли NeuroConsole

#### UI компоненты
- [ ] Добавить чекбокс "No Wrapper" рядом с Debug Output в OnResize()
- [ ] Добавить кнопку "Script Mode" для открытия многострочного окна
- [ ] Обновить раскладку для 3-го чекбокса (увеличить ширину окна)

#### Scripting Window (многострочный режим)
- [ ] Создать класс ScriptingWindow с RichEdit редактором
- [ ] Реализовать кнопку Execute для выполнения скрипта
- [ ] Обработка Enter для переноса строки (без отправки)
- [ ] Использовать TVPExecuteScript() вместо TVPExecuteExpression()
- [ ] Оптимальный размер окна (800x600)

#### Логика выполнения
- [ ] Модифицировать ExecuteTJSCommand() с проверкой "No Wrapper"
- [ ] Условное применение try-catch обёртки
- [ ] Разные пути выполнения для expression vs script modes
- [ ] Улучшенная очистка ввода от символов (\t, \r, лишние пробелы)

#### Интеграция
- [ ] Связь между NeuroConsole и ScriptingWindow
- [ ] Передача результатов выполнения в основную консоль
- [ ] Обработка ошибок в "No Wrapper" режиме

## Planned

### Исследование игры

- [ ] Изучить команды для получения значений и управления контролами
- [ ] Найти где хранятся локализованные имена персонажей (сейчас выводятся на японском)
- [ ] Документировать найденное в отдельный .md файл

#### Исследование имён персонажей
- [ ] Проверить `kag.conductor.getAliasName()` для получения оригинальных имён
- [ ] Исследовать `kag.tagHandlers["dispname"]` для перехвата отображаемых имён
- [ ] Найти альтернативные источники имён (возможно charaName или другие свойства)
- [ ] Проверить содержимое `kag.historyLayer.data[i].name` на разных локализациях

#### Перехват игровых событий
- [ ] Перехват появления/исчезновения персонажей в сцене
- [ ] Перехват смены фонов/локаций
- [ ] Перехват звуковых событий (BGM, SE, voice)
- [ ] Сбор контекста для принятия решений Neuro-sama

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

Планируется использовать отдельные .tjs файлы в папке `Scripts/` рядом с DLL (или запаковать в .xp3).

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

**Для бинарных файлов:** `TVPCreateIStream` → `tTJSBinaryStream*`

См. `docs/TJS-Commands.md` для команд работы с файлами и объектами игры.
