# Runtime Settings Reference

Настройки игры ATRI, доступные через TJS в runtime. Основано на `initialEnvironment()` в `custom.tjs` и `SystemConfig`.

---

## Sound

### On/Off

| Команда | Тип | Описание |
|---------|-----|----------|
| `kag.waveenable` | bool | Wave |
| `kag.bgmenable` | bool | BGM |
| `kag.seenable` | bool | SE |
| `kag.voiceenable` | bool | Voice |
| `kag.bgv2enable` | bool | Background voice |
| `kag.movieAudioEnable` | bool | Movie audio |
| `kag.voicecut` | bool | Вырезать голос |
| `kag.bgmdown` | bool | Приглушать BGM при голосе |
| `kag.nosewhenskip` | bool | Не играть SE при скипе |
| `kag.voicecutpage` | bool | true=вырезать при смене страницы, false=при смене имени |
| `SystemVoice.enabled` | bool | Системный голос |
| `kag.sysseenable` | bool | Системные SE |

### Volume (0–100)

| Команда | Default | Описание |
|---------|---------|----------|
| `kag.wavevolume` | 100 | Wave |
| `kag.bgmvolume` | 40 | BGM |
| `kag.sevolume` | 60 | SE |
| `kag.voicevolume` | 100 | Voice |
| `kag.bgv2volume` | 50 | Background voice |
| `kag.movieAudioVolume` | 55 | Movie audio |
| `kag.bgmdownlevel` | 80 | Насколько приглушать BGM при голосе (0–100%) |
| `SystemVoice.pervolume` | 75 | Громкость системного голоса |
| `kag.syssevolume` | 100 | Громкость системных SE |

### Прочее

| Команда | Описание |
|---------|----------|
| `kag.voicespeed` | Скорость голоса (min:1.0) |
| `kag.scflags.voiceSpeedSync` | Синхронизировать скорость голоса со скоростью игры |
| `setVoiceAll(true, 100)` | Включить все голоса, громкость 100% |
| `kag.setVoiceVolume(name, vol)` | Индивидуальная громкость персонажа (`"●アトリ", 80`) |

---

## Window / Display

| Команда | Тип | Описание |
|---------|-----|----------|
| `kag.fullScreen` | bool | Fullscreen mode |
| `kag.noeffect` | bool | false=эффекты включены, true=выключены |
| `kag.changeFullScreenOrWindow()` | method | Переключить fullscreen/window |
| `setSysArgMenuItem("sqscr", "off")` | — | Соотношение экрана (16:9 / 4:3) |
| `setSysArgMenuItem("scanim", "yes")` | — | Анимационные эффекты |
| `setSysArgMenuItem("stayontop", "no")` | — | Поверх всех окон |
| `SystemConfig.MusicTitleAutoHideTime` | int | -1=нет, 0=всегда, 1+=секунд |
| `SystemConfig.showVoiceReplay` | bool | Кнопка повтора голоса |
| `SystemConfig.showVoiceSave` | bool | Сохранение голоса в избранное |
| `sf.windowOpac` | int (0–255) | Непрозрачность окна сообщений |

---

## System

| Команда | Default | Описание |
|---------|---------|----------|
| `kag.drawspeed` | 1 | Скорость игры (min:1.0) |
| `SystemConfig.immQuitOnSaved` | false | Выйти сразу после сохранения |
| `SystemConfig.TextSpeechEnabled` | false | Внешний голосовой инструмент |
| `SystemConfig.touchUIMode` | bool | Touch-режим UI |
| `SystemConfig.readedTextColorChange` | true | Цвет прочитанного текста |
| `kag.appIconicPreview` | bool | Превью при свёртке |

---

## Text / Skip

| Команда | Default | Описание |
|---------|---------|----------|
| `kag.textspeed` | 80 | Скорость текста (100=мгновенно, 0=медленно) |
| `kag.autospeed` | 50 | Скорость auto-режима |
| `kag.autoModeWaitVoice` | 0 | 0=норм, 1=по голосу, 2=по скорости |
| `kag.allskip` | false | Skip непрочитанного |
| `kag.skipspeed` | 0 | Скорость skip (0=макс) |
| `kag.cancelAutoWhenClick` | false | Клик отключает auto |

---

## SystemConfig — общие настройки

Хранятся в `with (SystemConfig) { ... }`. Сохраняются между сессиями.

| Поле | Описание |
|------|----------|
| `SystemConfig.volumeNumber` | Номер тома (для мульти-томных изданий) |
| `SystemConfig.uiLanguageType` | Язык UI (0=jp, 1=en, 2=cn) |
| `SystemConfig.subLanguageType` | Суб-язык |
| `SystemConfig.XRated` | R18 контент |
| `SystemConfig.disableChangeUILang` | Блокировка смены языка |
| `SystemConfig.quickMenuPrevScn` | Кнопка "назад" в быстром меню |
| `SystemConfig.quickMenuNextScn` | Кнопка "вперёд" в быстром меню |
| `SystemConfig.DialogForcusForceYes` | Принудительный "Да" в диалогах |

---

## Детекция первого запуска

Игра отслеживает первый запуск через persistent flag `sf.notFirst`:

```tjs
// true = первый запуск (Language Display ещё не показано)
var isFirstRun = !sf.notFirst;
```

Логика в `custom.ks:240`:
```tjs
[eval exp='tf.LangSelect=!sf.notFirst, tf.ExecGame = true']
[eval exp='sf.notFirst=true']
```

При первом запуске:
1. `sf.notFirst` отсутствует → `tf.LangSelect = true`
2. Открывается Language Display (выбор языка)
3. После подтверждения → `*langsel_back` → `sf.initialMainLang/sub/UILang` сохраняются
4. При повторном запуске `sf.notFirst` уже `true` → Language Display пропускается

## Применение настроек при первом запуске

```tjs
if (!sf.notFirst) {
    kag.bgmvolume   = 30;   // понизить BGM
    kag.sevolume    = 50;   // понизить SE
    kag.voicevolume = 80;   // понизить голос
    kag.changeFullScreenOrWindow(); // переключить на fullscreen
}
```

Либо, если нужно применить настройки **после** выбора языка, захукаться на `onLangChanged` или на момент когда `tf.LangSelect` сбросится (после `*langsel_back`).
