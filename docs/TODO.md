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

### Исследование игры
- [x] Разобрать `backlog.tjs` — HistoryTextStore: `data[]` массив с `{name, disp, text, plaintext, voice}`
- [x] Разобрать `LineModeEx.tjs` — TagQueue, `parseLanguage`, `characterVoiceInfo`
- [x] Разобрать `rendermsgwin.tjs` — `drawName`, `getLangInfo` цепочка
- [x] Разобрать `uimain.tjs` — `SetLanguageType`, `LanguageTags`
- [x] Разобрать `multilang.tjs` — `LanguageTags = ["jp","en","cn"]`, `CurrentLanguageTag`, `SetLanguageType`
- [x] Разобрать `langfileparser.tjs` — `LangFileParser` extends `KAGParser`
- [x] Разобрать `LangRender.tjs` — языковой рендеринг с `getLangInfo`
- [x] Разобрать `syslangtext.tjs` — системные языковые строки
- [x] Документировать PSB v3 формат в `docs/kirikiri/PSB-Format.md`
- [x] Найти `[language]` KAG тег и `parseLanguage` хендлер
- [x] Понять цепочку: PSB байткод → `language[type]` → `getLangInfo` → `dispNameFilter`
- [x] Обновлён `psb-analyze.ps1` — теперь извлекает (type, stringId) пары из массивов записей
- [x] Найдено: локализованные имена в `charaInfo.language[langType]["disp"]`
- [x] Найдено: `dispNameFilter` в Steam EN — NO-OP (`return name`)
- [x] Документировано: 46 персонажей, voice prefix, Character-Reference.md
- [x] Составлена полная карта runtime flow от PSB до отображаемого имени

### Улучшения консоли NeuroConsole

- [x] Добавить чекбокс "No Wrapper" рядом с Debug Output в OnResize()
- [x] Добавить кнопку "Script Mode" для открытия многострочного окна
- [x] Обновить раскладку для 3-го чекбокса (увеличить ширину окна)
- [x] Создать функцию DumpKeys(objectName, fileName) для сохранения ключей объекта в файл
- [x] Добавить DumpKeys("kag", "kag-full.txt") в основную консоль
- [x] Изучить 1426 свойств kag объекта для поиска полезных функций
- [x] Создать GAME-MECHANICS.md с ключевыми функциями игры
- [x] Создать docs/GameObjectsKeys/ с ключами всех объектов

#### Scripting Window (многострочный режим)
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

#### Исправление критических проблем NeuroConsole

- [x] Утечки памяти: FreeLibrary для Msftedit.dll
- [x] Утечки памяти: DeleteObject для созданных шрифтов
- [x] Утечки памяти: восстановление оригинальных WNDPROC в деструкторе
- [x] Утечки памяти: проверка в AppendToNeuroConsole при неудачном PostMessage
- [x] Thread safety: добавить mutex для ConsoleAppendData
- [x] Thread safety: atomic операции для g_Console
- [x] Buffer overflow: динамические буферы вместо фиксированных 4096
- [x] Buffer overflow: безопасная очистка строк с iswspace()
- [x] Фикс критической ошибки: WindowProc NULL pointer checking

## In Progress

- [ ] Проверить runtime: `neuroGetCurrentContext()` через NeuroConsole (F12 → `neuroGetCurrentContext()`)
- [ ] Проверить runtime: `neuroBuildVoiceMap()` через NeuroConsole
- [ ] Проверить runtime: `backlogWindow._historyTextStore.data` через NeuroConsole
- [ ] Проверить runtime: `getLangInfo(charaInfo, "disp", 0/1/2)` для разных языков

## Done in Current Session

- [x] Создать TJS функцию `neuroGetCurrentContext()` (dllmain.cpp:1526-1618) возвращающую JSON с:
  - `currentSpeaker`, `currentText`, `currentVoice`, `currentName`
  - `voicePrefix` (из `characterVoiceInfo[name].file`)
  - `languageType`, `languageTag`
  - `recentEntries` (последние 5 диалогов)
  - `voiceMap` (prefix → charaName)
  - `currentScene`, `currentPage`
- [x] Создать TJS функцию `neuroBuildVoiceMap()` (dllmain.cpp:1620-1660) для prefix→имя маппинга
- [x] Интегрировать обе функции в `RegisterAllFunctions()`

## Planned

### Сбор контекста для Neuro-sama

- [ ] Hook на `storeName`/`reline` для автоматического сбора контекста
- [ ] Перехват голосовых событий (voice file → prefix → персонаж)
- [ ] Перехват смены сцены/фона
- [ ] Перехват BGM/SE
- [ ] `neuroGetCurrentContext()` — JSON с контекстом

### ImGui Object Inspector

- [ ] Incremental Type Detection - проверять типы ключей по 10/кадр
- [ ] Покраска по типу: function=синий, object=жёлтый, value=белый
- [ ] Кэш типов в структуре KeyInfo
- [ ] Ограничение N проверок за кадр

### Batching for async TJS calls

- [ ] Ring buffer / queue с mutex для batch команд от Neuro SDK
- [ ] `WM_TJS_EXECUTE_BATCH` — обработка всех команд за один цикл

### Auto-configuration on startup

- [ ] Автоматически применить настройки при загрузке плагина (громкость ниже, fullscreen)
- [ ] Решить: задавать через TJS при инициализации или читать из конфиг-файла
- [ ] Продумать формат конфига (JSON/ini/tjs) и его расположение
- [ ] Проверить на чистых настройках (VM) чтобы вспомнить все желаемые настройки

### Форк libneurosdk под новую спецификацию Neuro API

- [ ] Создать форк libneurosdk C SDK с поддержкой обновлённого Neuro API
- [ ] Сверить diff между старой и новой спецификацией
- [ ] Добавить/обновить функции в соответствии с изменениями
- [ ] Подключить форк вместо текущего submodule

### ncbind и дополнительные заголовки

- [ ] ncbind - для привязки C++ классов к TJS
- [ ] Заголовки из krkrz_dev для доступа к внутренним классам
- [ ] WindowEx, KAGParser и другие модули

### TJS функции для Neuro API

- [ ] `neuroConnect(url, gameName)` - подключение к Neuro API
- [ ] `neuroDisconnect()` - отключение
- [ ] `neuroSendContext(message, silent)` - отправка контекста
- [ ] `neuroRegisterAction(name, description, schema)` - регистрация action
- [ ] `neuroSendActionResult(id, success, message)` - результат action
- [ ] `neuroIsConnected()` - проверка подключения

## Notes

### Thread Safety
TJS2 НЕ потокобезопасен. Все вызовы TVPExecuteExpression должны быть из главного потока.
Решение: Window Subclass + PostMessage для cross-thread communication.

### KAG Ready Check
```cpp
TVPExecuteExpression(TJS_W("typeof global.kag != 'undefined'"), &result);
```

### Чтение файлов из VFS (XP3 архивов)
Для бинарных файлов: `TVPCreateIStream` → `tTJSBinaryStream*`
См. `docs/TJS-Commands.md` для команд работы с файлами и объектами игры.

### Voice prefix → Character mapping
```javascript
// Строится динамически из characterVoiceInfo:
var prefixToChar = {};
for (var name in characterVoiceInfo)
    prefixToChar[characterVoiceInfo[name].file] = name;
// prefixToChar["atr"] → "アトリ/Atri"
```

### Локализованные имена
- `charaInfo.language[kag.languageType]["disp"]` — локализованное отображаемое имя
- `backlogWindow._historyTextStore.data[i].disp` — локализованное имя из истории
- `characterVoiceInfo[charaName].file` — 3-буквенный voice prefix (языконезависимый)
