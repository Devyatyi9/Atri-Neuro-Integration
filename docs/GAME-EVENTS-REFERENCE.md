# Game Events Reference

> **Цель:** Задокументировать все отслеживаемые события игры, их TJS/C++ источники и формат данных.
> **Статус:** Живой документ — пополняется по мере исследования через TCP-зондирование.

---

## Мониторинг (ContextMonitor)

Запускается в C++ потоке каждые 200мс. Вызывает `neuro_scripts/context_monitor.tjs`, который читает состояние игры и возвращает изменения.

### События

| Событие | Формат | TJS источник | Описание |
|---------|--------|-------------|----------|
| **Смена сценария** | `[scenario] path.ks` | `kag.getCurrentInfo().file` | Загрузка нового .ks файла |
| **Смена метки** | `[label] *labelname` | `kag.getCurrentInfo().label` | Переход на новую метку внутри сценария |
| **Смена строки** | `[line] N` | `kag.getCurrentInfo().line` | Прогресс внутри метки |
| **Имя говорящего** | `[name] "Display Name"` | `kag.historyLayer.currentName` | Локализованное имя персонажа |
| **Персонаж (ID)** | `[chara] アトリ` | `kag.historyLayer.currentChara` | Внутреннее имя персонажа (всегда яп.) |
| **Текст диалога** | `[text] "..."` | `kag.historyLayer.currentText` | Текущий текст (JP или EN) |
| **Смена BGM** | `[bgm] filename.ogg` | `kag.bgm.playingStorage` | Новый музыкальный файл |
| **Статус BGM** | `[bgm_playing] 0/1` | `kag.bgm.inPlaying` | Играет ли музыка |
| **Авто-режим** | `[auto] 0/1` | `kag._autoMode` | Включён ли авточтение |
| **Скип-режим** | `[skip] 0/1` | `kag._skipMode` | Включён ли пропуск |
| **Последний KAG тег** | `[lastTag] p` | `kag.conductor.lastTagName` | Последний обработанный тег (p, msgoff, msgon, etc.) |
| **Статус кондуктора** | `[status] 0/1/2` | `kag.conductor.status` | Состояние парсера (0=idle, 1=?, 2=running) |

### Статус кондуктора (`kag.conductor.status`)

| Значение | Описание |
|----------|----------|
| `0` | Idle — ожидание, конец сценария |
| `1` | Загрузка/переход |
| `2` | Активная обработка (диалог, анимация) |

### KAG теги (`kag.conductor.lastTagName`)

| Тег | Описание |
|-----|----------|
| `p` | Пауза (ожидание клика/авто) |
| `msgoff` | Скрытие окна сообщений |
| `msgon` | Показ окна сообщений |
| `l` | Метка |
| `bgm` | Запуск фоновой музыки |
| `se` | Звуковой эффект |
| `vid` | Видео |
| `anim` | Анимация |
| `chara_show` / `chara_hide` | Появление/исчезновение персонажа |

*Полный список: `docs/kag_tags_all.txt` (255 тегов)*

---

## Неподтверждённые / требуют проверки

| Событие | Предполагаемый источник | Статус |
|---------|------------------------|--------|
| Смена голосового префикса | `characterVoiceInfo[name].file` | ✅ Подтверждено |
| Cutscene (Movie) | `kag.movies` | ❓ Не исследовано |
| Появление спрайта | `kag.getCharacter()` или `kag.layer` | ❓ Требует зондирования |
| Выбор (меню) | `kag.conductor.status` + последний тег | ❓ Требует проверки |
| Сохранение/загрузка | `saveCount`, `restore` | ❓ Требует проверки |
| Концовка | `kag.getCurrentInfo().file` + тег | ❓ Требует проверки |

---

## Как исследовать новое событие

1. Открыть NeuroConsole (F12) или использовать TCP клиент:
   ```
   .\Tools\neuro_tcp_client.ps1 -Code "(function(){ ... return ...; })()"
   ```
2. Запросить объект: `Scripts.getObjectKeys(obj)` для списка свойств
3. Наблюдать значение во время нужного игрового события
4. Добавить отслеживание в `neuro_scripts/context_monitor.tjs`

---

## Формат лога

Записи пишутся в `neuro_tag_log.txt` в корне игры.

Пример:
```
[scenario] b101.ks
[label] *start
[line] 147
[name] "Woman's Voice"
[chara] キャサリン
[text] "「起きた？うなされてたわよ？」"
[lastTag] msgoff
[bgm] field.ogg
[bgm_playing] 1
[auto] 1
```

---

## Связанные файлы

- `neuro_scripts/context_monitor.tjs` — TJS логика мониторинга
- `KiriKiri-Test/integration-test/ContextMonitor.cpp` — C++ поток опроса
- `KiriKiri-Test/integration-test/ContextMonitor.h` — заголовок монитора
