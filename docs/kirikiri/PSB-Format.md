# PSB Format Reference

PSB — формат упаковки сценариев KiriKiri-Z (.ks.scn). Содержит строки, ресурсы и байткод KAG.

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

## Инструменты

- `F:\Users\User\Projects\KrKrZSceneManager\Tools\extract-psb.ps1` — дамп всех строк в порядке таблицы
- `F:\Users\User\Projects\KrKrZSceneManager\Tools\psb-analyze.ps1` — дамп в порядке байткода (ID = ScnEditorGUI)
- `F:\Users\User\Projects\KrKrZSceneManager\ScnEditorGUI\` — GUI редактор на C# с PSBAnalyzer
- `F:\Users\User\Projects\KrKrZSceneManager\KrKrSceneManager\PSBAnalyzer.cs` — C# реализация анализатора
- `F:\Users\User\Projects\KrKrZSceneManager\KrKrSceneManager\PSBStringManager.cs` — C# импорт/экспорт строк
