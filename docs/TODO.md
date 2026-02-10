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

- [x] Добавить чекбокс "No Wrapper" рядом с Debug Output в OnResize()
- [x] Добавить кнопку "Script Mode" для открытия многострочного окна
- [x] Обновить раскладку для 3-го чекбокса (увеличить ширину окна)

#### Удобные инструменты исследования

- [x] Создать функцию DumpKeys(objectName, fileName) для сохранения ключей объекта в файл
- [x] Добавить DumpKeys("kag", "kag-full.txt") в основную консоль (добавлены примеры в приветствие)
- [ ] Создать поиск по ключевым словам среди всех ключей kag (например, по "select", "choice", "option")
- [x] Изучить 1426 свойств kag объекта для поиска полезных функций

#### Документация

- [x] Создать GAME-MECHANICS.md с ключевыми функциями игры
- [x] Создать docs/GameObjectsKeys/ с ключами всех объектов

#### Scripting Window (многострочный режим) - ГОТОВО ✅

- [x] Создать класс ScriptingWindow с RichEdit редактором
- [x] Реализовать кнопку Execute для выполнения скрипта
- [x] Обработка Enter для переноса строки (без отправки)
- [x] Использовать TVPExecuteScript() вместо TVPExecuteExpression()
- [x] Оптимальный размер окна (800x600)

#### Логика выполнения

- [x] Модифицировать ExecuteTJSCommand() с проверкой "No Wrapper"
- [x] Условное применение try-catch обёртки
- [x] Разные пути выполнения для expression vs script modes
- [x] Улучшенная очистка ввода от символов (\t, \r, лишние пробелы)

#### Интеграция

- [x] Связь между NeuroConsole и ScriptingWindow
- [x] Передача результатов выполнения в основную консоль
- [x] Обработка ошибок в "No Wrapper" режиме

#### Исследование игры

Изучить
extract_dataxp3\main\default.tjs
extract_dataxp3\main\custom.tjs
extract_dataxp3\main\custom.ks
PS в custom.tjs есть пример перехвата тегов

- [ ] Изучить команды для получения значений и управления контролами
- [ ] Найти где хранятся локализованные имена персонажей (сейчас выводятся на японском)
- [ ] Документировать найденное в отдельный .md файл

#### Перехват игровых событий

- [ ] Перехват появления/исчезновения персонажей в сцене
- [ ] Перехват смены фонов/локаций
- [ ] Перехват звуковых событий (BGM, SE, voice)
- [ ] Сбор контекста для принятия решений Neuro-sama

#### Исправление критических проблем NeuroConsole

**🔴 Срочно (критично):**

- [x] Утечки памяти: FreeLibrary для Msftedit.dll
- [x] Утечки памяти: DeleteObject для созданных шрифтов  
- [x] Утечки памяти: восстановление оригинальных WNDPROC в деструкторе
- [x] Утечки памяти: проверка в AppendToNeuroConsole при неудачном PostMessage
- [x] Thread safety: добавить mutex для ConsoleAppendData
- [x] Thread safety: atomic операции для g_Console
- [x] Buffer overflow: динамические буферы вместо фиксированных 4096
- [x] Buffer overflow: безопасная очистка строк с iswspace()

**🟡 В краткосрочной перспективе:**  

- [ ] Error handling: проверки создания окон
- [ ] Error handling: полная обработка ошибок wcscpy_s
- [ ] UI/UX: заменить блокирующий Sleep на event-based ожидание
- [ ] UI/UX: валидация длины команды
- [ ] Производительность: PeekMessage с timeout вместо GetMessage
- [ ] Производительность: batch append операций

✅ **Фикс критической ошибки:** WindowProc NULL pointer checking для предотвращения crash при кликах на кнопки

**🟢 В долгосрочной перспективе:**

- [ ] Безопасность: валидация входных данных
- [ ] Безопасность: проверки границ массивов
- [ ] Поддерживаемость: константы вместо magic numbers
- [ ] Поддерживаемость: рефакторинг больших функций

## Planned

### Интегрировать Dear ImGui для Object Explorer

Использовать TreeNode для итерирования объктов, и использовать Lazy Loading для поочерёдного раскрытия каждого объекта. 

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
