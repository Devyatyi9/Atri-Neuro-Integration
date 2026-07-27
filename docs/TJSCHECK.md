# tjscheck — standalone TJS2 syntax checker

## Расположение

`D:\ProjectsD\krkrz-tjscheck\`

Копия `krkrz` (исходники KiriKiri-Z) с вырезанными модулями, не нужными для компиляции TJS2.

## Сборка

Visual Studio 2022, solution: `D:\ProjectsD\krkrz-tjscheck\vcproj\tjscheck.vcxproj`

```
MSBuild tjscheck.vcxproj /p:Configuration=Debug /p:Platform=Win32
MSBuild tjscheck.vcxproj /p:Configuration=Release /p:Platform=Win32
```

Выходной файл:
- Debug: `D:\ProjectsD\krkrz-tjscheck\tjscheck_d.exe`
- Release: `D:\ProjectsD\krkrz-tjscheck\tjscheck.exe`

## Использование

```
tjscheck file.tjs
```

- Успех: exit code 0, stdout: `OK`
- Синтаксическая ошибка: exit code 1, stderr: `Syntax error at line N: <message>`
- Файл не найден: exit code 1, stderr: `Error: cannot open 'file.tjs'`

## Поддерживаемый синтаксис

- Все конструкции TJS2: переменные, функции, классы, выражения
- Поддержка Unicode (UTF-8 на входе конвертируется в UTF-16)
- `ConstParse=true` — только компиляция, код не исполняется (синтаксическая проверка без сайд-эффектов)

## Что НЕ поддерживается

- **RegExp** (`TJS_NO_REGEXP`) — отключён, весь `tjsRegExp.cpp` исключён из сборки
  - Если в коде используется RegExp, синтаксис самого TJS будет проверен, но runtime-контекст RegExp не создаётся
- **TJS Debugger** — заглушка (не влияет на синтаксис)
- **Исполнение кода** — только `Parse()`, выполнение не производится
- **KAG теги** — только TJS2, не KAG-сценарии
- **TVPPlugin / загрузка плагинов** — не реализована, KAGParserEx/k2compat не подключены
  - Для KAG-валидации нужен полный рантайм KiriKiri, см. `KAGParserEx.dll` в директории игры
- **Фрагменты кода** — файлы ATRI без `;` в конце не являются валидным standalone TJS2:
  - `function(x){...}` — анонимная функция на верхнем уровне (должно быть `var f = function(x){...};`)
  - `%[...]` — словарный литерал на верхнем уровне (должно быть `var d = %[...];`)

## Технические детали

- База: krkrz (KiriKiri-Z) — исходники TJS2
- Из tjs2/ исключены: `tjsRegExp.cpp`, `tjsDebug.cpp` (был исключён, но добавлен обратно для символов)
- Oniguruma не компилируется (отключён через `TJS_NO_REGEXP`)
- Дополнительные файлы: `utils/uni_cp932.cpp`, `utils/cp932_uni.cpp` — для SJIS-конверсий
- `_UNICODE`, `UNICODE` определены (wchar_t символы)
- Потокобезопасность: не требуется (однопоточный синтаксический анализ)

## Сообщения об ошибках

Английские сообщения об ошибках определены в `tjs2/tjsErrorInc.h` (на основе `tjsError_jp.h` из tjsdisasm).
Часть сообщений содержит `%1` для подстановки — например `Syntax error(%1)` при синтаксической ошибке.
Если файл сообщений не загружен (пустые строки вместо текста), удалите или замените `tjsErrorInc.h`.

## Ограничения

- Не проверяет корректность KAG-тегов (только TJS2 выражения)
- Не проверяет типы и существование runtime-объектов (т.к. код не исполняется)
- Для проверки сложных `.tjs` скриптов, используемых в игре, лучше загружать через `neuroExecFile()` в рантайме

## Скомпилированные .tjs (байткод)

Файлы с сигнатурой `TJS2` (первые 4 байта) автоматически распознаются и пропускаются:

```
tjscheck.exe binary.tjs
OK (compiled bytecode)
```

Из 236 `.tjs` файлов в `new-extract-data` ATRI: **196 бинарных**, 40 текстовых.

Байткод не требует проверки синтаксиса — он уже прошёл компиляцию в движке. tjscheck только подтверждает корректную сигнатуру.

## Известные случаи

Все эти файлы в игре загружаются не как standalone-скрипты, а через eval/модульный контекст. Для проверки оберните содержимое в `var _ = ...;`.

### Анонимная функция на верхнем уровне

```tjs
// Ошибка — анонимная функция на верхнем уровне
function(x) { var a = 1; }

// OK — присвоена переменной с ;
var f = function(x) { var a = 1; };

// OK — именованная функция
function foo(x) { var a = 1; }

// OK — в выражении с ;
(function(x) { var a = 1; })();
```

Пример: `script_kirie.tjs` line 52.

### Словарный литерал `%[...]` на верхнем уровне

```tjs
// Ошибка — без ; в конце
%[
  key: "value"
]

// OK — с ;
%[key: "value"];

// OK — присвоен переменной
var d = %[key: "value"];
```

Пример: все `trans_*.tjs` (файлы переходов), `embfontlist.tjs`, `envinit.tjs`.

## Массовая проверка

```powershell
Get-ChildItem "D:\Games\ATRI-TestVersion\extract_dataxp3" -Recurse -Filter *.tjs |
  ForEach-Object {
    $r = & "D:\ProjectsD\krkrz-tjscheck\tjscheck.exe" $_.FullName 2>&1
    if ($LASTEXITCODE -eq 0) { Write-Host "OK : $($_.Name)" -Fore Green }
    else { Write-Host "ERR: $($_.Name) $($r.Split(\"`n\")[0])" -Fore Red }
  }
```

Результат на ATRI: 11 OK, 28 ERR (все ERR — фрагменты без `;` на верхнем уровне).

## Пример

```tjs
// valid.tjs
var x = 42;
if (x > 10) Debug.message("ok");
```

```tjs
// invalid.tjs
var x = ;  // syntax error
```

```
> tjscheck valid.tjs
OK
> tjscheck invalid.tjs
Syntax error at line 1: 
```
