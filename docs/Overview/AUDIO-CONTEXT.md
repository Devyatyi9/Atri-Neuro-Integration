# Audio Context for Neuro API

> Текстовые описания звуков (BGM, SE, голоса) для передачи Neuro-sama API.
> Цель — дать AI эмоциональный/аудиальный контекст сцены текстом.

## Общие принципы

- Формат — **второе лицо** ("You hear..."), иммерсивно для зрителей и готово для озвучки.
- Описывать звук, а не файл: `"You hear a calm underwater ambience, hopeful undertones"` вместо `"BGM01"`.
- Единый стиль для всех аудио-событий: `"You hear {description}.{optional mood}"`.
- Скрытые приоритеты: голос > BGM > SE (что важнее в моменте).

## BGM (фоны)

Маппинг `kag.bgm.playingStorage` → английское описание (файл `neuro_scripts/bgm_descriptions.tjs`).

Пример:
```tjs
var bgmDesc = %[
  "BGM01": "calm underwater ambience, hopeful undertones",
  "BGM02": "a gentle ray of hope through the depths",
  // ...
];
```

Формат события:
```
"You hear {description}."
```

### Остановка музыки

Когда трек был и пропал (отслеживай `kag.bgm.inPlaying` true→false) — одноразовое описание тишины:

```tjs
// Разные варианты (можно на рандом):
"The music fades, leaving an uneasy silence."
"Only the sound of waves remains."
"The room falls silent."
"As the music stops, {character}'s voice is alone in the silence."
```

> Если музыки не было и нет — ничего не отправлять (не засорять контекст).
> "The music stops." — допустимо, но лучше с контекстом.

## SE (звуковые эффекты)

Отслеживание: теги `[se]`, `[playse]`, `[wa]` (ambient) через `kag.conductor.lastTagName` / polling.

Маппинг filename → описание:
```tjs
var seDescriptions = %[
  "se_footsteps_metal": "footsteps echo on metal stairs",
  "se_door_open": "a door slides open",
  "se_machine": "the hum of machinery fills the air",
  // ...
};
```

Формат события:
```
"You hear {description}."
```

Это дополняет Neuro аудио-интеграцией: даже без прямого звука AI знает, что происходит.

## Голоса (Voice descriptions)

Идея: при **первом появлении** персонажа (имя ещё неизвестно) — описание голоса вместо имени, как в speaker label.

Пример:
- Высокий/низкий, молодой/старый, тон.
- При смене имени в `kag.historyLayer.currentName` с неизвестного на известный — в первый раз подавать звук.

Данные уже есть в `characterVoiceInfo` (voice prefix → персонаж, языконезависимый).

Маппинг `voicePrefix → { description, name }`:
```tjs
var voiceDescriptions = %[
  "man": { desc: "a deep, calm voice", name: "" },
  "wom": { desc: "a soft rising voice", name: "" },
  // ...
};
```

Логика: пока `currentName` пусто/неизвестно → отправляй `desc`, при установке имени → `name`.

## Планы / TODO

- [ ] Файл маппинга BGM-описаний (`neuro_scripts/bgm_descriptions.tjs`)
- [ ] Файл маппинга SE-описаний
- [ ] Маппинг голосовых описаний на основе `characterVoiceInfo`
- [ ] Логика "первого появления" персонажа (хук смены `currentName`)
- [ ] Рандом вариантов для тишины/событий
- [ ] Интеграция с форматом контекста Neuro API