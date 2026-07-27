# PSB Format Reference

PSB — формат упаковки сценариев KiriKiri-Z (.ks.scn которые загружаются через psbfile.dll). Содержит строки, ресурсы и байткод KAG.

## Общая структура PSB v3

```
PSB Header (0x28 bytes)
├── Signature   (4 bytes)  "PSB\0"
├── Version     (4 bytes)  1/2/3
├── Padding     (8 bytes)
├── StrOffPos   (4 bytes)  Offset -> таблица смещений строк
├── StrDataPos  (4 bytes)  Offset -> данные строк (null-terminated UTF-8)
├── ResOffPos   (4 bytes)  Offset -> таблица смещений ресурсов
├── ResLenPos   (4 bytes)  Offset -> таблица размеров ресурсов
├── ResDataPos  (4 bytes)  Offset -> хэш-дерево ресурсов (ResIndexTree)
│
├── String Offset Table      (начинается с StrOffPos)
├── String Data Area         (начинается с StrDataPos)
├── Resource Offset Table    (начинается с ResOffPos)
├── Resource Length Table    (начинается с ResLenPos)
├── Resource Index Tree      (начинается с ResDataPos)
└── Bytecode                 (начинается с offset 0x24)
```

## String Table (таблица строк)

### Offset Table
```
CountSize (1 byte):  0x0D=1, 0x0E=2, 0x0F=3, 0x10=4
Count     (CountSize bytes): количество строк
OffSize   (1 byte):  0x0D=1, 0x0E=2, 0x0F=3, 0x10=4
Offsets[] (OffSize bytes each): смещения относительно StrDataPos
```

### String Data
Null-terminated UTF-8 строки, расположенные по Offsets[i] относительно StrDataPos.

## Bytecode (байткод ссылок на строки)

Байткод начинается с offset 0x24. Длина: `StrOffPos - 0x24`.

### Опкоды для строковых ссылок

| Байт | Размер ID | Описание |
|------|-----------|----------|
| 0x15 | 1 байт    | Ссылка на строку (ID 0-255) |
| 0x16 | 2 байта   | Ссылка на строку (ID 0-65535) |
| 0x17 | 3 байта   | Ссылка на строку (ID 0-16777215) |
| 0x18 | 4 байта   | Ссылка на строку (ID 0-4294967295) |

### Другие опкоды

| Байты | Тип |
|-------|-----|
| 0x00-0x03 | Константы (void, integer, real, string) |
| 0x04-0x0C | Числа (размер = байт - 0x04) |
| 0x0D-0x14 | Массивы |
| 0x1D | void |
| 0x1E | 4-байтовое embedded |
| 0x1F | 8-байтовое embedded |
| 0x19-0x1C | Ссылки на встроенный контент |
| 0x20 | Вложенный блок (рекурсивный обход) |
| 0x21 | Двойной вложенный блок |

## Получение строк в порядке байткода (bytecode order)

Алгоритм (см. `Tools/psb-analyze.ps1`):

1. Прочитать таблицу строк (Strings[])
2. Обойти байткод, собирая ID строк в порядке появления
3. Дедуплицировать: каждый ID добавляется только при первом вхождении
4. Добавить неиспользованные строки в конец (для совместимости с ScnEditorGUI)

Результат: массив строк с ID, совпадающими с ScnEditorGUI.

## Особенности ATRI

### Оригинальная версия (vol1)
- PSB v3, одна таблица строк со всеми языками (JP, EN, ZH-CN, ZH-TW)
- Строки диалогов идут вперемешку по языкам: JP→EN→CN→TW (по 4 строки на одно высказывание)
- Voice cues (формата `PREFIX_scene_NNN`) имеют отдельные ID в байткоде
- Имена персонажей и voice cues идут в порядке появления в KAG-сценарии
- Всего ~2400 строк для b101.ks.scn

### Русская версия (patch_ru)
- PSB v3, другая структура: строки — это метаданные (имена ресурсов, BGM)
- Встроенные .ks-ресурсы для каждого языка (b101.ks, b101_en.ks, etc.)
- Первые строки:
  - ID 0: MD5-хэш (e6ab11cd064648af640b6f0b76fe8485)
  - ID 1-4: имена встроенных .ks-файлов локализации
  - ID 5: *start (маркер сценария)
  - ID 6-7: bgm/BGM01 (метаданные музыки)
- Всего ~1974 строк для b101.ks.scn

### Пример ID в оригинальном b101.ks.scn (vol1)

| ID (ScnEditorGUI) | Строка | Тип |
|----|--------|------|
| 404 | 夏生 | JP name |
| 406 | Natsuki | EN name |
| 504 | キャサリン | JP name |
| 505 | 女の声 | JP name |
| 506 | Woman's Voice | EN name |
| 508 | Catherine | EN name |
| 516 | CAT_b101_001 | Voice cue |
| 523 | Catherine | EN name |
| 577 | 打ち上げ中継 | JP name |
| 578 | 朝のラジオ | JP name |
| 580 | Newsreader | EN name |
| 653 | 水菜萌 | JP name |
| 654 | 誰かの声 | JP name |
| 656 | Someone's Voice | EN name |
| 668 | Minamo | EN name |

## Инструменты для работы с .ks.scn (PSB v2/v3)

### Сравнительный анализ инструментов

| Инструмент | Язык | PSB v2 | PSB v3 (ATRI) | Декомпил → JSON | Компил JSON → PSB | Текст | Лицензия |
|---|---|---|---|---|---|---|---|
| **FreeMote** | C# (.NET) | ✅ | ✅ | `PsbDecompile.exe` | `PsBuild.exe` | через `parse_script.py` | CC BY-NC-SA 4.0 |
| **psbfile** | C++ | ✅ | ✅ | `decompiler.exe` | `pcc.exe` | ❌ (полный JSON) | MIT |
| **psbtool_py** | Python | ? | ? | ❌ | ❌ | `unpack`/`pack` строк в CSV | GPLv3 |
| **KrkrExtract** | C++ | ✅ | ✅ | Есть (тот же код что psbfile) | Через UI | ❌ | GPLv3 |
| **KrKrSceneManager** | C# | ✅ | ? | GUI `ScnEditorGUI.exe` | ❌ | Дамп строк | — |

### Рекомендация для ATRI

**FreeMote** — основной инструмент. Работает с PSB v3 (ATRI). Полный цикл: `.ks.scn` → JSON → редактирование → `.ks.scn` (через `PsBuild`).

Резерв: **psbfile** (`decompiler.exe`) даёт полный JSON без .NET, если нужен быстрый дамп.

### ATRI multi-lingual структура

ATRI `.ks.scn` (из `vol1.xp3`) использует **PSB v3** (сигнатура `PSB\0`, версия `0x0003`). При декомпиляции FreeMote/psbfile дают JSON со следующими корневыми ключами:

```json
{
  "hash": "e6ab11cd...",
  "name": "b101.ks",
  "llmap": {
    "*start": [3, 5, 7, ...],
    ...
  },
  "outlines": [...],
  "scenes": [
    {
      "firstLine": 6,
      "label": "*start",
      "lines": [...],         // полный массив KAG-команд сцены
      "texts": [...],         // мультиязычные диалоги
      "title": ["JP", "EN", "ZHS", "ZHT"]
    }
  ]
}
```

#### texts[n] массив

Каждый entry = `[character, display_name, translations, voice, line_index, tag_info]`:

- **`character`** (`string | null`): имя персонажа. `null` = нарратор
- **`display_name`** (`string | null`): отображаемое имя
- **`translations`** (`[[lang_id, text], ...]`): мультиязычный текст
  - `[0][1]` = японский
  - `[1][1]` = английский
  - `[2][1]` = упрощённый китайский
  - `[3][1]` = традиционный китайский
  - Где `[N][0]` всегда `null`, `[N][1]` — строка текста
- **`voice`** (`dict | null`): `{"voice": "filename"}` или `null`
- **`line_index`** (`int`): индекс в `lines[]`
- **`tag_info`** (`dict`): оригинальные KAG-параметры тега

#### llmap

`llmap` — карта `label → [line_numbers]`, где line_number — индекс в `scenes[].lines[]`.

Пример:
- `"*start": [3, 5, 7, 9, ...]` — сцена `*start` начинается со строки 3 массива `lines`

### Инструменты для ATRI (подробно)

#### 1. FreeMote — рекомендован
- `F:\Users\User\Projects\FreeMote`
- **Декомпиляция**: `PsbDecompile.exe file.ks.scn` → `file.ks.json` + `file.ks.resx.json`
- **Компиляция**: `PsBuild.exe -i file.ks.json -o file.ks.scn`
- Поддерживает PSB v2 и v3 (ATRI)
- 19 плагинов: TLG, LZ4, WebP, VGAudio, Zstd
- .NET 4.8, VS 2022
- **Ограничение**: лицензия CC BY-NC-SA 4.0 (некоммерческая)

#### 2. psbfile — резерв
- `F:\Users\User\Projects\psbfile`
- `binary/decompiler.exe` ↔ `_psb_tools/decompiler.exe` (идентичны, хэши совпадают)
- **Декомпиляция**: `decompiler.exe file.ks.scn` → `file.ks.scn.json` (полное дерево) + `file.ks.scn.res.json` (ресурсы)
- **Компиляция**: `pcc.exe` (компилятор PSB)
- Доп. утилиты: `emote_conv.exe`, `mdftool.exe`, `rl_convert.exe`, `expimg.exe`
- Написан на C++ (VS), использует jsoncpp, zlib, lz4
- Исходный код от asmodean, автор number201724 (MIT)

#### 3. psbtool_py — для извлечения/вставки строк
- `F:\Users\User\Projects\psbtool_py`
- Python, извлекает строки `.ks.scn` → CSV и обратно
- Зависимость: **filetranslate** (`F:\Users\User\Projects\filetranslate`) — библиотека для CSV-таблиц
- `psb_tool.py`: `unpack` (извлечь строки в CSV), `pack` (вставить обратно)
- Для массового перевода/замены текста

#### 4. atri_extract_toolbox — готовый пайплайн для ATRI
- `F:\Users\User\Projects\atri_extract_toolbox`
- `parse_script.py`: парсит JSON от FreeMote → TSV (роль | яп | перевод | голос)
- `batch_convert.py`: конвертация opus/ogg через ffmpeg
- README сводит воедино весь процесс: GARbro (xp3) → FreeMote (PSB→JSON) → parse_script.py (текст)
- В `texts[][2][language][1]`: 0=JP, 1=EN, 2=ZHS, 3=ZHT

#### 5. KrkrExtract — xp3 + PSB (избыточно)
- `F:\Users\User\Projects\KrkrExtract`
- Основное: распаковка/упаковка xp3 архивов (GPLv3)
- `PsbFile.cpp` — тот же код что psbfile (от asmodean с модификациями)
- Есть `EMoteDumper.exe`, `tjs2Compiler.exe`, `tjs2Decompiler.exe`
- Не поддерживается (форк: KrkrzExtract)
- Для PSB лучше использовать FreeMote или psbfile напрямую

### Пути

```
F:\Users\User\Projects\FreeMote\bin\PsbDecompile.exe
F:\Users\User\Projects\FreeMote\bin\PsBuild.exe
F:\Users\User\Projects\psbfile\binary\decompiler.exe
F:\Users\User\Projects\psbfile\binary\pcc.exe
F:\Users\User\Projects\psbtool_py\psbtool_py\psb_tool.py
F:\Users\User\Projects\atri_extract_toolbox\parse_script.py
F:\Users\User\Projects\atri_extract_toolbox\batch_convert.py
F:\Users\User\Projects\filetranslate
F:\Users\User\Projects\KrkrExtract\KrkrExtract\KrkrExtract.Core\PsbFile.cpp
```
