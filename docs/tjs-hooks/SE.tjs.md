# SE.tjs — Sound Effects

Источник: байткод игры `system/SE.tjs`
Сравнение: `docs/original-kag-tjs/data/kag3/SE.tjs` (source)

---

## SESoundBuffer

Буфер звуковых эффектов.

**Свойства:**
- `owner` — Object (KAGEnvironment)

**Методы:**

| Сигнатура | В исходнике |
|-----------|:---:|
| `SESoundBuffer()` — конструктор | ✅ |
| `play(storage, loop, volume)` — проиграть | ✅ |
| `stop(storage)` — остановить | ✅ |
| `stopFade()` | ❌ |
| `fade(vol, time, system)` | ❌ |
| `fadeIn(params)` | ❌ |
| `fadeOut(time)` | ❌ |
| `onFadeCompleted()` | ❌ |
| `onStatusChanged()` | ❌ |
| `canWaitStop()` | ❌ |
| `setVolume(vol)` | ✅ |
| `setGain(val)` / `resetGain()` | ❌ |
| `setOptions(params)` — `{volume, gvolume}` | ✅ |
| `store()` / `restore()` / `restoreSystemState()` | ✅ |
| `onLabel()` — обработка метки | ❌ |
