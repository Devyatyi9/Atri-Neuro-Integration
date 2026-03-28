# KiriKiri-Z Neuro Integration

Плагин для интеграции визуальной новеллы ATRI с AI-стримером Neuro-sama.

## Структура документации

```
docs/
├── README.md              ← Вы здесь
├── TODO.md               # Задачи и прогресс
│
├── Overview/             # Обзорные документы
│   ├── ARCHITECTURE.md   # Архитектура плагина
│   ├── GAME-MECHANICS.md # Механики игры
│   └── IMGUI-INSPECTOR.md # Инструмент отладки
│
├── kirikiri/             # KiriKiri/TJS
│   ├── TJS-Commands.md  # TJS команды
│   ├── TJS-Scripts.md   # Примеры скриптов
│   └── BATCHING-IDEA.md # Идеи батчинга
│
├── utils/               # Утилитарная информация
│   └── dump-keys.txt
│
└── GameObjectsKeys/     # Дампы объектов игры (.txt)
```

## Быстрый старт

### Сборка

1. Открыть `KiriKiri-Test/KiriKiri-Test.sln`
2. Build → Build Solution (Release, Win32)

### Запуск

1. Скопировать `integration-test.dll` в папку с игрой
2. Запустить игру
3. Нажать **F12** для открытия ImGui панелей

## Горячие клавиши

| Клавиша | Действие |
|---------|----------|
| F12 | Toggle ImGui windows |

## Использование

Дампы созданы через:
```tjs
Scripts.getObjectKeys(objectName)
```

Содержимое - список всех свойств и методов объекта.

## Ресурсы

- [TJS2 Reference](https://krkrz.github.io/docs/tjs2/j/contents/index.html)
- [KAG System](https://krkrz.github.io/krkr2doc/kag3/)
