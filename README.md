# TrapWin Sampler — https://trapwin.com

VST3 / AU nástroj (Windows a Mac) pro trapové producenty. Přetáhneš na vinyl song nebo loop (WAV, MP3, AIFF, FLAC) a plugin ho celý rozebere:

- **Loopy podle skutečných začátků.** Plugin rozezná, co hraje v každém taktu (kick, hi-haty, basa/808, melodie přes Basic Pitch) a najde místa, kde začíná nová část: *Intro, Kick In, Melody, Drop, Hook, Break, Outro*. Od každého začátku nařeže 8 a 16taktové loopy přes celý song. Opakované části (druhý refrén) přeskočí. Loopy jsou natažené na tempo projektu a plynule se opakují.
- **One-shoty, nic náhodného.** Údery jsou rozpoznané (*Kick, 808 s tónem, Snare, Hat, Open Hat, Perc*). Vybírají se nejčistší kusy: ticho před náběhem, nic dalšího přes zvuk, celý dozvuk, kicky bez hi-hatů. Z melodie vybere tóny a akordy, které zní samostatně (*Note C#5, Chord F#m*).
- **Editor dole.** Klikni na vzorek a pracuj s ním: ořez úchyty (u loopů po dobách), fade in/out, hlasitost, ladění (±12 půltónů, délka se nemění), reverse, reset. Úpravy se ukládají s projektem.
- **Přetažení do DAW kdykoli.** Řádek v seznamu nebo tlačítko *DRAG TO DAW* v editoru přetáhneš do FL Studia, Abletonu, Logicu a dalších. Přepínač **DRAG AS WAV / MIDI**: WAV s úpravami, nebo MIDI s notami (převod Basic Pitch, i akordy).
- **Piano Roll.** Vybraný vzorek hraje z MIDI: one-shoty chromaticky (nota 60 = původní výška), loopy spouští nota.
- **Tempo.** Plugin pozná tempo a tóninu samplu a čte tempo projektu. Tlačítka ×2 / ÷2 opraví dvojnásobné nebo poloviční tempo, BPM jde i ručně napsat.

Design odpovídá webu trapwin.com (šablona *TrapWin Chrome*): pozadí #04060A, chromový gradient, modrá záře #9FC3FF, písma Archivo a IBM Plex Mono.

## Stažení a instalace

GitHub Actions při každém pushi postaví:
- **TrapWinSampler-Windows:** VST3 + samostatná aplikace + instalátor (Inno Setup)
- **TrapWinSampler-Mac:** universal (Apple Silicon + Intel), VST3 + AU + aplikace
- **trapwin.com:** složka pro e-shop: `Windows/TrapWinSampler-1.0.0-Windows.zip` (instalátor, návod, EULA) a `macOS/TrapWinSampler-1.0.0-macOS.zip` (podepsané bundly, návod, EULA; bez podpisu má název `-UNSIGNED`)

Stáhneš je na stránce běhu workflow (Actions → Build → Artifacts).

### Podpis pro Mac

Stejné secrets jako u ostatních TrapWin pluginů (např. TrapWin Effector). Vlož je do tohoto repozitáře (Settings → Secrets and variables → Actions); klíče máš zálohované v `Dokumenty\TrapWin-signing`:

| Secret | Co to je |
|---|---|
| `MACOS_CERT_P12` | certifikát *Developer ID Application* jako .p12, zakódovaný base64 |
| `MACOS_CERT_PASSWORD` | heslo k .p12 |
| `MACOS_SIGN_IDENTITY` | volitelné, např. `Developer ID Application: Jméno (TEAMID)` (jinak se vezme z .p12) |
| `APPLE_API_KEY_P8`, `APPLE_API_KEY_ID`, `APPLE_API_ISSUER_ID` | notarizace přes App Store Connect API klíč |
| `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD` | nebo notarizace přes Apple ID a app-specific heslo |

Bez nich se Mac verze postaví nepodepsaná (ZIP s koncovkou `-UNSIGNED` a návodem, jak ji otevřít).

## Build

Požadavky: CMake ≥ 3.22, C++20. JUCE 8.0.9 se stáhne automaticky.

```sh
# Windows (Visual Studio 2022+)
cmake -S . -B build -A x64
cmake --build build --config Release

# Mac (universal)
cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release

# Linux
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Volby: `-DTRAPWIN_JUCE_PATH=/cesta/k/JUCE` (lokální JUCE), `-DTRAPWIN_BUILD_TOOLS=ON` (end-to-end test `TrapWinTest <složka> <song.wav> [analyse]`, který bez DAW projde načtení → analýzu → loopy/shoty → editor → přehrávání → WAV/MIDI export → uložení a obnovení stavu a vykreslí UI do PNG).

Kontrola pluginu: `pluginval --strictness-level 10 --validate "TrapWin Sampler.vst3"` (prochází).

## Struktura

```
source/
  PluginProcessor.*   AudioProcessor: MIDI, hlasitost, stav projektu
  PluginEditor.*      okno (velikost podle obrazovky, pamatuje si ji)
  core/               Engine (řízení), Edit (úpravy editoru), SampleStore (načtení),
                      JobQueue, LockFree, AudioTools
  analysis/           Features, Analyzer (tempo, takty, tónina), Structure (profil taktů, části songu)
  loops/              LoopGenerator (loopy od začátků částí, stretch, crossfade)
  oneshots/           OneShotExtractor (druhy úderů, čistota, tóny a akordy z Basic Pitch)
  midi/               BasicPitch (audio → noty, nativně v C++), MidiExport
  dsp/                Stretcher (time-stretch / pitch, signalsmith)
  export/             Exporter (WAV pro přetažení)
  playback/           SamplePlayer (náhled songu), ClipPlayer (náhledy, MIDI)
  ui/                 Theme + TrapWinLookAndFeel (design webu), Header, Vinyl,
                      SampleList, EditorPanel, FormatSwitch, MainView
packaging/       návody k instalaci do ZIPů pro e-shop
installer/       Windows instalátor (Inno Setup)
tools/                Snapshot.cpp (TrapWinTest), export_basic_pitch.py
```

## Licence třetích stran

| Komponenta | Licence |
|---|---|
| JUCE 8 | AGPLv3 **nebo** komerční licence JUCE. **Pro prodej je potřeba licence JUCE** (Starter zdarma do obratu 50 000 USD/rok). |
| VST3 SDK, AU SDK (součást JUCE) | MIT / Apple |
| signalsmith-stretch 1.4.0 + signalsmith-linear 0.6.4 (`third_party/`) | MIT |
| Basic Pitch (Spotify), váhy `assets/models/basic_pitch.bin` | Apache 2.0 (`third_party/basic-pitch/`) |
| Archivo, IBM Plex Mono | SIL OFL 1.1 (`assets/fonts/*-OFL.txt`) |
