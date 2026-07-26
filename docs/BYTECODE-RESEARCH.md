# TJS Bytecode Research (2026-07-23)

## Полная распаковка data.xp3

Папка: `D:\Games\ATRI-TestVersion\new-extract-data`

Всего ~220 `.tjs` файлов.

### Текстовые (UTF-16LE, читаемые) — ~39 файлов

| Директория | Кол-во | Назначение |
|------------|--------|------------|
| `main/*.tjs` | 12 | Сюжетная логика, UI: `envinit.tjs` (196KB), `uimain.tjs` (236KB), `custom.tjs`, `default.tjs`, `Config.tjs` |
| `scenario/transitions/*.tjs` | 22 | Эффекты переходов |
| `scenario/scripts/*.tjs` | 3 | Скрипты сцен |
| `font/*.tjs` | 1 | Шрифты |
| `AppConfig.tjs` | 1 | Конфиг |

**→ 39 файлов можно читать напрямую.**

### Скомпилированные (TJS2 bytecode) — ~180 файлов

| Директория | Кол-во | Назначение |
|------------|--------|------------|
| `system/*.tjs` | 91 | Движок KiriKiri: Conductor, MainWindow, MessageLayer, SelectLayer, KAGEnv*, Stand*, BGM, SE, Voice, и т.д. |
| `sysscn/*.tjs` | 89 | Системные экраны: saveload, option, backlog, title, voicemode, и т.д. |
| `k2compat/*.tjs` | 12 | Совместимость с KiriKiri2 |
| `dmmcloud/*.tjs` | 2 | DMM Cloud |
| `startup.1.tjs` | 1 | Стартовый (дубль) |

## Доступные исходники из репозиториев

- **`D:\ProjectsD\krkrz_dev\script\KAG3\data\system\`** — 21 файл (базовый набор)
- **`D:\ProjectsD\KAGeXpress\Sample\system\`** — 29 файлов (чуть шире)
- Остальные ~130 скомпилированных файлов не имеют исходников

### Проверено на Conductor.tjs

Исходник `Conductor.tjs` из `krkrz_dev` НЕ совпадает с игровым байткодом:
- В игре добавлен класс `OneShotAsyncTrigger` (отсутствует в исходнике)
- В игре добавлен класс `Conductor` (наследует `BaseConductor`) — в исходнике он есть
- Структура классов/функций в остальном схожа

**Вывод:** игра модифицировала системные TJS-файлы. Исходники из репозиториев можно использовать как референс, но не как точную копию.

## Инструмент: tjsDisasm

- Путь: `D:\ProjectsD\tjsdisasm\release\bin\tjsdisasm.exe`
- Работает со всеми опкодами TJS2
- Вывод: ассемблерные мнемоники с аннотациями (строковые константы, inline-комментарии)
- Поддерживает все ~60 опкодов
- Есть исходники в `D:\ProjectsD\tjsdisasm\src\`

### Использование

```powershell
# Запуск из командной строки (аргумент: путь до .tjs файла)
& "D:\ProjectsD\tjsdisasm\release\bin\tjsdisasm.exe" "path\to\file.tjs" 2>&1

# Пример: дамп Conductor.tjs
& "D:\ProjectsD\tjsdisasm\release\bin\tjsdisasm.exe" "D:\Games\ATRI-TestVersion\new-extract-data\system\Conductor.tjs" 2>&1

# Сохранение результата в файл
& "D:\ProjectsD\tjsdisasm\release\bin\tjsdisasm.exe" "file.tjs" 2>&1 | Out-File -FilePath "dump.txt" -Encoding utf8
```

Выходные данные содержат:
- Заголовок байткода (top level script)
- Все классы и функции с их смещениями
- Тело каждой функции в виде ассемблерных инструкций с аннотациями констант

## План: LLM-assisted decompilation

1. Прогнать все `system/*.tjs` и `sysscn/*.tjs` через tjsDisasm
2. Для файлов с исходниками (krkrz_dev/KAGeXpress): Diff — найти изменения
3. Для файлов без исходников: скормить дизассемблер LLM для реконструкции TJS
4. Собрать карту событий (onTag, onLabel, onJump, etc.) для интеграции Neuro

## Ключевые файлы для интеграции (приоритет)

Из анализа `Conductor.tjs` (байткод) уже видны хуки событий:

| Событие | Функция | Вызовет owner-а |
|---------|---------|-----------------|
| Загрузка сценария | `onScenarioLoad(loadParam)` | `owner.onConductorScenarioLoad()` |
| Сценарий загружен | `onScenarioLoaded()` | `owner.onConductorScenarioLoaded()` |
| Метка (label) | `onLabel()` | `owner.onConductorLabel()` |
| Jump | `onJump()` | `owner.onConductorJump()` |
| Call | `onCall()` | `owner.onConductorCall()` |
| Return | `onReturn()` | `owner.onConductorReturn()` |
| AfterReturn | `onAfterReturn()` | `owner.onConductorAfterReturn()` |
| Script | `onScript()` | `owner.onConductorScript()` |
| Before tag | `onBeforeTag()` | `owner.onConductorBeforeTag()` |
| Unknown tag | `onUnknownTag()` | `owner.onConductorUnknownTag()` |
| Stop | `onStop()` | `owner.onConductorStop()` |

Это именно то, что нужно для перехвата игровых событий.
