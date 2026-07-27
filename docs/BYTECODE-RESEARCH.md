# TJS Bytecode Research (2026-07-27)

## Распакованные data.xp3

`D:\Games\ATRI-TestVersion\new-extract-data` — все `.tjs` файлы из data.xp3.

| Тип | Кол-во | Назначение |
|-----|--------|------------|
| Текстовые (UTF-16LE) | ~40 | UI, конфиги, сценарии, переходы, шрифты |
| Скомпилированные (байткод `TJS2`) | ~196 | Движок (Conductor, Layers, BGM/SE/Voice, KAGEnv и т.д.) |

Текстовые читаются напрямую и уже проанализированы в `docs/tjs-hooks/`.

Скомпилированные требуют дизассемблера/декопилятора.

---

## Инструмент: `crate-1556/tjs2-decompiler` (Python) — основной

`F:\Users\User\Projects\tjs2-decompiler`

Python-тул с полным CFG-анализом (control flow graph + region tree + dominators).  
Декомпиляция на уровне, близком к исходному коду.

**Ключевые особенности:**
- CFG + dominator tree → структурное восстановление if/while/switch/try
- Реставрация `extends`, `super.`, default-параметров, property getter/setter
- `else if` слияние, нормализация catch-переменных к `e`
- Pretty-print с переносом строк >120 символов
- Output: UTF-16LE BOM (стандарт TJS2) или utf-8, shift_jis, gbk

**Критическое преимущество:** проверен на kag3/system, Krkr2Compat, KAGEX3/system и полной игре — декомпилированные скрипты **исполняемы**.

### Использование

```powershell
# Одиночный файл
python tjs2_decompiler.py input.tjs -o output.tjs

# Директория (рекурсивно, с сохранением структуры)
python tjs2_decompiler.py input_dir/ -r -o output_dir/

# Кодировка
python tjs2_decompiler.py input.tjs -o output.tjs -e utf-16le-bom
```

### Батчевая декомпиляция ATRI

Скрипт: `Tools\decompile_tjs.ps1`

```powershell
.\Tools\decompile_tjs.ps1                    # с дефолтными путями
.\Tools\decompile_tjs.ps1 -Force             # перезаписать всё
```

Результат (`D:\Games\ATRI-TestVersion\data-tjs-decompiled\`):

| Директория | Файлов |
|------------|--------|
| `root/` | 1 |
| `dmmcloud/` | 2 |
| `k2compat/` | 11 |
| `sysscn/` | 81 |
| `system/` | 101 |
| `scenario/scripts/` | 3 |
| `scenario/transitions/` | 23 |
| **Всего** | **196/236** (40 текстовых пропущены) |

### Пример качества декомпиляции

Conductor.tjs (сравнение с Rust-версией):

| Аспект | Python | Rust (tjs2dec) |
|--------|--------|----------------|
| `extends` | `class Conductor extends BaseConductor` | `this.Conductor = Conductor;` |
| `switch/case` | ✅ `switch (arg1) { case -5: ... break; }` | if-else цепочки |
| `try/catch(e)` | ✅ чистый блок | goto-подобные переходы |
| `property` | `property interrupted { getter() ... setter() ... }` | Размазанные функции |
| `super.Exception()` | ✅ восстановлено | Нет |
| Default params | `run(arg0 = 0)` | Нет |
| `incontextof` | `(Dictionary.assign incontextof waitUntil)(...)` | Нет |
| `extends` chain | ✅ Conductor → BaseConductor → KAGParser | Нет |
| Switch fallthrough | ✅ `pendings.insert(0, arg2)` (без `break`) | Нет |

---

## Инструмент: `tjs2dec` (Rust) — запасной

`F:\Users\User\Projects\tjs2Decompiler\target\release\tjs2dec.exe`

Rust-тул с SSA/HLIR анализом.

### Подкоманды

| Команда | Описание |
|---------|----------|
| `tjs2dec disasm file.tjs` | Дизассемблирование: мнемоники VM с аннотациями |
| `tjs2dec tjs file.tjs` | Декомпиляция в TJS |
| `tjs2dec ssa file.tjs` | SSA-форма для dataflow анализа |
| `tjs2dec ssa --hlir file.tjs` | HLIR (High-Level IR) |
| `tjs2dec emit-tjs file.tjs` | Генерация исполняемого TJS |

### Примеры

```powershell
& "F:\Users\User\Projects\tjs2Decompiler\target\release\tjs2dec.exe" tjs "file.tjs" 2>&1
& "F:\Users\User\Projects\tjs2Decompiler\target\release\tjs2dec.exe" disasm "file.tjs" 2>&1
```

---

## Сравнение инструментов

| Характеристика | Старый (`tjsdisasm`) | Rust (`tjs2dec`) | **Python (crate-1556)** |
|----------------|---------------------|-------------------|------------------------|
| Язык | C++ | Rust | Python |
| Декомпиляция | ❌ | ✅ (`tjs` subcmd) | ✅ (основной режим) |
| CFG + dominators | ❌ | SSA/HLIR | ✅ region tree |
| `extends` / `super` | ❌ | ❌ | ✅ |
| default params | ❌ | ❌ | ✅ |
| pretty-print | ❌ | мин. | ✅ 120-col + else-if |
| Batch | ❌ | ❌ | ✅ встроенный + скрипт |
| switch/try | ❌ | ❌ | ✅ |
| Executable output | ❌ | `emit-tjs` | ✅ (проверено на игре) |

**Вывод:** **Python-версия предпочтительна** для чтения и анализа. Rust-версию используем если нужен SSA/HLIR или emit-tjs.

---

## Декомпилированные исходники (для сравнения)

- `D:\ProjectsD\krkrz_dev\script\KAG3\data\system\` — 21 файл
- `D:\ProjectsD\KAGeXpress\Sample\system\` — 29 файлов

Системные `.tjs` в ATRI — модифицированные версии. Исходники из репозиториев — референс, но не точная копия.

---

## Ключевые события Conductor (из байткода)

Декомпиляция `system\Conductor.tjs` подтверждает все события:

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
| Stop | `onStop(arg0)` | `owner.onConductorStop()` |

### Дополнительно подтверждено

- `Conductor.status`: 0 = mStop, 1 = mRun, 2 = mWait
- `Conductor.lastTagName` = `tagName` в `onTag()`, сразу после вызова `handlers[tagName](arg0)`
- `BaseConductor` → `KAGParser` (через `SuperConstructor` lookup)
- `interrupted` property: getter = `_interrupted`, setter = управление timer/oneshot
- Полный цикл `timerCallback()`: `while ((tag = getNextParseTag()) !== void) { onTag(tag); ... }`

### Важно о хуках

- `onBeforeTag()` → `owner.onConductorBeforeTag()` → `kag.forEachFunctionHook("onBefore", ...)` — это **C++ ncbind**, переопределить из TJS нельзя
- `onUnknownTag()` → `owner.onConductorUnknownTag()` → `kag.forEachFunctionHook("onUnknown", ...)` — тоже C++ ncbind
- `onLabel()` → `owner.onConductorLabel()` — **можно переопределить**, это обычный TJS метод
- `onScenarioLoaded()` → `owner.onConductorScenarioLoaded()` — **можно переопределить**
- См. `docs/kirikiri/Hook-Mechanisms.md`
