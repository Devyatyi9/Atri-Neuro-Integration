# Character Reference: ATRI -My Dear Moments-

Сводка всех персонажей игры по данным `charlist.csv`, `charvoice.csv`, `envinit.tjs`, `macro.ks`.

---

## Основной состав (есть спрайты + озвучка)

| # | Ключ (`T[key]`) | Японское имя | Voice prefix | Английское* | Описание |
|---|----------------|-------------|-------------|-------------|----------|
| 1 | `Tatr` | アトリ | `atr` | Atri | Главная героиня |
| 2 | `Tcat` | キャサリン | `cat` | Catherine | Главная героиня |
| 3 | `Tryu` | 竜司 | `ryu` | Ryuji | |
| 4 | `Trir` | 凜々花 | `rir` | Ririka | |
| 5 | `Tmin` | 水菜萌 | `min` | Minamo | |
| 6 | `Tnat` | 夏生 | — | Natsuki | Протагонист (без голоса) |
| 7 | `Tyas` | ヤスダ | `YAS` | Yasuda | |

## Второстепенные (есть спрайты, без голоса)

| # | Ключ (`T[key]`) | Японское имя | Voice prefix | Английское* | Описание |
|---|----------------|-------------|-------------|-------------|----------|
| 8 | `Thiw` | 日和 | — | Hiyori | |
| 9 | `Tyac` | 八千代 | — | Yachiyo | |
| 10 | `Tnio` | 鳰 | — | Nio | |

## Эпизодические женские (озвучка, группа ●他女 / `wom`)

| # | Японское имя | Voice prefix | Английское* | Описание |
|---|-------------|-------------|-------------|----------|
| 11 | 洋子 | `XXA` | Yoko | |
| 12 | みよ | `XXB` | Miyo | |
| 13 | 水菜萌の母 | `XXC` | Minamo's Mother | |
| 14 | 乃音子 | `XXD` | Nonoko | |
| 15 | 店主 | `XXE` | Shopkeeper | |
| 16 | 肉屋のおばさん | `XXF` | Butcher Lady | |
| 17 | 詩菜 | `XXG` | Shina | |
| 18 | 駄菓子屋のおばちゃん | `XXH` | Candy Store Lady | |
| 19 | ココロ | `XXI` | Kokoro | |
| 20 | テレビ | `XXJ` | TV | |
| 21 | 打ち上げ中継 | `XYA` | Launch Broadcast | |

## Эпизодические мужские (озвучка, группа ●他男 / `man`)

| # | Японское имя | Voice prefix | Английское* | Описание |
|---|-------------|-------------|-------------|----------|
| 22 | ヤスダ | `YAS` | Yasuda | Дублирует Tyas |
| 23 | ラジオ | `XYA` | Radio | |
| 24 | ラジオＤＪ | `XYB` | Radio DJ | |
| 25 | 露店の店主 | `XYC` | Stall Owner | |
| 26 | とおる | `XYD` | Toru | |
| 27 | 中等部男子 | `XYE` | Middle School Boy | |
| 28 | 富田さん | `XYF` | Tomita-san | |
| 29 | ジャンク屋 | `XYG` | Junk Shop Owner | |
| 30 | 宅配業者 | `XYH` | Delivery Person | |
| 31 | 町の人Ａ | `XYI` | Townsperson A | |
| 32 | 町の人Ｂ | `XYJ` | Townsperson B | |

## Сценарные коды без известных японских имён

Эти `T[key]` используются в `macro.ks` как теги персонажей, но отсутствуют в `charlist.csv` / `charvoice.csv`:

| # | Ключ (`T[key]`) | cname | Возможное назначение |
|---|----------------|-------|---------------------|
| 33 | `Tren` | ren | |
| 34 | `Ttoh` | toh | |
| 35 | `Tchr` | chr | |
| 36 | `Tmur` | mur | |
| 37 | `Tshi` | shi | |
| 38 | `Tcho` | cho | |
| 39 | `Tban` | ban | |
| 40 | `Taya` | aya | |
| 41 | `Tbel` | bel | |
| 42 | `Tsil` | sil | |
| 43 | — | gak | Macro script only |
| 44 | — | har | Macro script only |
| 45 | — | mak | Macro script only |
| 46 | — | nog | Macro script only |

> **\*Английские имена** не существуют в данных игры. Колонка заполняется вручную для Neuro-интеграции.

---

## Цепочка идентификации персонажа

```
KAG сценарий: [Tnat ...]
  → charlist.csv: Tnat → 夏生.stand (спрайт)
  → charvoice.csv: 夏生 → voice prefix "nat" (голос)
  → runtime: characterVoiceInfo["夏生"] = {file:"nat", config:"●他男", ...}
  → backlog: currentInfo.name = "夏生", currentChara = "夏生"
  
Для подмены на английские в Neuro:
  dispNameFilter("夏生") → "Natsuki"
```

## Маппинг для Neuro (голосовые префиксы)

```javascript
// Определение персонажа по voice prefix из voice file path
var voiceNameMap = {
    "atr": "Atri", "cat": "Catherine", "min": "Minamo",
    "ryu": "Ryuji", "rir": "Ririka", "nat": "Natsuki",
    "yas": "Yasuda",
    "XXA": "Yoko", "XXB": "Miyo", /* ... и т.д. */
};
```
