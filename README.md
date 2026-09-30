# DIGGA KILLA — TrapVST

VST3 nástroj pro trapové producenty: přetáhni sampl → dostaneš loopy a one-shoty → **KILL** z nich udělá variace.
Zadání: [`docs/SPEC.md`](docs/SPEC.md), vizuální předloha: [`docs/design.png`](docs/design.png).

## Stav

| Fáze | Obsah | Stav |
|---|---|---|
| 1 | Kostra: JUCE projekt, VST3 + Standalone, UI podle designu, drag & drop samplu, přehrání | ✅ |
| 2 | Analýza: tempo, tónina, tempo z hostitele, ×2 / ÷2 | — |
| 3 | Loopy: 4 loopy, time-stretch, crossfade, náhled | — |
| 4 | One-shoty | — |
| 5 | KILL: variace, strom, síla, seedy, undo | — |
| 6 | Efekty a REVERSE | — |
| 7 | Export přetažením, MIDI, uložení stavu | — |
| 8 | Ladění, pluginval, design | — |

## Build

Požadavky: CMake ≥ 3.22, kompilátor s C++20. JUCE 8.0.9 se stáhne automaticky (FetchContent).

### Windows (hlavní cíl, FL Studio)

Visual Studio 2022 s workloadem *Desktop development with C++*:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
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
- `-DDIGGA_BUILD_TOOLS=ON`: postaví `DiggaKillaSnapshot`, který bez hostitele vykreslí UI do PNG (kontrola proti designu):
  `DiggaKillaSnapshot <výstupní složka> [sampl.wav]`

### pluginval

```sh
pluginval --strictness-level 10 --validate "build/DiggaKilla_artefacts/Release/VST3/Digga Killa.vst3"
```

## Struktura

```
source/
  PluginProcessor.*   AudioProcessor: parametry (APVTS), MIDI, stav projektu
  PluginEditor.*      okno pluginu, škálování (pevný poměr stran)
  core/               JobQueue (vlákno na pozadí), SampleStore (načtení + převzorkování)
  playback/           SamplePlayer (předávání bufferů bez zámků a alokací)
  ui/                 Theme (barvy, fonty, grunge), LookAndFeel, Header, RecordLabel,
                      TrackRow / TrackColumn, FxPanel, PaperBackground, MainView
  analysis/ loops/ oneshots/ kill/ dsp/ export/   (další fáze)
tools/Snapshot.cpp    headless snímek UI
assets/fonts/         Anton, Courier Prime (SIL OFL)
```

Pravidla pro vlákna:
- Audio vlákno nikdy nealokuje, nezamyká a neuvolňuje paměť.
- Nové buffery předává `SamplePlayer::setSample()` přes atomický ukazatel, staré se uvolňují na message threadu (`collectGarbage`).
- Dekódování, převzorkování a později i analýza, generování a KILL běží v `JobQueue`.

## Licence třetích stran

| Komponenta | Licence |
|---|---|
| JUCE 8 | AGPLv3 **nebo** komerční licence JUCE. Pro komerční prodej je potřeba licence JUCE (Starter zdarma do obratu 50 000 USD ročně). |
| VST3 SDK (součást JUCE) | MIT (SDK 3.8+) |
| libFLAC (součást JUCE) | BSD |
| Anton, Courier Prime | SIL Open Font License 1.1 (`assets/fonts/*-OFL.txt`) |
| pluginval (jen testovací nástroj, nelinkuje se) | GPLv3 |

Knihovny GPL/AGPL se do pluginu nelinkují (kromě JUCE, pro které je potřeba komerční licence, viz výše).
