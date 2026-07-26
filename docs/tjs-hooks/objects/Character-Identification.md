# Runtime Character Identification

Все способы определить, какой персонаж говорит/активен в данный момент, без хардкода.

---

## 1. Голосовой префикс (самый надёжный)

Уникальный 3-буквенный идентификатор, не зависящий от языка локализации.

**Источник:** `characterVoiceInfo` — грузится из `main/charvoice.csv`.

```javascript
// Через characterVoiceInfo (доступен всегда после загрузки)
var info = characterVoiceInfo["夏生"];
info.file; // → "nat"

// У всех озвученных персонажей есть voice prefix
var prefixMap = {
    atr: "Atri", cat: "Catherine", min: "Minamo",
    ryu: "Ryuji", rir: "Ririka",
    XXA: "Yoko", XXB: "Miyo",    // женские мобы
    YAS: "Yasuda", XYA: "Radio", // мужские мобы
    man: "Man", wom: "Woman",    // голосовые группы
};
```

**Использование с voice file path:**
```javascript
// "voice/chara1/atr_001_001.ogg" → извлекаем "atr"
function charFromVoicePath(path) {
    var m = path.match(/[/\\]([a-zA-Z]{3})_\d/);
    return m ? m[1] : null;
}
```

---

## 2. `characterVoiceInfo` (японское имя → информация)

Словарь, где ключ = японское имя, значение = `{name, file, config, per, alias, level, color}`.

Доступен глобально после инициализации `LineModeEx`.

```javascript
// Получить информацию по японскому имени
var info = characterVoiceInfo["夏生"];
// → { name: "夏生", file: "nat", config: "●他男", per: 100, alias: "" }

// Префикс для поиска голосовых файлов
var prefix = info.file; // "nat"

// Название конфига голоса (для SystemConfig)
var configName = info.config; // "●他男"
```

---

## 3. `currentChara` / `currentName` (backlog)

Из `backlog.tjs:HistoryTextStore` — последний обработанный персонаж.

```javascript
var store = kag.backlogWindow._historyTextStore;

store.currentChara; // → японское имя: "夏生"
store.currentName;  // → отображаемое имя: "夏生" (disp? : name)
store.currentText;  // → последний текст
```

**Отличие:**
- `currentChara` = `currentInfo.name` — всегда оригинальный ключ (японское имя из сценария)
- `currentName` = `currentInfo.disp || currentInfo.name` — локализованное отображаемое или имя

---

## 4. `getAliasName(name)` (преобразование имени)

```javascript
kag.getAliasName("夏生"); // → "夏生"
```

Вызывается из `rendermsgwin.tjs` для получения отображаемого имени. Если в `characterVoiceInfo[name].alias` пусто — возвращает оригинал.

---

## 5. `nameLayer` (текущий слой имени)

Из `MessageLayer`:

```javascript
kag.messageLayer.name;        // → текущее имя (строка)
kag.messageLayer.nameLayer;   // → NameLayer (объект)
```

---

## 6. Conductor context (текущий тег/сцена)

```javascript
kag.conductor.getAliasName(); // → текущий алиас из сценария
kag.conductor.lastTagName;    // → имя последнего тега
```

---

## 7. `dispNameFilter` (глобальный хук)

Вызывается из `rendermsgwin.tjs` при каждом отображении имени. Можно перехватить:

```javascript
dispNameFilter = function(name) {
    Debug.message("Display name: " + name);  // логируем
    return name;  // или подменяем
};
```

---

## 8. По image path (nameImageMap)

Если `nameImageMap` установлена как функция:

```javascript
messageLayer.owner.nameImageMap = function(name) {
    // name = японское имя
    // вернуть путь к изображению или undefined для text fallback
    return "fgimage/names/char_" + getEnglishName(name) + ".png";
};
```

Или как словарь:
```javascript
messageLayer.owner.nameImageMap = {
    "夏生": "fgimage/names/natsuki_en.png",
    "アトリ": "fgimage/names/atri_en.png",
};
```

---

## Приоритет идентификации (fallback chain)

Для определения персонажа в любой момент:

```javascript
function identifySpeaker() {
    // 1. Текущее имя из MessageLayer
    var name = kag.messageLayer.name;
    if (name && name != "") {
        // 2. Попробовать voice prefix
        var info = characterVoiceInfo[name];
        if (info && info.file) {
            return { id: info.file, jpName: name, voicePrefix: info.file };
        }
        return { id: name, jpName: name };
    }

    // 3. Из backlog
    var store = kag.backlogWindow._historyTextStore;
    if (store) {
        var chara = store.currentChara;
        if (chara) {
            var info = characterVoiceInfo[chara];
            return {
                id: info ? info.file : chara,
                jpName: chara,
                voicePrefix: info ? info.file : null
            };
        }
    }

    return null;
}
```

---

## Детальный flow: от PSB до отображаемого имени

### 1. PSB → байткод → записи (C++ уровень)

```
vol1/b101.ks.scn (MDF/PSB)
  ↓
PSB парсинг:
  - Header (0x00-0x27): сигнатура, StrOffPos, ResOffPos, ...
  - Байткод TJS2: от ResIndexTree(0x24) до StrOffPos
  - String Table: ID → UTF-8 строки (2412 строк в b101)
  ↓
Массив опкодов в байткоде:
  0x0D..0x14 = Array(тип_размера, count, elemSize, data...)
  ↓
Структура записей — пары массивов:
  Array(1-байт, N) = коды тегов   (46 значений: 0x00..0x41)
  Array(2-байт, N) = ID строк     (текст, имена, голос)
  ↓
C++ KAGParser конвертирует числовые коды → строки тегов
(маппинг: "character", "voice", "ch", "language", ...)
```

### 2. Извлечение тега → dispatch (TJS уровень)

```
KAGParser → kag.conductor.onTagExtract(tagParams, tagNameString)
  ↓
LineModeEx.checkTagExtract  [LineModeEx.tjs:2346-2404]
  │
  ├─ tagName == "case"     → checkEnd, enqueue
  ├─ tagName == "language" → this.parseLanguage(tagParams)
  │                          this.ignoreCrToNext = 1
  ├─ tagName == "same"     → this.showSame = 1
  ├─ tagName == "r"        → this.ignoreCrToNext = 0
  └─ иначе                 → LineMode.checkTagExtract (базовый класс)
```

### 3. `parseLanguage` — занесение имени в charaInfo

```javascript
function parseLanguage(arg) {
    var dict = new Dictionary();      // chara info
    this.chara = dict;
    if (arg.name != void)
        dict.disp = arg.name;        // display name = name
    if (arg.language == void)
        arg.language = new Array();  // инициализация
    arg.language[(int)arg.type] = this.chara;  // chara[languageType] = dict
    this.afterName = 1;
}
```

**Пример занесения из PSB:**
```
[language type=1 name="Natsuki"]
  → arg.language[1] = { disp: "Natsuki" }

[language type=0 name="夏生"]
  → arg.language[0] = { disp: "夏生" }
```

После этого `charaInfo` выглядит так:
```javascript
charaInfo = {
    name: "夏生",              // японское имя (из [ch name="夏生"])
    disp: "夏生",              // display name из basic
    language: {
        0: { name: "夏生", disp: "夏生" },   // язык 0 = JP
        1: { name: "Natsuki", disp: "Natsuki" },  // язык 1 = EN
        2: { name: "夏生", disp: "夏生" }    // язык 2 = CN
    }
}
```

### 4. Рендеринг имени — `drawName` [rendermsgwin.tjs:185-220]

```
rendermsgwin.drawName(charaInfo)
  │
  ├─ 1. getLangInfo(charaInfo, "disp")
  │     │
  │     ├─ charaInfo.language?.kag.languageType?.disp
  │     │     → если kag.languageType=1: language[1]["disp"] = "Natsuki"
  │     │     → если kag.languageType=0: language[0]["disp"] = "夏生"
  │     │
  │     ├─ fallback: charaInfo.disp (если language не заполнен)
  │     └─ fallback: "" (void)
  │
  ├─ 2. Если getLangInfo вернул "" → charaInfo.name
  │     → "夏生" (оригинал из KAG)
  │
  ├─ 3. Если name == "" → getAliasName(name)
  │     → characterVoiceInfo[name].alias
  │
  ├─ 4. dispNameFilter(name) [custom.tjs]
  │     → Steam EN: function dispNameFilter(name) { return name; }  // NO-OP
  │     → Яп.оригинал: не определена
  │
  └─ 5. Отображаемое имя
```

### 5. Выбор по языку — ключевые точки

| Шаг | Где | Как выбирается язык |
|-----|-----|---------------------|
| `kag.languageType` | глобально | 0=JP, 1=EN, 2=CN |
| `CurrentLanguageTag` | multilang.tjs | `LanguageTags[kag.languageType]` → `"jp"`/`"en"`/`"cn"` |
| `language[type]` | parseLanguage | `arg.language[(int)arg.type]` — заносится из PSB |
| `language[kag.languageType]` | getLangInfo | чтение по текущему языку |
| `dispNameFilter` | custom.tjs | финальный фильтр (NO-OP в Steam) |

### 6. Сводная схема

```
PSB .ks.scn
  │
  ├─ String Table ──── ID строк (имя, текст, голос — все языки вместе)
  │
  ├─ Байткод ──────── пары массивов (тип_тега : ID_строки)
  │                       │
  │                       └─ [type=1 name="Natsuki"]
  │                          [type=0 name="夏生"]
  │                              │
  ▼                              ▼
LineModeEx.parseLanguage → arg.language[type] = dict
                              │
                              ▼
rendermsgwin.drawName → getLangInfo → language[kag.languageType]
                              │
                              ▼
                       dispNameFilter (NO-OP в Steam)
                              │
                              ▼
                       Отображаемое имя
```

## Сводная таблица методов идентификации

## Сводная таблица

| Метод | Где определён | Возвращает | Язык |
|-------|--------------|-----------|------|
| `characterVoiceInfo[name]` | `LineModeEx.tjs` | `{file, config, per, alias}` | Яп → данные |
| `currentChara` | `backlog.tjs` | японское имя | Яп |
| `currentName` | `backlog.tjs` | `disp \|\| name` | Локализ. \| Яп |
| `messageLayer.name` | `MessageLayer.tjs` | текущее имя (яп, если не подменено) | Зависит |
| `getAliasName(name)` | `MainWindow.tjs` → `LineModeEx` | alias или имя | Яп |
| `getLangInfo(charaInfo, field)` | `MainWindow.tjs` | локализ. поле или оригинал | Локализ. |
| voice file prefix | `charvoice.csv` | 3 буквы (`atr`) | Языконез. |
| `dispNameFilter(name)` | глобальная | любое | Настраив. |
