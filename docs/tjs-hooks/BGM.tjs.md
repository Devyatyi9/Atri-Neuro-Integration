# BGM.tjs — Background Music

Источник: байткод игры `system/BGM.tjs`
Сравнение: `docs/original-kag-tjs/data/kag3/BGM.tjs` (source)

---

## Классы

### KAGSoundBuffer extends SoundBuffer
Базовый буфер звука. Методы: `fade()`, `stopFade()`, `stop()`, `fadeOutAndStop()`, `fadeOutAndPause()`, `onFadeCompleted()`, `onStatusChanged()`.

### KAGWaveSoundBuffer extends KAGSoundBuffer
Для WAV-файлов. `onLabel()`.

### KAGMIDISoundBuffer extends KAGSoundBuffer
Для MIDI.

### KAGCDDASoundBuffer extends KAGSoundBuffer
Для CD Audio.

### BGM
Управление фоновой музыкой.

**Декомпиляция (Python crate-1556):** все перечисленные функции и свойства подтверждены. `inPlaying` — **property** в байткоде (не C++ side):

```javascript
property inPlaying {
    return playingStorage != "" && (currentBuffer.status == "play" && !currentBuffer.inFadeAndStop);
}
```

**Свойства:**
- `currentStorage` — string, текущий трек
- `playingStorage` — string, играющий трек ("" если нет)
- `volume` — int (0-1000)
- `buf1`, `buf2` — KAGSoundBuffer (buf2 для crossfade)
- `owner` — Object (KAGEnvironment)
- `paused` — property getter/setter, состояние паузы
- `inFading` — property getter, идёт ли fade
- `canWaitStop` — property getter, можно ли ждать остановки
- `inPlaying` — property getter: `playingStorage != "" && status == "play" && !inFadeAndStop`

**Методы:**

| Сигнатура | В исходнике |
|-----------|:---:|
| `BGM()` — конструктор | ✅ |
| `play(params)` — запустить. `params`: `{storage, loop, volume}` | ✅ |
| `stop()` — остановить. Очищает `currentStorage`/`playingStorage` | ✅ |
| `pause()` | ✅ |
| `resume()` | ✅ |
| `fadeIn(params)` — fade-in с параметрами | ❌ |
| `fadeOut(time)` — fade-out | ✅ |
| `fadePause()` / `fadeResume()` | ❌ |
| `fade(vol, time, system)` — плавная смена громкости | ✅ |
| `stopFade()` | ✅ |
| `exchange(params)` — crossfade. `params`: `{storage, loop, time, intime(5000), outtime}` | ✅ |
| `setVolume(vol)` | ✅ |
| `setGain(val)` / `resetGain()` | ❌ |
| `setOptions(params)` — `{device, volume, gvolume}` | ✅ |
| `setLoop(loop)` | ❌ |
| `canWaitLabel()` — проверка ожидания метки | ❌ |
| `onSoundBufferFadeCompleted()` — коллбэк окончания fade | ❌ |
| `onSoundBufferStop(buffer)` — **вызывает `owner.onBGMStop()`** | ❌ |
| `store()` / `restore()` / `restoreSystemState()` | ❌ |

---

## Event Hooks

| Событие | Вызов | Когда |
|---------|-------|-------|
| BGM закончил играть | `owner.onBGMStop()` | Буфер остановлен |

```javascript
// Получение текущего трека:
kag.bgm.playingStorage  // "bgm/theme.ogg" или ""
kag.bgm.currentStorage  // текущий назначенный трек
```
