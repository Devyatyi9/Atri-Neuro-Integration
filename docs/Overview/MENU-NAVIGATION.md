# Menu Navigation (TJS)

> Как программно управлять меню игры через TJS (для Neuro API / плагина).

## Состояния игры

`SystemHook.currentState` — текущее состояние (строка). `SystemHook.lastState` — предыдущее.

| Состояние   | Описание                | Скрипт           |
| ----------- | ----------------------- | ---------------- |
| `title`     | Главное меню            | title.ks         |
| `game`      | Основная игра           | —                |
| `option`    | Настройки               | option.ks        |
| `load`      | Загрузка                | load.ks          |
| `save`      | Сохранение              | save.ks          |
| `cgmode`    | CG галерея (Extra)      | cgmode.ks        |
| `soundmode` | Музыкальная галерея     | soundmode.ks     |
| `scenemode` | Воспоминания (Scene)    | scenemode.ks     |

Проверки:
```tjs
SystemHook.currentState == "title"   // в главном меню
SystemHook.currentState == "game"    // в игре
```

## Переход между меню

### Через SystemHook.jump (рекомендуется)

```tjs
SystemHook.jump(from, to);
SystemHook.jump("cgmode", "title");     // Extra → Title
SystemHook.jump("title", "cgmode");     // Title → Extra
SystemHook.jump("cgmode", "soundmode"); // CG → Music (внутри Extra)
SystemHook.jump("", "title");           // Откуда угодно → Title
```

### Через kag.process (прямой запуск скрипта)

```tjs
kag.process("title.ks", "*start");       // Title
kag.process("option.ks", "*start");      // Настройки
kag.process("cgmode.ks", "*start");      // CG Gallery
kag.process("soundmode.ks", "*start");   // Music
kag.process("scenemode.ks", "*start");   // Scene recollection
kag.process("load.ks", "*start");        // Загрузка
kag.process("save.ks", "*start");        // Сохранение
kag.process("", "*return");              // Вернуться
```

### Через startSubSystem (безопасный вход)

```tjs
startSubSystem(0, "title");       // Через главный conductor
startSubSystem(1, "option");      // Через extra conductor (sub-menu)
startSubSystem(1, "cgmode");      // Extra → CG
startSubSystem(1, "soundmode");   // Extra → Music
startSubSystem(1, "scenemode");   // Extra → Scene
```

### Extra Conductor (для sub-menus из игры)

```tjs
kag.callExtraConductor("option.ks", "*start");      // Открыть настройки
kag.returnExtraConductor();                          // Вернуться назад
kag.usingExtraConductor                              // true/false — внутри sub-menu?
```

## Навигация внутри Music Gallery

```tjs
SoundModeInstance.items.count              // количество треков
SoundModeInstance.items[i].storage          // "BGM01"
SoundModeInstance.items[i].title            // "海中都市"

SoundModeInstance.onSelect(index);          // выбрать/играть трек
SoundModeInstance.next();                   // следующая страница
SoundModeInstance.prev();                   // предыдущая
SoundModeInstance.play();                   // play/pause
SoundModeInstance.stop();                   // стоп
SoundModeInstance.setVolume(val);           // громкость (0–100)
SoundModeInstance.looping;                  // true/false — зацикливание
SoundModeInstance.playing;                  // текущий играющий storage
SoundModeInstance.isAnyPlaying              // играет ли что-то
SoundModeInstance.paused;                   // на паузе?
SoundModeInstance.getTitle(index)           // название трека
SoundModeInstance.isSeen(index)             // прослушан ли?
SoundModeInstance.currentVolume             // текущая громкость
```

## Клик / взаимодействие

```tjs
kag.onPrimaryClick();                // ЛКМ (диалоги, выбор пунктов)
kag.onPrimaryClick(1);               // без отмены auto-режима
kag.conductor.run();                 // продолжить выполнение сценария
kag.conductor.trigger("click");      // триггернуть click из conductor
kag.goToStart();                     // вернуться в Title (через anchor save)
```

## Пример: полный цикл навигации

```tjs
// Из игры → Extra → Music → играть трек → Title → обратно в Extra
startSubSystem(1, "cgmode");
// ...ждём...
SystemHook.jump("cgmode", "soundmode");
// ...ждём...
SoundModeInstance.onSelect(0);       // играть первый трек
// ...ждём...
SystemHook.jump("soundmode", "title");
// ...ждём...
SystemHook.jump("title", "cgmode");
```

## Проверено в рантайме

- `SystemHook.jump('cgmode', 'title')` → Extra → Title ✅
- `SystemHook.jump('title', 'cgmode')` → Title → Extra ✅
