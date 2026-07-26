# Voice.tjs — Voice Playback

Источник: байткод игры `system/Voice.tjs`
Исходник: отсутствует в kag3/KAGeXpress

---

## VoiceSoundBuffer

Буфер голосового воспроизведения. Наследует `SoundBuffer`.

**Свойства:**
- `owner` — Object (KAGEnvironment)

**Методы:**

| Сигнатура | Описание |
|-----------|----------|
| `VoiceSoundBuffer()` | Конструктор |
| `open(storage, vol, pan)` | Открыть голосовой файл |
| `start(storage, vol, pan)` | Запустить голос |
| `stop()` | Остановить |
| `fade(vol, time, system)` | Fade |
| `_start()` | Внутренний запуск |
| `canWait()` → bool | Можно ждать? |
| `clearMode()` | Сбросить режим |
| `resetVolume()` | Сбросить громкость |
| `setGain(val)` / `resetGain()` | Громкость |
| `_onStatusChanged()` / `onStatusChanged()` | Статус изменился |
| `onLabel()` | Метка сценария |
| `onFadeCompleted()` | Fade завершён |
| `onFlip()` | Смена страницы текста |
| `onErrorVoice()` | Ошибка голоса |

---

## Использование

```javascript
// Проверка играет ли голос:
// (голос привязан к диалогу, обычно запускается автоматически)

// Текущий статус можно отследить через onStatusChanged
// (см. sysscn/voicemode.tjs для деталей)
```
