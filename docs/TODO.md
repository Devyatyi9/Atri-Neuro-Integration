# TODO - KiriKiri-Z Neuro Integration

## Completed

### Инфраструктура
- [x] Self-linking — RVA константы в плагине, proxy DLL упрощена
- [x] Thread-safe TJS execution via Window Subclass + WM_TJS_EXECUTE_SYNC
- [x] Use-After-Free fix в ExecuteTJSSync (heap + processed flag)
- [x] Race condition fix — thread.join() вместо Sleep(100)
- [x] Buffered logging with mutex
- [x] IsMainThread() + IsWindow() проверки
- [x] tp_stub connected
- [x] libneurosdk C++ функции готовы

### NeuroConsole (Win32, F12)
- [x] Своя консоль TJS команд (F12 toggle)
- [x] Esc для скрытия, Ctrl+A без звуков
- [x] "No Wrapper" чекбокс
- [x] "Script Mode" многострочный редактор
- [x] Исправлены утечки памяти (FreeLibrary, DeleteObject, WNDPROC)
- [x] Thread safety: mutex для ConsoleAppendData, atomic для g_Console
- [x] Buffer overflow: динамические буферы вместо 4096

### TCP сервер и клиент
- [x] WinSock2 TCP сервер на порту 18700
- [x] Length-prefixed UTF-8 протокол
- [x] Thread-safe через ExecuteTJSSync()
- [x] PowerShell клиент `Tools/neuro_tcp_client.ps1` (-Code, -Restart, -Kill)

### Регистрация TJS функций (ncbind)
- [x] 18 функций: neuroTest, neuroGetVersion, neuroEcho, neuroSendData, neuroIsTJSReady, neuroIsKAGReady, neuroShowConsole, neuroExec, neuroConsoleLog, neuroDump, neuroGetCurrentContext, neuroBuildVoiceMap, neuroStartHooks, neuroStopHooks, neuroStartTagLogger, neuroStopTagLogger, neuroFlushTagLog, neuroExecFile
- [x] neuroExecFile с silent параметром

### ContextMonitor
- [x] C++ polling thread (200ms) → neuro_scripts/context_monitor.tjs
- [x] Отслеживание: сценарий, метка, линия, имя, персонаж, текст
- [x] Отслеживание BGM (filename, playing state)
- [x] Отслеживание голоса (voicePlayingRatio)
- [x] Отслеживание визуальных эффектов (quake, msg_hide, select, noeffect)
- [x] Отслеживание режимов (auto, skip)
- [x] Отслеживание кондуктора (status, lastTagName)
- [x] Вывод в neuro_tag_log.txt (отдельный файл)

### Исследование игры
- [x] PSB v3 формат документирован
- [x] Character-Reference.md — 46 персонажей, voice prefix
- [x] GAME-MECHANICS.md — ключевые механики
- [x] GameObjectsKeys/ — ключи всех объектов (73 файла)
- [x] TJS2-SYNTAX.md — отличия TJS2 от JS (for...in не существует, null неюзабелен, etc.)
- [x] TJS-CODE-REFERENCE.md — inline TJS код
- [x] GAME-EVENTS-REFERENCE.md — runtime события и их источники
- [x] Подтверждено: tag hooks (addHook, onTag, onLabel) — dead code
- [x] kag.historyLayer.{currentName,currentChara,currentText} — источник диалога
- [x] kag.bgm, kag.voicePlayingRatio, kag.conductor — аудио-визуальный контекст
- [x] Разобраны: backlog.tjs, multilang.tjs, rendermsgwin.tjs, langfileparser.tjs, LangRender.tjs, uimain.tjs, syslangtext.tjs, LineModeEx.tjs

### Сборка и утилиты
- [x] psb-analyze.ps1 — bytecode-ordered PSB string extractor
- [x] gen_ccdb.ps1 — compile_commands.json
- [x] neuro_tcp_client.ps1 — удалённое выполнение TJS
- [x] **tjscheck** — standalone TJS2 syntax checker (`D:\ProjectsD\krkrz-tjscheck\`)
  - Debug: `tjscheck_d.exe`, Release: `tjscheck.exe`
  - Line numbers в syntax errors (сообщение пустое — locale не загружена, но номер строки корректен)
  - RegExp отключён (`TJS_NO_REGEXP`), Oniguruma не нужен
  - Сборка: `MSBuild tjscheck.vcxproj /p:Configuration=Release /p:Platform=Win32`
- [x] Контекст вынесен в `neuro_scripts/context_monitor.tjs` (через `neuroExecFile`)

## In Progress

- [ ] **Формат контекста для Neuro API** — спроектировать JSON структуру, определить триггеры отправки
- [ ] **tjscheck: загружать locale/messages** — чтобы сообщения об ошибках отображались, а не пустая строка

## Planned

### TJS функции для Neuro API
- [ ] `neuroConnect(url, gameName)` — подключение к Neuro API
- [ ] `neuroDisconnect()` — отключение
- [ ] `neuroSendContext(message, silent)` — отправка контекста
- [ ] `neuroRegisterAction(name, description, schema)` — регистрация action
- [ ] `neuroSendActionResult(id, success, message)` — результат action
- [ ] `neuroIsConnected()` — проверка подключения

### Форк libneurosdk
- [ ] Создать форк с поддержкой обновлённого Neuro API (startup ack)
- [ ] Сверить diff старой и новой спецификации
- [ ] Подключить форк вместо текущего submodule

### Batching
- [ ] Ring buffer / queue для batch команд от Neuro SDK
- [ ] WM_TJS_EXECUTE_BATCH — обработка всех команд за один цикл

### Auto-configuration
- [ ] Автоприменение настроек при загрузке плагина
- [ ] Формат конфига и расположение

## Cancelled
- [x] ~~ImGui Object Inspector~~ — не нужно, NeuroConsole + TCP клиент покрывают потребности
- [x] ~~TJS tag hooks (addHook, onTag, onLabel)~~ — dead code в движке
- [x] ~~for...in~~ — не существует в TJS2

## Notes

### Thread Safety
TJS2 НЕ потокобезопасен. Все вызовы TVPExecuteExpression — из главного потока.
Решение: Window Subclass + PostMessage.

### Voice prefix → Character mapping
```
characterVoiceInfo[name].file → 3-буквенный voice prefix (языконезависимый)
```

### KAG Ready Check
```cpp
TVPExecuteExpression(TJS_W("typeof global.kag != 'undefined'"), &result);
```
