# name.tjs — Character Name Input & Display System

Источник: байткод игры `sysscn/name.tjs`

**Подтверждено декомпилятором tjs2dec.** См. `disasm/name.tjs.txt`.

Система ввода имени главного героя, фильтрации отображаемых имён, и хранения имён персонажей.

---

## MyNameInputBase

Базовый класс для UI ввода имени.

**Свойства класса (static):**
- `ThisClass` — ссылка на класс
- `Instance` — синглтон-инстанс
- `NGWords` — Array, чёрный список запрещённых имён (грузится из `NGWordFile`)
- `NGWordFile` — string, путь к файлу с запрещёнными словами
- `Names` — Array, список имён для ввода
- `Values` — Dictionary, введённые значения
- `InputAreas` — Array, конфигурация полей ввода

**Методы:**

| Сигнатура | Описание |
|-----------|----------|
| `MyNameInputBase(owner)` | Конструктор. Загружает NGWords, инициализирует InputAreas/Names, вызывает `loadNameVariables()` |
| `finalize()` | Сбрасывает `Instance` |
| `foreachNames(callback)` | Итерация по Names с колбэком |
| `loadNameVariables()` | Загрузка сохранённых имён из `sf` |
| `saveNameVariables()` | Сохранение имён в `sf` |
| `check(name)` → bool | Валидация имени: проверка на NGWords, форматирование через `han2zen` |
| `isNGWord(str)` → bool | Проверка в чёрном списке |
| `getValueExpression(key)` | Возвращает TJS-выражение для доступа к значению |
| `addEditLayers(window, callback)` | Создание слоёв ввода |
| `onDefaultClick()` / `onCancelClick()` / `onOkClick()` | Обработчики кнопок |
| `onComplete(name)` / `onIncomplete()` | Колбэки завершения/незавершения |
| `onInsertText(text)` | Обработка вставки текста |
| `action()` | Основной экшен |

---

## MyNameInputDialog extends MyNameInputBase

Диалог ввода имени с UI.

**Свойства:**
- `Values` — Dictionary, введённые значения
- наследует всё от MyNameInputBase

**Методы:**
- `init()` — инициализация диалога
- `addEdit(edit)` — добавить поле ввода
- `save()` — сохранить через `storeRecName`
- `close()` — закрыть диалог
- `storeRecName()` — **сохраняет имя МС в system flags**

---

## storeRecName (глобальная функция)

```javascript
function storeRecName() {
    if (kag.isRecollection) return;  // пропустить в режиме воспоминаний
    sf["name_" + kag.currentRecordName] = f.name;
    sf["family_" + kag.currentRecordName] = f.family;
}
```

Сохраняет `f.name` и `f.family` в `sf` (system flags) с ключами `name_{recordName}` / `family_{recordName}`.
Где `kag.currentRecordName` — идентификатор текущей "записи" (сейв-слота?).

---

## initFamilyName (глобальная функция)

```javascript
function initFamilyName(family, name) {
    f.family = family;    // Флаг: фамилия
    f.name = name;        // Флаг: имя
    f.mother = family + " ";    // Флаг: мать (фамилия + пробел)
    f.father = family + " ";    // Флаг: отец (фамилия + пробел)
    f.fullname = family + name; // Флаг: полное имя
}
```

Инициализация имени главного героя. Вызывается при начале игры/новой игры.
В ATRI игрок вводит своё имя в начале, и оно сохраняется в `f.name`/`f.family`.

---

## dispNameFilter (глобальная функция)

```javascript
function dispNameFilter(str) {
    if (typeof str == "String") {
        str = str.trim();
        if (str.length == 1) str = " " + str + " ";
        else if (str.length == 2) str = " " + str + " ";
        if (str.length > 0) str = " " + str + " ";
    }
    return str;
}
```

**Вызывается из `rendermsgwin.tjs` при каждом отображении имени персонажа.** (см. `renderMsgwin.tjs:211`)

Поток вызова:
1. `kag.getLangInfo(charaInfo, "disp")` — получить локализованное отображаемое имя
2. Если пусто: `kag.getAliasName(charaInfo.name)` — получить алиас из voice info
3. Если определена глобальная `dispNameFilter` → вызвать её для фильтрации

### Использование для интеграции: подмена имён на английские

```javascript
// Определить глобально — будет автоматически вызываться
dispNameFilter = function(name) {
    var enNames = {
        "アトリ": "Atri", "キャサリン": "Catherine",
        "水菜萌": "Minamo", "竜司": "Ryuji",
        "凜々花": "Ririka", "夏生": "Natsuki",
        "日和": "Hiyori", "八千代": "Yachiyo",
        "鳰": "Nio", "ヤスダ": "Yasuda"
    };
    return enNames[name] || name;
};
```

> **Для Neuro:** `dispNameFilter` — идеальный hook point для подмены имён персонажей без модификации игры. Не требует правки сценариев или дампов памяти.

---

## Система голосовой информации (characterVoiceInfo)

Загружается из `main/charvoice.csv` функцией `loadCharacterVoiceInfo()` в `LineModeEx.tjs`.

**Формат CSV:**
```
DisplayName, VoiceFilePrefix, ConfigName, Volume%, Alias, Threshold1, Threshold2, Color
```

**Структура `characterVoiceInfo`:**
- Ключ: DisplayName (lowercased, для японских имён без изменений)
- Значение: `{name, file, config, per, alias, level: [], color}`

**Пример данных из ATRI:**
```
アトリ,atr,●アトリ,100,,,,
キャサリン,cat,●キャサリン,100,,,,
水菜萌,min,●水菜萌,100,,,,
竜司,ryu,●竜司,100,,,,
凜々花,rir,●凜々花,100,,,,
夏生,nat,●他男,100,,,,
```

Поле `alias` (5-я колонка) может содержать:
- Пустую строку — `getAliasName()` вернёт оригинальное имя
- Текст — будет возвращён как алиас
- `&<expression>` — будет выполнен как `Scripts.eval(<expression>)` для динамического разрешения

---

## Получение имени персонажа: вся цепочка

```
KAG тег [chara name="Tnat"]
  → MainWindow / LineMode обрабатывает тег
    → charaInfo = {name: "夏生", ...}
      → kag.getLangInfo(charaInfo, "disp") → локализованное отображаемое имя
        → если пусто: kag.getAliasName("夏生")
          → ищет characterVoiceInfo["夏生"]
            → находит {file:"nat", config:"●他男", alias:""}
            → alias пустой → возвращает "夏生"
        → dispNameFilter("夏生") → фильтрация (можно подменить)
      → kag.messageLayer.name = результат
      → BacklogHistoryTextStore: currentInfo = {name: "夏生", disp: "...", plaintext: "..."}
        → currentName → disp (если не пусто) иначе name
        → currentChara → name (всегда оригинальный ключ)
```
