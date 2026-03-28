# Batching Ideas

Кстати, на счёт batching я уточнял варианты, вот что-то вроде прототипа или наброска:

```cpp

// 1. ВЫСОКИЙ ПРИОРИТЕТ: Атомарный счётчик для срочных команд (Action, Force)

std::atomic<int> g_highPriorityMsgCount(0);

const int MAX_HIGH_PRIORITY = 100;



bool PostHighPriorityMessage(const std::string& json) {

    // Быстрая проверка и отправка в главный поток

    if (g_highPriorityMsgCount.fetch_add(1) < MAX_HIGH_PRIORITY) {

        // Кодируем JSON в wchar_t и отправляем WM_TJS_HIGH_PRIORITY_MSG

        PostMessage(g_GameWindow, WM_TJS_HIGH_PRIORITY_MSG, ...);

        return true;

    } else {

        g_highPriorityMsgCount--;

        LogToFile("High-priority message dropped (queue full).\n");

        return false;

    }

}



// 2. НИЗКИЙ ПРИОРИТЕТ: Буфер-агрегатор для потока контекста (чат, события)

struct ContextBatch {

    std::vector<std::string> contextMessages; // Накопленные сообщения

    // Можно добажать метаданные: источник, тип и т.д.

};

std::mutex g_contextBufferMutex;

ContextBatch g_contextBuffer;



void BufferContextMessage(const std::string& contextJson) {

    std::lock_guard<std::mutex> lock(g_contextBufferMutex);

    g_contextBuffer.contextMessages.push_back(contextJson);

    // Можно задать лимит на размер вектора

}



// 3. ОБРАБОТКА В ГЛАВНОМ ПОТОКЕ (например, по WM_TIMER)

case WM_TIMER: {

    // А. Обрабатываем ВСЁ из буфера контекста за ОДИН вызов

    ContextBatch batchToProcess;

    {

        std::lock_guard<std::mutex> lock(g_contextBufferMutex);

        batchToProcess = std::move(g_contextBuffer);

        // g_contextBuffer теперь пуст

    }

    if (!batchToProcess.contextMessages.empty()) {

        // Конвертируем batchToProcess в один JSON-массив

        // ВЫПОЛНЯЕМ ОДИН ВЫЗОВ TJS: neuroOnContextBatch(batchedDataJson)

        ExecuteTJSAsync(L"neuroOnContextBatch(" + BatchToJson(batchToProcess) + L")");

    }

    // Б. Высокоприоритетные сообщения уже обрабатываются

    // в своих обработчиках (WM_TJS_HIGH_PRIORITY_MSG)

    break;

}
```

Идея разделения на "Срочное" (High Priority) и "Контекстное" (Batching) решает главную проблему производительности TJS — накладные расходы на вызов скрипта.

Почему это хорошо:

Снижение Overhead: Вызов neuroOnContextBatch один раз с массивом из 50 сообщений в 100 раз дешевле, чем 50 вызовов neuroOnMessage. TJS — интерпретатор, вход в него стоит дорого.

WM_TIMER: Это безопасный способ разгрузки. Он не забивает очередь сообщений.

Атомик на High Priority: Защищает от DoS-атаки на игровой цикл, если нейронка сойдет с ума и начнет спамить кликами.

Что можно улучшить (Micro-optimizations):

Триггер переполнения буфера: WM_TIMER срабатывает раз в N мс. Если буфер заполнится раньше (резкий всплеск чата), сообщения "застрянут" до тика таймера.

Решение: В BufferContextMessage добавьте проверку:
```cpp
if (g_contextBuffer.contextMessages.size() >= BATCH_LIMIT) {
     // Форсируем обработку, не дожидаясь таймера
     PostMessage(g_GameWindow, WM_PROCESS_CONTEXT_BATCH, 0, 0); 
}
```
Тяжелый JSON в Main Thread: Строка BatchToJson(batchToProcess) выполняется в главном потоке (внутри WM_TIMER). Если там накопится 1 МБ текста, это может вызвать микро-фриз при склейке строк.

Решение: Если данных реально много, JSON лучше склеить в std::string в рабочем потоке (перед отправкой), а в главном только сконвертировать в ttstr. Но для чата это, скорее всего, преждевременная оптимизация.

Я не думаю что именно так буду делать, это скорее на размышления, на будущее. Эта идея была не моя, а со стороны.

Упрощённый вариант для начала:
```cpp
// Срочные команды — как сейчас (PostMessage + WM_TJS_EXECUTE)
// Контекст — накапливаем и отправляем по таймеру или при переполнении

std::vector<std::wstring> g_pendingContext;
std::mutex g_contextMutex;
const size_t CONTEXT_BATCH_LIMIT = 20;

void QueueContext(const wchar_t* data) {
    std::lock_guard<std::mutex> lock(g_contextMutex);
    g_pendingContext.push_back(data);
    
    if (g_pendingContext.size() >= CONTEXT_BATCH_LIMIT) {
        PostMessage(g_GameWindow, WM_FLUSH_CONTEXT, 0, 0);
    }
}
```
