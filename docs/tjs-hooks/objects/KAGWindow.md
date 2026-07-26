# KAGWindow — Главное окно и управление сценарием

Источник: дампы ключей `global`, `kag`, `KAGWindow` (собраны runtime).

> **Важно:** `KAGWindow` — это класс, объявленный внутри `system/MainWindow.tjs` (`class KAGWindow extends Window`). Отдельного файла `KAGWindow.tjs` не существует. Экземпляр класса доступен как глобальная переменная `kag`.

`KAGWindow` — центральный объект движка KiriKiri-Z, управляющий окном игры, сценарием (через кондуктор), слоями, аудио, skip/auto-режимами, сохранениями и системными событиями. Доступен как `kag` в глобальной области TJS.

> **Примечание:** Свойства, отмеченные 💡, особенно полезны для Neuro-sama интеграции — позволяют читать текущее состояние игры, управлять воспроизведением и реагировать на события.

---

## Состояние сценария и диалога 💡

| Свойство / Метод | Тип | Описание | Neuro |
|---|---|---|---|
| `currentStorage` | string | Текущий файл сценария (.ks) | 💡 |
| `currentLabel` | string | Текущая метка (label) в сценарии | 💡 |
| `currentPage` | string | Текущая страница текста | 💡 |
| `currentPageName` | string | Имя текущей страницы | 💡 |
| `currentDialog` | string | Текущий отображаемый диалог | 💡 |
| `currentDialogAbsolute` | string | Диалог с учётом вложенности | |
| `currentNum` | int | Номер текущей строки | |
| `currentVoice` | string | Текущий голосовой файл | |
| `currentVoiceScript` | string | Сценарий для текущего голоса | |
| `currentWithBack` | string | Текущий текст с бэклайном | |
| `clickWaiting` | bool | Ожидание клика (текст напечатан) | 💡 |
| `waitingRatio` | float | Прогресс ожидания (0.0–1.0) | |
| `voicePlayingRatio` | float | Прогресс воспроизведения голоса (0.0–1.0) | |
| `isRecollection` | bool | Режим воспоминаний (recollection) | 💡 |
| `clickCount` | int | Счётчик кликов | |
| `talkWaitCount` | int | Счётчик ожидания talk | |
| `selectLayer` | object | Слой выбора (выбор игрока) | |
| `selectShowing` | bool | Отображается ли выбор | 💡 |
| `historyLayer` | object | Слой истории | |
| `historyShowing` | bool | Отображается ли история | |
| `messageLayerHiding` | bool | Скрыт ли слой сообщений | |
| `messageFadeTime` | int | Время затухания сообщения (ms) | |
| `initialMessageLayerVisible` | bool | Видимость слоя сообщений при старте | |
| `currentMessageLayer` | object | Текущий активный слой сообщений | |
| `numMessageLayers` | int | Количество слоёв сообщений | |
| `getSpeechText()` | string | Получить текст речи персонажа | 💡 |
| `getCurrentInfo()` | object | Информация о текущей строке сценария | 💡 |
| `getCurrentRead()` | string | Текст, который уже прочитан | |
| `getRecordLabel()` | string | Записанная метка | |
| `getCurrentPageName()` | string | Имя текущей страницы | |
| `lineMode` | int | Режим строки | |

---

## Skip / Auto режимы 💡

| Свойство / Метод | Тип | Описание | Neuro |
|---|---|---|---|
| `skipMode` | int | Текущий режим skip (0=off, >0=on) | 💡 |
| `allskip` | bool | Пропуск всего (включая непрочитанное) | 💡 |
| `autoMode` | bool | Режим авто-чтения | 💡 |
| `canSkipToClickByPrimaryClick` | bool | Skip до следующего ожидания по клику | |
| `canFastSkip` | bool | Можно ли быстро пропускать | 💡 |
| `canIgnoreL` | bool | Игнорирование меток при skip | |
| `canRestore` | bool | Можно ли восстановить | |
| `canStore` | bool | Можно ли сохранить | |
| `enterAutoMode()` | void | Войти в режим авто | 💡 |
| `cancelAutoMode()` | void | Отменить авто | 💡 |
| `enterNoWait()` | void | Войти в режим без ожидания | |
| `skipToClick()` | void | Skip до следующего `@click` | 💡 |
| `skipToLabel(label)` | void | Skip до указанной метки | 💡 |
| `skipToPage()` | void | Skip до конца страницы | |
| `skipToStop()` | void | Skip до ближайшей остановки | |
| `cancelSkip()` | void | Отменить skip | 💡 |
| `setSkipMode(mode)` | void | Установить режим skip | 💡 |
| `getSkipMode()` | int | Получить режим skip | |
| `isSkipMode()` | bool | В skip режиме? | 💡 |
| `isAutoMode()` | bool | В авто режиме? | 💡 |
| `isAllSkip()` | bool | Полный пропуск? | |
| `checkCancelSkipFast()` | bool | Проверить отмену быстрого skip | |
| `checkCancelSkipUnread()` | bool | Проверить отмену skip непрочитанного | |
| `checkSkipOnReadedLabel()` | bool | Проверить skip на прочитанных метках | |
| `checkSkipToLabel(label)` | bool | Проверить skip до метки | |
| `textspeed` | int | Скорость текста | |
| `autospeed` | int | Скорость авто-режима | |
| `waitspeed` | int | Скорость ожидания | |
| `skipspeed` | int | Скорость skip | |
| `actskipspeed` | int | Фактическая скорость skip | |
| `drawspeed` | int | Скорость отрисовки | |
| `effectspeed` | int | Скорость эффектов | |
| `flipspeed` | int | Скорость flip | |
| `flipskipspeed` | int | Скорость flip при skip | |
| `transspeed` | int | Скорость переходов | |
| `userChSpeed` / `userCh2ndSpeed` | int | Скорости символов пользователя | |
| `setUserSpeed(speed)` | void | Установить скорость пользователя | |

---

## Аудио (BGM, SE, Voice) 💡

| Свойство / Метод | Тип | Описание | Neuro |
|---|---|---|---|
| `bgm` | object | Ссылка на BGM-подсистему | 💡 |
| `se` | object | Ссылка на SE-подсистему | 💡 |
| `voice` | object | Ссылка на Voice-подсистему | 💡 |
| `bgmvolume` | float | Громкость BGM (0.0–1.0) | 💡 |
| `bgmenable` | bool | BGM включён | 💡 |
| `bgvvolume` | float | Громкость BGV | |
| `bgvenable` | bool | BGV включён | |
| `bgv2volume` | float | Громкость BGV2 | |
| `bgv2enable` | bool | BGV2 включён | |
| `sevolume` | float | Громкость SE | |
| `seenable` | bool | SE включён | |
| `voicevolume` | float | Громкость голоса | 💡 |
| `voiceenable` | bool | Голос включён | 💡 |
| `voicespeed` | int | Скорость голоса | |
| `voicepan` | float | Панорама голоса | |
| `voices` | array | Массив голосовых буферов | |
| `numVoiceBuffers` | int | Количество голосовых буферов | |
| `numSEBuffers` | int | Количество SE буферов | |
| `numSysSEBuffers` | int | Количество системных SE буферов | |
| `numMovies` | int | Количество видео | |
| `bgmdownvalue` | float | Значение приглушения BGM | |
| `sedownvalue` | float | Значение приглушения SE | |
| `downbgmvolume` | float | Приглушённая громкость BGM | |
| `downsevolume` | float | Приглушённая громкость SE | |
| `bgmdown` | bool | BGM приглушён | |
| `bgmdownlevel` | int | Уровень приглушения BGM | |
| `sedown` | bool | SE приглушён | |
| `sedownlevel` | int | Уровень приглушения SE | |
| `isVoicePlaying()` | bool | Воспроизводится ли голос? | 💡 |
| `isVoiceWait()` | bool | Ожидание голоса? | 💡 |
| `isVoiceCut()` | bool | Голос обрезан? | |
| `hasCurrentVoice()` | bool | Есть текущий голос? | |
| `getVoicePlayingState()` | int | Состояние воспроизведения голоса | 💡 |
| `getVoicePlayingScript()` | string | Сценарий воспроизводимого голоса | 💡 |
| `getPlayVoiceFilename()` | string | Имя файла текущего голоса | 💡 |
| `getVoiceTrack()` | object | Трек голоса | |
| `getPlayingVoiceTrack()` | object | Воспроизводимый трек | |
| `isChVoiceOn()` | bool | Канальный голос включён? | |
| `getChVoiceVolume()` | float | Громкость канального голоса | |
| `playVoice(filename)` | void | Воспроизвести голос | 💡 |
| `playAllVoice()` | void | Воспроизвести все голоса | |
| `playLineVoice()` | void | Воспроизвести голос строки | |
| `stopVoice()` | void | Остановить голос | 💡 |
| `stopAllVoice()` | void | Остановить все голоса | |
| `pauseVoice()` | void | Пауза голоса | |
| `resumeVoice()` | void | Возобновить голос | |
| `fadeVoice(time)` | void | Затухание голоса | |
| `systemVoicePlaying` | bool | Системный голос играет? | |
| `voicePaused` | bool | Голос на паузе? | |
| `setVoiceVolume(vol)` | void | Установить громкость голоса | |
| `setVoiceOn(on)` | void | Включить/выключить голос | |
| `clearVoiceMode()` | void | Сбросить режим голоса | |
| `setBgmLabel(label)` | void | Установить BGM-метку | |
| `clearBgmLabel()` | void | Сбросить BGM-метку | |
| `setBgmStop(target)` | void | Установить остановку BGM | |
| `clearBgmStop()` | void | Сбросить остановку BGM | |
| `getVolume(type)` | int | Получить громкость по типу | |
| `setVolume(type, vol)` | void | Установить громкость по типу | |
| `resetVolume()` | void | Сбросить громкость | |
| `updateVolume()` | void | Обновить громкость | |
| `getBGVVolume()` | int | Получить громкость BGV | |
| `getBGV2Volume()` | int | Получить громкость BGV2 | |
| `isMoviePlaying()` | bool | Видео проигрывается? | 💡 |

---

## Информация об окне и дисплее

| Свойство / Метод | Тип | Описание |
|---|---|---|
| `fullScreen` | bool | Полноэкранный режим |
| `fullScreenMode` | int | Режим полного экрана |
| `fullScreened` | bool | В полном экране |
| `maximizeMode` | int | Режим максимизации |
| `maximized` | bool | Окно развёрнуто |
| `minimized` | bool | Окно свёрнуто |
| `iconic` | bool | В трее |
| `appIconicPreview` | bool | Предпросмотр при свёртывании |
| `pseudoFullScreened` | bool | Псевдо-полноэкранный |
| `borderStyle` | int | Стиль рамки окна |
| `windowResizeable` | bool | Изменяемый размер окна |
| `visible` | bool | Видимость окна |
| `top` / `left` | int | Позиция окна |
| `width` / `height` | int | Размер окна |
| `clientWidth` / `clientHeight` | int | Размер клиентской области |
| `primaryWidth` / `primaryHeight` | int | Размер primary поверхности |
| `scWidth` / `scHeight` | int | Размер экрана |
| `pxWidth` / `pxHeight` | int | Физический размер |
| `exWidth` / `exHeight` | int | Внешний размер |
| `innerWidth` / `innerHeight` | int | Внутренний размер |
| `minWidth` / `minHeight` | int | Минимальный размер |
| `maxWidth` / `maxHeight` | int | Максимальный размер |
| `uiWidth` / `uiHeight` | int | Размер UI |
| `uiOffsetx` / `uiOffsety` | int | Смещение UI |
| `zoomDenom` / `zoomNumer` | int | Знаменатель/числитель зума |
| `origZoomDenom` / `origZoomNumer` | int | Исходные параметры зума |
| `getScreenRect()` | rect | Получить прямоугольник экрана |
| `getWindowRect()` | rect | Получить прямоугольник окна |
| `getDstRes()` | rect | Получить разрешение цели |
| `getPrimaryMonitorWorkRect()` | rect | Рабочая область монитора |
| `d3dBicubicParam` | float | Параметр бикубического фильтра |
| `d3dLowSpec` | bool | Низкие spec D3D |
| `d3dStretchType` | int | Тип растяжения D3D |
| `d3dTextureMaxSize` | int | Макс. размер текстуры D3D |
| `d3dTextureRenderTarget` | bool | Текстура как render target |
| `getDrawDevice()` | object | Получить устройство отрисовки |
| `setDrawDevice(dev)` | void | Установить устройство отрисовки |
| `drawDevice` | object | Текущее устройство отрисовки |
| `isD3D()` | bool | Используется Direct3D? |
| `getWindowVirtualPixelHeight()` | int | Виртуальная высота пикселя |

---

## Слои

| Свойство / Метод | Тип | Описание |
|---|---|---|
| `primaryLayer` | object | Primary слой (задний фон) |
| `temporaryLayer` | object | Временный слой |
| `tempLayer` | object | Временный слой (альтернатива) |
| `transLayer` | object | Слой переходов |
| `backupLayer` | object | Резервный слой |
| `motionD3DAdaptor` | object | D3D адаптер для motion |
| `motionWorkLayer` | object | Рабочий слой motion |
| `snapshotLayer` | object | Слой снимка |
| `poolLayer` | object | Pool-слой |
| `panelLayer` | object | Панельный слой |
| `panelLayerAbsolute` | object | Абсолютный панельный слой |
| `panelModal` | bool | Модальная панель |
| `panelShowing` | bool | Панель отображается |
| `btLayer` | object | Backlog-текстовый слой |
| `mouseCursorState` | object | Состояние курсора мыши |
| `tempZoomed` | bool | Временный зум |
| `getPrimaryLayerAt(x, y)` | object | Получить слой в координатах |
| `setPrimaryFocus(layer)` | void | Установить фокус на слой |
| `resetPrimaryFocus()` | void | Сбросить фокус |
| `setMultiFocus(focus)` | void | Множественный фокус |
| `getLayer(index)` | object | Получить слой по индексу |
| `getLayerList()` | array | Список слоёв |
| `getLayerFromElm(elm)` | object | Слой из элемента |
| `copyLayer(src, dst)` | void | Копировать слой |
| `clearLayers()` | void | Очистить слои |
| `getSelectLayer()` | object | Получить слой выбора |
| `getMessageLayer()` | object | Получить слой сообщений |
| `getMessageLayerByIndex(idx)` | object | Слой сообщений по индексу |
| `getMessageLayerCurrent()` | object | Текущий слой сообщений |
| `getMessageLayerNum()` | int | Количество слоёв сообщений |
| `getMessageLayerFromElm(elm)` | object | Слой сообщений из элемента |
| `getTextLayer()` | object | Получить текстовый слой |
| `setTextLayer(layer)` | void | Установить текстовый слой |
| `setBackupLayer(layer)` | void | Установить резервный слой |
| `setLayer(index, layer)` | void | Установить слой по индексу |
| `setLayerManagerIndex(idx)` | void | Индекс менеджера слоёв |
| `allocateMessageLayers(num)` | void | Выделить слои сообщений |
| `clearMessageLayers()` | void | Очистить слои сообщений |

---

## Сохранение / Загрузка

| Свойство / Метод | Тип | Описание |
|---|---|---|
| `bookMarkIndex` | int | Индекс сейв-слота |
| `bookMarkDateSecond` | int | Дата сейва (секунды) |
| `numBookMarks` | int | Количество сейвов |
| `saveDataID` | int | ID сейв-даты |
| `saveDataMode` | int | Режим сейв-данных |
| `saveDataLocation` | string | Расположение сейв-данных |
| `freeSaveDataMode` | int | Режим свободных сейв-данных |
| `readOnlyMode` | bool | Режим только чтение |
| `storeEnabled` | bool | Сохранение разрешено |
| `restoreEnabled` | bool | Восстановление разрешено |
| `storeLabelPassed` | bool | Метка сохранения пройдена |
| `isSaveThumbnailEnabled` | bool | Сохранение превью включено |
| `getBookMarkInfo(idx)` | object | Информация о сейве |
| `getBookMarkFileName(idx)` | string | Имя файла сейва |
| `getBookMarkFileNameAtLabel(label)` | string | Имя файла сейва по метке |
| `getBookMarkFileNameAtNum(num)` | string | Имя файла сейва по номеру |
| `getBookMarkStorage(idx)` | string | Сценарий в сейве |
| `getBookMarkPageName(idx)` | string | Страница в сейве |
| `getBookMarkDate(idx)` | string | Дата сейва |
| `getBookMarkDateText(idx)` | string | Дата сейва (текст) |
| `getBookMarkPlayTime(idx)` | string | Время игры в сейве |
| `getBookMarkMenuCaption(idx)` | string | Заголовок меню сейва |
| `isExistentBookMark(idx)` | bool | Существует ли сейв |
| `saveBookMark(idx)` | void | Сохранить сейв |
| `saveBookMarkToFile(path)` | void | Сохранить в файл |
| `loadBookMark(idx)` | void | Загрузить сейв |
| `loadBookMarkFromFile(path)` | void | Загрузить из файла |
| `loadBookMarkWithAsk(idx)` | void | Загрузить с подтверждением |
| `copyBookMark(src, dst)` | void | Копировать сейв |
| `eraseBookMark(idx)` | void | Удалить сейв |
| `tempSave()` | void | Быстрое сохранение |
| `tempLoad()` | void | Быстрая загрузка |
| `storeBookMark()` | void | Сохранить метку |
| `restoreBookMark()` | void | Восстановить метку |
| `storeAutoLabel()` | void | Сохранить авто-метку |
| `goBackAutoLabelStore()` | void | Вернуться к авто-метке |
| `goBackHistory()` | void | Вернуться по истории |
| `goBackByKey()` | void | Вернуться по клавише |
| `initBookMark()` | void | Инициализировать сейвы |
| `clearBookMarkIndex()` | void | Очистить индекс сейва |
| `setBookMarkIndex(idx)` | void | Установить индекс сейва |
| `setBookMarkMenuCaptions()` | void | Установить заголовки меню |
| `setStoreEnabled(enabled)` | void | Разрешить сохранения |
| `saveSystemVariables()` | void | Сохранить системные переменные |
| `loadSystemVariables()` | void | Загрузить системные переменные |

---

## Хуки и события 💡

| Событие / Хук | Описание | Neuro |
|---|---|---|
| `onConductorLabel` | Метка сценария достигнута | 💡 |
| `onConductorScenarioLoad` | Загрузка сценария | 💡 |
| `onConductorScenarioLoaded` | Сценарий загружен | 💡 |
| `onConductorScript` | Выполнение скрипта | |
| `onConductorCall` | Вызов подпрограммы | |
| `onConductorReturn` | Возврат из подпрограммы | |
| `onConductorJump` | Прыжок по сценарию | |
| `onConductorStop` | Остановка кондуктора | |
| `onConductorUnknownTag` | Неизвестный тег | |
| `onConductorBeforeTag` | Перед обработкой тега | |
| `onConductorAfterReturn` | После возврата | |
| `onConductorRestoreTarget` | Цель восстановления | |
| `onVoiceStart` | Голос начал воспроизведение | 💡 |
| `onVoiceStop` | Голос остановлен | 💡 |
| `onVoiceLabel` | Голосовая метка | |
| `onVoiceLabelStart` | Голосовая метка началась | |
| `onVoiceLabelStop` | Голосовая метка закончилась | |
| `onVoiceFlip` | Переворот голоса | |
| `onBGMStart` | BGM начал | 💡 |
| `onBGMStop` | BGM остановлен | 💡 |
| `onBGMLabel` | BGM метка | |
| `onBGMFadeCompleted` | Затухание BGM завершено | |
| `onSEStart` | SE начал | |
| `onSESoundBufferStop` | SE буфер остановлен | |
| `onSESoundBufferFadeCompleted` | SE затухание завершено | |
| `onSELabel` | SE метка | |
| `onPrimaryClick` | Основной клик | 💡 |
| `onPrimaryClickByKey` | Основной клик клавишей | |
| `onPrimaryRightClick` | Правый клик | |
| `onActivate` / `onDeactivate` | Активация/деактивация окна | |
| `onResize` / `onResizing` | Изменение размера окна | |
| `onMinimize` | Свёртывание окна | |
| `onShow` / `onHide` | Показ/скрытие окна | |
| `onKeyDown` / `onKeyUp` | Клавиша нажата/отпущена | |
| `onMouseDown` / `onMouseUp` | Кнопка мыши нажата/отпущена | |
| `onMouseMove` / `onMouseWheel` | Движение/колёсико мыши | |
| `onMouseEnter` / `onMouseLeave` | Вход/выход мыши | |
| `onCloseQuery` | Запрос закрытия окна | |
| `onCloseYes` | Подтверждение закрытия | |
| `onScreenSave` | Сохранение экрана | |
| `onShutdown` | Завершение игры | 💡 |
| `onTransitionEnd` | Переход завершён | |
| `onAnimationStopped` | Анимация остановлена | |
| `onMotionStart` / `onMotionStop` | Движение начато/остановлено | |
| `onDisplayChanged` | Дисплей изменён | |
| `onErrorVoice` | Ошибка голоса | |
| `onCheckTagExtract` | Проверка извлечения тега | |
| `onBookMarkStore` / `onBookMarkRestore` | Сохранение/восстановление сейва | |
| `onConductor` | Любое событие кондуктора | |
| `addHook(hookName, handler)` | Добавить хук | 💡 |
| `removeHook(hookName)` | Удалить хук | |
| `callHook(hookName, ...args)` | Вызвать хук | |
| `forEachEventHook` | Итерация по хукам событий | |
| `forEachFunctionHook` | Итерация по хукам функций | |
| `onMoviePlay` / `onMovieStop` | Видео играет/остановлено | |
| `onFlipTimerInterval` | Интервал таймера flip | |
| `onQuakeTimerInterval` | Интервал таймера землетрясения | |
| `onSquirrelThreadDone` | Поток squirrel завершён | |
| `onMessageReceived` | Сообщение получено | |

---

## Управление диалогами и меню

| Свойство / Метод | Тип | Описание |
|---|---|---|
| `currentDialog` | string | Имя текущего диалога |
| `currentDialogAbsolute` | string | Абсолютное имя диалога |
| `currentDialogFocusedRestore` | int | Фокус восстановления диалога |
| `isDialogOpened()` | bool | Открыт ли диалог? |
| `openDialog(dialogName)` | void | Открыть диалог |
| `closeDialog()` | void | Закрыть диалог |
| `handleCloseDialog()` | void | Обработка закрытия диалога |
| `handleOpenDialog()` | void | Обработка открытия диалога |
| `menuBarEnable` | bool | Панель меню включена |
| `menuBarState` | int | Состояние панели меню |
| `menuBarMode` | int | Режим панели меню |
| `menu` | object | Ссылка на меню |
| `systemMenu` | object | Системное меню |
| `systemControlMenu` | object | Меню управления системой |
| `trackTempMenuShowing` | bool | Отслеживание временного меню |
| `createMenuBarHideMenu()` | void | Создать скрытое меню |
| `changeMenuBarState(state)` | void | Изменить состояние меню |
| `toggleMenuBarState()` | void | Переключить состояние меню |
| `trackMenuByKey()` | void | Отследить меню по клавише |
| `trackMenuByMouse()` | void | Отследить меню по мыши |
| `trackPseudoMenu()` | void | Отследить псевдо-меню |
| `trackTempMenu()` | void | Отследить временное меню |
| `closeTempMenu()` | void | Закрыть временное меню |
| `isExHelperMode()` | bool | Режим ExHelper? |

---

## Запросы состояния (геттеры) 💡

| Метод | Возврат | Описание | Neuro |
|---|---|---|---|
| `isFullScreen()` | bool | Полноэкранный режим? | |
| `isIconic()` | bool | Свёрнуто? | |
| `isMaximized()` | bool | Развёрнуто? | |
| `isMinimized()` | bool | Минимизировано? | |
| `isWindowActive()` | bool | Окно активно? | 💡 |
| `isDelay()` | bool | Есть задержка? | |
| `isVoicePlaying()` | bool | Голос играет? | 💡 |
| `isVoiceWait()` | bool | Ожидание голоса? | 💡 |
| `isMoviePlaying()` | bool | Видео играет? | 💡 |
| `isRecollection()` | bool | Режим воспоминаний? | 💡 |
| `isPseudoMode()` | bool | Псевдо-режим? | |
| `isDefaultName()` | bool | Имя по умолчанию? | |
| `isD3D()` | bool | Direct3D? | |
| `isUsePlayTime()` | bool | Использовать время игры? | |
| `isLocalStorage()` | bool | Локальное хранилище? | |
| `isExistentBookMark(idx)` | bool | Существует сейв? | |
| `isHistoryOfStoreAlive()` | bool | История жива? | |
| `isDisabledAnimationLayer()` | bool | Анимация слоя отключена? | |
| `isChVoiceOn()` | bool | Канальный голос включён? | |
| `isVoiceCut()` | bool | Голос обрезан? | |
| `checkClick(x, y)` | bool | Проверить клик по координатам | |
| `checkProceedingKey(key)` | bool | Проверить нажатую клавишу | |
| `checkSave()` | bool | Можно сохранить? | |
| `checkVoiceWait()` | bool | Ожидание голоса? | |
| `checkTextSpeech()` | bool | Текстовая речь? | |
| `checkExistVoiceFilename(name)` | bool | Существует файл голоса? | |
| `checkLayerTouch(layer, x, y)` | bool | Касание слоя? | |

---

## Управление сценарием (кондуктор)

| Метод | Описание |
|---|---|
| `loadScenario(storage, label)` | Загрузить сценарий по имени файла и метке |
| `exec(code)` | Выполнить TJS-код |
| `eval(code)` | Вычислить TJS-выражение |
| `callExtraConductor(cond)` | Вызвать дополнительный кондуктор |
| `returnExtraConductor()` | Вернуться из дополнительного кондуктора |
| `setConductorToMain()` | Переключить кондуктор на основной |
| `getAliasName()` | Получить алиас кондуктора |
| `goToStart()` | Перейти к началу игры |
| `goToStartWithAsk()` | Перейти к началу с подтверждением |
| `startRecollection()` | Начать режим воспоминаний |
| `stopRecollection()` | Остановить режим воспоминаний |
| `endRecollection()` | Закончить воспоминания |
| `recollectionEndStorage` / `recollectionEndLabel` | Параметры конца воспоминаний |
| `recollectionStorage` / `recollectionTarget` | Параметры цели воспоминаний |
| `repageAutoIndent()` | Перестроить авто-отступы |
| `initAutoIndent()` | Инициализировать авто-отступы |

---

## Управление текстом и отображением

| Метод | Описание |
|---|---|
| `showMessageLayerByUser()` | Показать слой сообщений |
| `hideMessageLayerByUser()` | Скрыть слой сообщений |
| `hideMessageLayerByScenario()` | Скрыть слой сообщений (сценарием) |
| `switchMessageLayerHiddenByUser()` | Переключить видимость слоя |
| `setCurrentMessageLayerVisible(vis)` | Установить видимость текущего слоя |
| `setCurrentMessageLayerVisibleFast(vis)` | Быстрая установка видимости |
| `setMessageLayerPosition(idx, z)` | Установить позицию слоя сообщений |
| `setMessageLayerUserFont()` | Установить шрифт пользователя |
| `setMessageLayerHiddenState(hidden)` | Установить скрытое состояние |
| `syncMessageLayer()` | Синхронизировать слой сообщений |
| `showPageBreak()` | Показать разрыв страницы |
| `showPageBreakAndClear()` | Показать разрыв страницы и очистить |
| `showHistory()` | Показать историю |
| `hideHistory()` | Скрыть историю |
| `showHistoryByKey()` | Показать историю по клавише |
| `showHistoryByScenario()` | Показать историю сценарием |
| `showSelect()` | Показать выбор |
| `hideSelect()` | Скрыть выбор |
| `showMapSelect()` | Показать выбор на карте |
| `hideMapSelect()` | Скрыть выбор на карте |
| `showPanel()` | Показать панель |
| `hidePanel()` | Скрыть панель |
| `hideTransLayer()` | Скрыть слой переходов |
| `hideBlackLayer()` | Скрыть чёрный слой |
| `showClickGlyphs` / `hideClickGlyphs` | Показать/скрыть глифы клика |
| `setMessageHook(layer, cmd)` | Установить хук сообщения |
| `drawName(name)` | Нарисовать имя персонажа |
| `drawFace(face)` | Нарисовать лицо |
| `drawFaceLayer()` | Нарисовать слой лица |
| `drawToSnapshot()` | Рендерить в снимок |

---

## Задержки и ожидания

| Метод | Описание |
|---|---|
| `waitClick()` | Ждать клик |
| `waitTime(ms)` | Ждать указанное время |
| `waitDelay()` | Ждать задержку |
| `waitAuto()` | Ждать авто-режим |
| `waitAutoClear()` | Ждать очистку авто |
| `waitVoice()` | Ждать окончания голоса |
| `waitAllVoice()` | Ждать все голоса |
| `waitBGMFade()` | Ждать затухания BGM |
| `waitBGMLabel()` | Ждать BGM-метку |
| `waitBGMStop()` | Ждать остановки BGM |
| `waitSEFade()` | Ждать затухания SE |
| `waitSEStop()` | Ждать остановки SE |
| `waitTransition()` | Ждать переход |
| `waitLayerMotion()` | Ждать движение слоя |
| `waitMotion()` | Ждать движение |
| `waitMove()` | Ждать перемещение |
| `waitSquirrel()` | Ждать поток |
| `waitAllSquirrel()` | Ждать все потоки |
| `waitAllMotions()` | Ждать все движения |
| `waitAllAction()` | Ждать все действия |
| `waitAction(action)` | Ждать действие |
| `waitFunc(func)` | Ждать функцию |
| `waitUntil(cond)` | Ждать до условия |
| `waitQuake()` | Ждать землетрясение |
| `waitMoviePeriod()` | Ждать период видео |
| `waitMovieReady()` | Ждать готовность видео |
| `waitMovieStop()` | Ждать остановку видео |
| `waitTrigger(trig)` | Ждать триггер |
| `cancelDelay()` | Отменить задержку |
| `entryDelay(time)` | Ввести задержку |
| `entryDelayCmd(cmd)` | Ввести команду задержки |
| `entryDelayTime(time)` | Ввести время задержки |
| `clearDelay()` / `clearDelayTime()` | Очистить задержку/время |
| `getDelayLabelTime()` | Получить время задержки метки |
| `startDelay()` / `isDelay()` | Начать/проверить задержку |
| `delayTime` / `delayTimeList` | Таймер / список таймеров задержки |
| `delayLabels` | Метки задержки |
| `delayQueue` | Очередь задержки |

---

## Прочие полезные свойства

| Свойство | Тип | Описание | Neuro |
|---|---|---|---|
| `name` | string | Имя окна | |
| `title` | string | Заголовок окна | |
| `subTitle` | string | Подзаголовок | |
| `caption` | string | Надпись окна | |
| `versionString` | string | Версия | |
| `fileVersion` | string | Версия файла | |
| `dataName` | string | Имя данных | |
| `kagPlugins` | object | Плагины KAG | |
| `kag` | object | Ссылка на KAGEnvironment | |
| `mainWindow` | object | Главное окно | |
| `conductor` | object | MainConductor | 💡 |
| `flags` | object | Системные флаги | |
| `pcflags` | object | Флаги PC | |
| `sflags` | object | Флаги сценария | |
| `scflags` | object | Флаги сцены | |
| `tflags` | object | Флаги временные | |
| `error` | string | Текущая ошибка | |
| `errorCmd` | string | Команда, вызвавшая ошибку | |
| `errorLine` | int | Строка ошибки | |
| `errorInform` | bool | Информирование об ошибке | |
| `errorImage` | string | Изображение ошибки | |
| `errorSound` | string | Звук ошибки | |
| `errorVoice` | string | Голос ошибки | |
| `extraConductor` | object | Дополнительный кондуктор | |
| `extraTrigger` | string | Дополнительный триггер | |
| `usingExtraConductor` | bool | Используется доп. кондуктор? | |
| `mouseCursor` | int | Курсор мыши | |
| `mouseCursorState` | object | Состояние курсора | |
| `cursorDefault` / `cursorPointed` / `cursorDraggable` / `cursorWaitingClick` / `cursorWaitingAuto` | int | Константы курсора | |

---

## Быстрый доступ для Neuro

Основные точки входа для получения состояния игры из TJS:

```javascript
// Текущий текст и сценарий
kag.currentDialog       // отображаемый текст
kag.currentLabel        // текущая метка
kag.currentStorage      // текущий файл сценария
kag.currentPageName     // имя страницы
kag.getSpeechText()     // текст речи
kag.getCurrentInfo()    // полная информация о строке

// Состояние воспроизведения
kag.isVoicePlaying()    // играет ли голос
kag.voicevolume         // громкость голоса
kag.bgmvolume           // громкость BGM
kag.bgmenable           // включён ли BGM
kag.isMoviePlaying()    // играет ли видео

// Режимы чтения
kag.skipMode            // режим пропуска
kag.autoMode            // авто-чтение
kag.clickWaiting        // ожидание клика
kag.isRecollection()    // режим воспоминаний

// Управление
kag.setSkipMode(1)      // включить skip
kag.cancelSkip()        // выключить skip
kag.enterAutoMode()     // включить авто
kag.cancelAutoMode()    // выключить авто
kag.skipToLabel(name)   // пропустить до метки
kag.loadScenario(storage, label)  // загрузить сценарий

// События (хуки)
kag.addHook("onConductorLabel", handler)
kag.addHook("onVoiceStart", handler)
kag.addHook("onPrimaryClick", handler)
kag.addHook("onShutdown", handler)
```
