# DIGGA KILLA — TrapVST

VST3 nástroj pro trapové producenty: přetáhni sampl → dostaneš loopy a one-shoty → **KILL** z nich udělá variace.
Zadání: [`docs/SPEC.md`](docs/SPEC.md), vizuální předloha: [`docs/design.png`](docs/design.png).

## Stav

Všechny fáze zadání jsou hotové a připravené k testu ve FL Studiu.

| Fáze | Obsah | Stav |
|---|---|---|
| 1 | Kostra, VST3 + Standalone, UI podle designu, drag & drop samplu | ✅ |
| 2 | Detekce tempa a tóniny, tempo z hostitele, ×2 / ÷2, ruční BPM | ✅ |
| 3 | 4 loopy (8/16 taktů), time-stretch na tempo projektu, crossfade smyčky | ✅ |
| 4 | 8 nejrozmanitějších one-shotů | ✅ |
| 5 | KILL: 6 variací, strom, KILL STRENGTH, seedy, undo, sbalování | ✅ |
| 6 | Efekty (reverb, delay, distortion, filter, pitch, mix) a REVERSE | ✅ |
| 7 | Export přetažením do DAW, hraní z Piano Rollu, uložení stavu | ✅ |
| 8 | Ladění, pluginval (strictness 10), end-to-end test | ✅ |

## Jak se používá

1. **Přetáhni sampl** (WAV, AIFF, MP3, FLAC; celá písnička nebo kus loopu) na desku uprostřed, nebo na ni klikni a vyber soubor.
   Plugin v pozadí najde tempo a tóninu a vytvoří loopy A1, A2, B1, B2 a Shot 1–8. Samply do pluginu dáváš jen ty, sám žádné neobsahuje.
2. **Tempo:** vpravo nahoře `SAMPLE 94 BPM · F#m → PROJECT 140 BPM`. Když detekce sekne dvojnásobek nebo polovinu, klikni ×2 / ÷2. Na číslo můžeš kliknout a BPM napsat ručně. Loopy se pak přegenerují a strom KILL se zachová.
3. **Náhled:** ▶ u řádku. Loop hraje ve smyčce a když hraje projekt, je zarovnaný na takt.
4. **KILL:** razítko KILL u libovolného řádku (i u variace) vytvoří 6 variací pod ním. KILL STRENGTH vlevo = jemné, vpravo = brutální.
   - trojúhelník u řádku nebo dvojklik: sbalit / rozbalit variace
   - pravé tlačítko: KILL znovu, sbalit, **Undo**, zkopírovat seed
   - Ctrl+Z (Cmd+Z): undo posledního KILL
5. **Piano Roll:** klikni na řádek (zčervená) a hraj notami. One-shot: chromaticky, MIDI nota 60 = původní výška (ve FL Studiu se zobrazuje jako C5). Loop: nota ho spustí od začátku a hraje, dokud ji držíš.
6. **Efekty** dole platí pro přehrávání i export. REVERSE přehrává pozpátku.
7. **Do projektu:** chyť úchyt ⠿ u řádku a přetáhni ho do Playlistu nebo Browseru ve FL. Vyrenderuje se WAV s efekty, např. `DiggaKilla_A2_KillMix3_140bpm_F#m.wav`. Soubory jsou v dočasné složce `%TEMP%\DiggaKilla`.
8. **Uložení projektu** uloží cestu k samplu, nastavení a seedy celého stromu. Po otevření se vše přesně obnoví (sampl musí zůstat na stejném místě).

## Build

Požadavky: CMake ≥ 3.22, kompilátor s C++20. JUCE 8.0.9 se stáhne automaticky (FetchContent).

### Windows (hlavní cíl, FL Studio)

Visual Studio 2022 nebo novější s workloadem *Desktop development with C++*:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

Výstupy:
- `build\DiggaKilla_artefacts\Release\VST3\Digga Killa.vst3` → zkopíruj do `C:\Program Files\Common Files\VST3\`
- `build\DiggaKilla_artefacts\Release\Standalone\Digga Killa.exe`

Ve FL Studiu: *Options → Manage plugins → Find installed plugins*, pak přidej **Digga Killa** do Channel Racku.

### Linux / macOS

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Na Linuxu je potřeba: `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libfreetype-dev libfontconfig1-dev`.

Volby:
- `-DDIGGA_JUCE_PATH=/cesta/k/JUCE`: použije lokální JUCE místo stahování.
- `-DDIGGA_BUILD_TOOLS=ON`: postaví `DiggaKillaSnapshot`, end-to-end test bez hostitele. Projde načtení → analýzu → loopy/shoty → KILL → náhled → MIDI → export → uložení a obnovení stavu a vykreslí UI do PNG:
  `DiggaKillaSnapshot <výstupní složka> <tvůj-sampl.wav>`

### pluginval

```sh
pluginval --strictness-level 10 --validate "build/DiggaKilla_artefacts/Release/VST3/Digga Killa.vst3"
```

## Grafika (skin)

UI je přesně podle `docs/design.png`. Statická grafika (papír, nápis, logo, hlavičky, deska, knoby, popisky, čáry) se bere 1:1 z obrázku, živé prvky (řádky, waveformy, tempo, ukazatele knobů, přepínač, slider, stav desky) se kreslí na přesné pozice z designu.

```sh
pip install pillow numpy
python3 tools/make_skin.py      # docs/design.png → assets/skin/*.png
```

Po úpravě designu stačí nahradit `docs/design.png` a skript spustit znovu. Pro ostrý obraz při zvětšení se hodí design ve 2× rozlišení (2688 × 1792). Souřadnice pak stačí vynásobit v `tools/make_skin.py` i v kódu UI.

## Struktura

```
source/
  PluginProcessor.*   AudioProcessor: parametry (APVTS), MIDI, stav projektu
  PluginEditor.*      okno pluginu, škálování (pevný poměr stran)
  core/               Engine (řízení), ResultTree (strom + undo), JobQueue (pozadí),
                      SampleStore (načtení), LockFree, AudioTools
  analysis/           Features (spektrum, onsety), Analyzer (tempo, první doba, tónina)
  loops/              LoopGenerator (skóre taktů, loopovatelnost, stretch, crossfade)
  oneshots/           OneShotExtractor (onsety, konec dozvuku, výběr rozmanitosti)
  kill/               KillEngine (přeskládání, reverse, stupnice, halftime, stutter, filtr, výpadky)
  dsp/                Stretcher (offline), FxChain (efekty, realtime i offline)
  export/             Exporter (WAV pro přetažení do DAW)
  playback/           SamplePlayer (náhled zdroje), ClipPlayer (náhledy, MIDI, bez alokací)
  ui/                 Theme (barvy, fonty, skin), LookAndFeel, TempoDisplay, RecordLabel,
                      TrackRow / TrackColumn, FxPanel, MainView
tools/Snapshot.cpp    headless snímek UI (i s ukázkovými řádky jako v designu)
tools/make_skin.py    generátor skinu z docs/design.png
assets/skin/          pozadí a sprity vyříznuté z designu
assets/fonts/         Archivo Black, Barlow Condensed, Courier Prime (SIL OFL)
```

Pravidla pro vlákna:
- Audio vlákno nikdy nealokuje, nezamyká a neuvolňuje paměť.
- Nové buffery předává `SamplePlayer::setSample()` přes atomický ukazatel, staré se uvolňují na message threadu (`collectGarbage`).
- Dekódování, analýza, generování a KILL běží v `JobQueue` (6 variací KILL paralelně).
- Variace jsou dané rodičem, silou a seedem. Po obnovení projektu vyjdou bit po bitu stejně (ověřuje to test).

## Licence třetích stran

| Komponenta | Licence |
|---|---|
| JUCE 8 | AGPLv3 **nebo** komerční licence JUCE. Pro komerční prodej je potřeba licence JUCE (Starter zdarma do obratu 50 000 USD ročně). |
| VST3 SDK (součást JUCE) | MIT (SDK 3.8+) |
| libFLAC (součást JUCE) | BSD |
| signalsmith-stretch 1.4.0 + signalsmith-linear 0.6.4 (`third_party/`) | MIT |
| Archivo Black, Barlow Condensed, Courier Prime | SIL Open Font License 1.1 (`assets/fonts/*-OFL.txt`) |
| pluginval (jen testovací nástroj, nelinkuje se) | GPLv3 |

Knihovny GPL/AGPL se do pluginu nelinkují (kromě JUCE, pro které je potřeba komerční licence, viz výše).
