# ImGui Object Inspector - Документация

## Обзор

ImGui Object Inspector - инструмент для исследования объектов игры KiriKiri-Z в реальном времени через наложенный UI.

## Архитектура

### Компоненты

| Файл | Назначение |
|------|------------|
| `ImGuiInit.cpp/h` | DX9 hook, инициализация/shutdown ImGui |
| `ImGuiWindows.cpp/h` | UI панели Object Inspector, Variables Panel |
| `dllmain.cpp` | TJS helper функции |

### Поток выполнения

```
DX9 EndScene (hook)
    │
    ▼
hkEndScene()
    │
    ├── InitImGuiHook() - первый вызов
    │
    ├── Проверка F12 - toggle ImGui
    │
    └── ImGui_NewFrame()
            │
            ▼
        RenderImGuiWindows()
            ├── RenderObjectInspector()
            └── RenderVariablesPanel()
```

## Object Inspector

### Структура данных

```cpp
struct ObjectNodeData {
    std::string name;           // Отображаемое имя
    std::string fullPath;       // Путь в TJS (e.g., "global")
    bool expanded = false;       // Состояние раскрытия
    int loadedKeys = 0;         // Загружено ключей
    int totalKeys = 0;          // Всего ключей
};
```

### Текущие объекты

```cpp
static ObjectNodeData g_ObjectNodes[] = {
    {"global", "global", false, 0, 0}
};
```

### Возможности

- **Lazy Loading** - загрузка ключей по 30 за раз
- **Поиск** - фильтрация по имени
- **Always Reload** - принудительная перезагрузка при открытии
- **Кнопка More...** - загрузка следующих 30 ключей

### Рендеринг ключей

```
1. Раскрытие узла → neuroImGuiGetKeys()
2. Получение ключей из TJS
3. Разбиение по '\x0A'
4. Отображение в ImGui списке
```

## Variables Panel

### Переменные

| Тип | Описание | Источник |
|-----|----------|----------|
| `f` | Temporary flags | Временные переменные |
| `tf` | Temporary game flags | Игровые переменные |
| `sf` | System flags | Системные переменные |

### Рендеринг

```
1. Нажатие Refresh → neuroImGuiGetFlags(type)
2. Получение ключей переменной
3. Отображение в TreeNode
```

## TJS Helper Функции

### neuroImGuiGetKeys

```cpp
extern "C" __declspec(dllexport) const char* neuroImGuiGetKeys(const char* objectName, int offset, int count);
```

**Параметры:**
- `objectName` - путь к объекту (e.g., "global")
- `offset` - смещение (не используется, загружает все)
- `count` - количество (не используется)

**Возвращает:** ключи через `\x0A`

**TJS код:**
```tjs
Scripts.getObjectKeys(global).join('\x0A')
```

### neuroImGuiGetFlags

```cpp
extern "C" __declspec(dllexport) const char* neuroImGuiGetFlags(const char* flagType);
```

**Параметры:**
- `flagType` - "f", "tf" или "sf"

**Возвращает:** ключи переменной через `\x0A`

### neuroImGuiGetKeysCount

```cpp
extern "C" __declspec(dllexport) int neuroImGuiGetKeysCount();
```

**Возвращает:** количество ключей (глобальная переменная)

## Конвертация строк

```cpp
static std::string TJSStringToStdString(const ttstr& tjsStr) {
    // UTF-16 → UTF-8
    int requiredSize = WideCharToMultiByte(CP_UTF8, ...);
    // Конвертация...
}
```

## Thread Safety

**КРИТИЧНО:** TJS не потокобезопасен!

ImGui может рендериться в контексте DX9 потока. Все TJS вызовы идут через `ExecuteTJSSync()`:

```cpp
if (ExecuteTJSSync(wcode, &result, 10000)) {
    // Результат в result
}
```

`ExecuteTJSSync` использует Window Subclass для безопасного вызова TJS из любого потока.

## Ограничения

1. **Нет определения типов** - ключи отображаются белым цветом
2. **Все ключи загружаются сразу** - lazy loading с offset не реализован
3. **Нет дочерних узлов** - вложенные объекты не отображаются
4. **Нет tooltip'ов** - значения не показываются

## Планы улучшений

### 1. Incremental Type Detection
- Проверять типы по 10/кадр
- Покраска по типу (function=синий, object=жёлтый)
- Кэш типов в KeyInfo структуре

### 2. Lazy Loading с Offset
- Загрузка только видимых ключей
- Прокрутка с подзагрузкой

### 3. Дочерние узлы
- TreeNode для объектов
- Загрузка вложенных ключей

### 4. Tooltips
- Показ значений при наведении
- Копирование значения

## Отладка

### Логирование

```cpp
LogToFile("[ImGui] neuroImGuiGetFlags calling ExecuteTJSSync\n");
LogToFile("[ExecuteTJSSync] Running on main thread\n");
```

Файл: `plugin_log.txt`

### Частые проблемы

| Проблема | Причина | Решение |
|----------|---------|---------|
| Syntax Error | Сложный TJS синтаксис | Упростить код |
| Японские кракозябры | Неправильная кодировка | CP_UTF8 в WideCharToMultiByte |
| Пустые переменные | TJS не готов | Проверить g_TJSReady |

## Горячие клавиши

| Клавиша | Действие |
|---------|----------|
| F12 | Toggle ImGui windows |
