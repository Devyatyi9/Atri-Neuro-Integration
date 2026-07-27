# multilang.tjs — Multi-Language System

Источник: байткод игры `sysscn/multilang.tjs`

**Подтверждено декомпилятором tjs2dec.** См. `disasm/multilang.tjs.txt`.

Центральная система поддержки нескольких языков. Управляет переключением языка, загружает шрифты, обрабатывает LanguageTags.

> **Связь с LineModeEx.tjs:** `LanguageTags` + `CurrentLanguageTag` используются из multilang.tjs, а `parseLanguage` (хендлер `[language]` тега) и `characterVoiceInfo` — в `LineModeEx.tjs` (диалоговый движок, загружается отдельно).

---

## LanguageTags (глобальный массив)

```javascript
LanguageTags = ["jp", "en", "cn"];
// Индекс:  0     1     2
// Соответствует kag.languageType:
//   0 = Japanese
//   1 = English
//   2 = Chinese (Traditional)
```

## Глобальные свойства

| Свойство | Тип | Описание |
|----------|-----|----------|
| `LanguageTags` | Array | ["jp", "en", "cn"] — теги доступных языков |
| `CurrentLanguageTag` | String | `LanguageTags[kag.languageType]` — текущий язык |
| `CurrentLanguageUITag` | String | `LanguageTags[kag.uiLanguageType]` — язык UI |
| `LanguageWordBreaks` | Object | Настройки переносов для каждого языка |
| `LanguageWidthTimeScales` | Object | Настройки времени отрисовки для каждого языка |
| `GetLanguageParamsList(langType, default)` | Function | Возвращает массив параметров для всех языков |
| `subLanguageType` | Property | Прокси к `SystemConfig.subLanguageType` |

## Ключевые функции

### `SetLanguageType(newType[, subType])`

```javascript
SetLanguageType(type, subType) {
    var oldType = kag.languageType;
    kag.languageType = (int)type;
    if (subType !== void)
        SystemConfig.subLanguageType = (int)subType;
    CallLangChanged(newType, oldType);
}
```

Устанавливает `kag.languageType` (0/1/2) и вызывает `CallLangChanged`.

### `SetSubLanguageType(type)`

```javascript
SetSubLanguageType(type) {
    var oldType = kag.languageType;
    SystemConfig.subLanguageType = (int)type;
    CallLangChanged(oldType, oldType); // оба = oldType
}
```

### `SetUILanguageType(type)`

Устанавливает `SystemConfig.uiLanguageType` (язык UI-элементов меню).

### `CallLangChanged(newType, oldType)`

```javascript
CallLangChanged(newType, oldType) {
    kag.callHook("onSystemLangChanged", newType, oldType);
    Current.action("onLangChanged");
    updateLangMenuCheck();
}
```

Оповещает систему о смене языка.

### `CurrentLanguageTag` (property getter)

```javascript
getter() {
    var tags = LanguageTags;
    if (typeof kag != "Object" || !kag || !kag.invalid) // проверка kag
        return tags[0]; // fallback: jp
    return tags[kag.languageType];
}
```

### `GetLanguageParamsList(langType, defaultVal)`

```javascript
GetLanguageParamsList(langType, defaultVal) {
    var result = [];
    var tags = LanguageTags;
    var map = SystemConfig.multiLangParamsMap || %[];
    for (var i = 0; i < tags.count; i++) {
        var tag = tags[i];
        if (tag != "") {
            var entry = map[tag];
            if (entry !== void && typeof entry[langType] != "undefined")
                result.add(entry[langType]);
            else
                result.add(defaultVal);
        } else {
            result.add(defaultVal);
        }
    }
    return result;
}
```

## Инициализация (top-level код)

При загрузке multilang.tjs:

1. Создаёт `SystemConfig.multiLangLanguageTags = ["jp", "en", "cn"]`
2. Настраивает `multiLangParamsMap`:
   ```javascript
   multiLangParamsMap = {
       "en": { word_break: 0, width_time_scale: 1 },
       // jp и cn — без доп. настроек
   }
   ```
3. Настраивает `multiLangTagAliasMap`:
   ```javascript
   multiLangTagAliasMap = ["chinese", "cn", "japanese", "jp", "english", "en", ""]
   ```
4. Настраивает `multiLangMenuMap`:
   ```javascript
   multiLangMenuMap = {
       "jp": "&Japanese",
       "en": "&English",
       "cn": "Traditional &Chinese"
   }
   ```
5. Регистрирует хуки:
   - `onPreSaveStoreBookmarkInfo` — сохранение языка в сейвы
   - `onSystemLangChanged` — обновление шрифтов при смене языка
6. Регистрирует методы в `kag`:
   - `kag.getLanguageFont(langType)` — получение шрифта для языка
   - `kag.initLanguageFont()` — инициализация шрифтов
   - `kag.resetLanguageFont()` — сброс шрифтов
   - `kag.setLanguageFont()` — установка шрифта

## Поток смены языка в игре

```
Пользователь выбирает язык в меню
  → вызывается SetLanguageType(1)  // 1 = English
    → kag.languageType = 1
    → CallLangChanged(1, oldType)
      → kag.callHook("onSystemLangChanged", 1, oldType)
        → обновление шрифтов, перерисовка меню
      → Current.action("onLangChanged")
        → оповещение всех слоёв
      → updateLangMenuCheck()
```

## Использование для Neuro

Для определения текущего языка:

```javascript
// Текущий languageType (число):
kag.languageType  // 0=JP, 1=EN, 2=CN

// Текущий тег языка (строка):
CurrentLanguageTag  // "jp", "en", "cn"

// Ранее сохранённый язык из сейва:
GetSysLangText("speechtext")  // текущий languageTag для текста
```

Для принудительной установки языка:
```javascript
neuroExec("SetLanguageType(1)");  // переключить на английский
```
