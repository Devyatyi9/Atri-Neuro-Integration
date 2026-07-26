# System / Debug — Системные глобальные объекты

Глобальные объекты `System` и `Debug` предоставляют доступ к среде выполнения: пути, ОС, реестр, отображение, диалоги, логирование и консоль.

---

## System

| Свойство | Тип | Описание | Neuro |
|----------|-----|----------|-------|
| `title` | string | Заголовок окна игры | ✅ отслеживание сцены |
| `exePath` | string | Путь к исполняемому файлу | ✅ определение корня игры |
| `exeName` | string | Имя exe-файла | |
| `dataPath` | string | Рабочая директория данных | ✅ доступ к ресурсам |
| `appDataPath` | string | Путь к AppData/Roaming | ✅ сохранения/конфиги |
| `personalPath` | string | Путь к My Documents | |
| `savedGamesPath` | string | Путь к Saved Games игры | ✅ расположение сейвов |
| `osName` | string | Название ОС (например `"Windows 10"`) | ✅ логи совместимости |
| `platformName` | string | Платформа (`"win32"`) | |
| `processorNum` | int | Количество логических процессоров | |
| `getCPUType()` | string | Тип CPU (например `"x86"`) | |
| `getOSVersion()` | string | Версия ОС (например `"10.0.19045"`) | ✅ детализация окружения |
| `versionString` | string | Версия движка KiriKiri | |
| `versionInformation` | string | Подробная информация о версии | |
| `getAboutString()` | string | Описание приложения | |
| `getTickCount()` | int | Время работы системы (мс) | |
| `screenWidth` | int | Ширина экрана | ✅ определение разрешения |
| `screenHeight` | int | Высота экрана | |
| `desktopWidth` | int | Ширина рабочего стола | |
| `desktopHeight` | int | Высота рабочего стола | |
| `desktopLeft` | int | Левый край рабочего стола | |
| `desktopTop` | int | Верхний край рабочего стола | |
| `getDisplayMonitors()` | array | Массив с информацией о мониторах | ✅ multi-monitor |
| `getMonitorInfo()` | object | Информация о текущем мониторе | |
| `getSystemMetrics(index)` | int | Системные метрики (WinAPI) | |
| `getDoubleClickTime()` | int | Время двойного клика (мс) | |
| `getKeyState(vkCode)` | bool | Состояние клавиши (нажата/нет) | ✅ хоткеи |
| `getCursorPos()` | object | `{x, y}` позиция курсора | |
| `setCursorPos(x, y)` | void | Установить позицию курсора | |
| `inputString(prompt[, default])` | string | Диалог ввода строки | ✅ получение текста от Neuro |
| `confirm(message)` | bool | Диалог подтверждения (OK/Отмена) | |
| `inform(message)` | void | Информационный диалог | |
| `openURL(url)` | void | Открыть URL в браузере | ✅ открытие ссылок |
| `shellExecute(op, file[, params])` | void | ShellExecute (открыть/печать/...) | |
| `showVersion()` | void | Показать окно с версией | |
| `setApplicationIcon(path)` | void | Установить иконку приложения | |
| `exit()` | void | Выход из приложения | |
| `terminate(exitCode)` | void | Аварийное завершение с кодом | |
| `exitOnNoWindowStartup` | bool | Выход если нет окна при старте | |
| `exitOnWindowClose` | bool | Выход при закрытии окна | |
| `breathe()` | void | "Вздох" — сброс состояния ожидания | |
| `isBreathing()` | bool | В режиме ожидания | |
| `stayOnTop` | bool | Окно поверх всех окон | |
| `setIconicPreview(enable)` | void | Предпросмотр при сворачивании | |
| `onActivate` | callback | Вызывается при активации окна | ✅ отслеживание фокуса |
| `onDeactivate` | callback | Вызывается при деактивации окна | |
| `eventDisabled` | bool | Блокировка обработки событий | |
| `createAppLock(name)` | void | Создать блокировку приложения | |
| `appLockKey` | string | Ключ блокировки | |
| `waitForAppLock(name[, timeout])` | bool | Ожидание снятия блокировки | |
| `createUUID()` | string | Генерация UUID | ✅ уникальные ID |
| `getArgument(index)` | string | Аргумент командной строки | |
| `setArgument(index, value)` | void | Установить аргумент командной строки | |
| `expandEnvString(str)` | string | Развернуть `%VARIABLES%` в строке | |
| `readEnvValue(name)` | string | Чтение переменной окружения | ✅ проверка окружения |
| `writeEnvValue(name, value)` | void | Запись переменной окружения | |
| `readRegValue(key, name)` | string | Чтение реестра Windows | |
| `writeRegValue(key, name, value)` | void | Запись реестра Windows | |
| `urldecode(str)` | string | URL-decoding | ✅ обработка данных |
| `urlencode(str)` | string | URL-encoding | |
| `toActualColor(color)` | int | Преобразование цвета в формат движка | |
| `setDpiAwareness(enable)` | void | DPI awareness | |
| `graphicCacheLimit` | int | Лимит кэша графики (байты) | |
| `clearGraphicCache()` | void | Очистка кэша графики | |
| `drawThreadNum` | int | Количество потоков отрисовки | |
| `addFont(path)` | void | Зарегистрировать шрифт из файла | |
| `doCompact()` | void | Сборка мусора (компактизация кучи) | |
| `dumpHeap()` | void | Дамп кучи в лог | ✅ отладка утечек |
| `addContinuousHandler(func)` | void | Добавить непрерывный обработчик | |
| `removeContinuousHandler(func)` | void | Удалить непрерывный обработчик | |
| `assignMessage(message)` | void | Установить строку сообщения | |
| `commandExecute(cmd)` | void | Выполнить внутреннюю команду | |
| `exceptionHandler` | callback | Обработчик необработанных исключений | |
| `nullpo` | ? | (отладка) доступ к null-объекту для тестов | |
| `touchDevice` | bool | Есть ли сенсорное устройство | |
| `touchImages` | array | Изображения для сенсорного ввода | |
| `system` | Object | Ссылка на самого себя (System) | |
| `_uiloadExtraType` | string | Тип загрузки UI | |

### ModelessOwnerWindow

Окно-владелец для немодальных диалогов.

---

## Debug

| Свойство | Тип | Описание | Neuro |
|----------|-----|----------|-------|
| `EngineRev` | string | Ревизия движка KiriKiri | ✅ логирование |
| `console` | Object | Объект консоли отладки | ✅ вывод диагностики |
| `controller` | Object | Контроллер отладки (скорость/шаг) | |
| `message(msg)` | void | Вывод сообщения в лог | ✅ логирование событий |
| `notice(msg)` | void | Уведомление (акцент) | |
| `getLastLog()` | string | Последнее сообщение лога | ✅ быстрая проверка |
| `logLocation` | string | Путь к файлу лога | ✅ проверка куда пишется |
| `logToFileOnError` | bool | Писать лог в файл при ошибке | |
| `clearLogFileOnError` | bool | Очищать лог перед записью ошибки | |
| `logAsError` | bool | Режим: все сообщения как ошибки | |
| `startLogToFile(path)` | void | Начать запись лога в файл | ✅ перенаправление лога |
| `addLoggingHandler(handler)` | void | Добавить кастомный обработчик лога | ✅ перехват в Neuro |
| `removeLoggingHandler(handler)` | void | Удалить обработчик лога | |
| `finalize()` | void | Финализация объекта Debug (очистка) | |

### LogWindowPad

Окно лога (обычно вызывается из `Debug.console` или Debug UI).

| Метод | Описание |
|-------|----------|
| `print(str)` | Печать строки в окно лога |
| `clear()` | Очистка содержимого |
| `updateText()` | Принудительное обновление отображения |
| `showResults()` | Показать результаты |
| `setPos(x, y)` | Установить позицию окна |
| `setSize(w, h)` | Установить размер окна |
| `onClose()` | Обработчик закрытия окна |
| `invokeOwnerAction()` | Вызвать действие владельца |
| `finalize()` | Финализация |

---

## Применение для Neuro

### Пути и окружение
```javascript
// Определение где лежат сейвы и конфиги
System.savedGamesPath    // → "C:/Users/.../Saved Games/ATRI"
System.dataPath          // → корень игры
System.appDataPath       // → AppData/Roaming

// Проверка версии ОС для совместимости
System.getOSVersion()    // → "10.0.19045"
```

### Логирование и отладка
```javascript
// Вывод в отладочную консоль
Debug.message("Neuro: connected to chat");

// Перехват логов через кастомный обработчик
Debug.addLoggingHandler(function(msg) {
    // отправить в Neuro API
});

// Логирование в файл
Debug.startLogToFile(System.dataPath + "neuro_debug.log");

// Получение последней ошибки
var lastError = Debug.getLastLog();
```

### Диалоги и ввод
```javascript
// Запрос ввода от стримера/зрителя
var userInput = System.inputString("Say something:", "");

// Подтверждение действия
if (System.confirm("Restart scene?")) { ... }
```

### Системная информация
```javascript
// Разрешение экрана для наложения
System.screenWidth + "x" + System.screenHeight;

// Информация о мониторах
System.getDisplayMonitors();
```

### Обработчики
```javascript
// Непрерывный обработчик (вызывается каждый кадр)
System.addContinuousHandler(function() {
    // проверка очереди задач Neuro
});

// Блокировка/синхронизация
var lockId = System.createUUID();
```
