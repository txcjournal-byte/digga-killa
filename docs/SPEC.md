# DIGGA KILLA – zadání pluginu (TrapVST)

## Co to je
DIGGA KILLA je VST3 nástroj (generator) pro trapové producenty. Uživatel do pluginu přetáhne sampl (celou písničku nebo kus loopu). Plugin sám najde nejlepší místa, vygeneruje z nich hotové loopy na 8 a 16 taktů a sadu one-shot zvuků. Tlačítkem KILL pak z libovolného loopu nebo one-shotu vygeneruje 6 přetvořených variací, a to opakovaně (variace z variací).

Žádné pady, žádné ruční chopování. Jednoduchý, rychlý plugin: přetáhni sampl → dostaneš loopy.

Vizuální předloha je v souboru `design.png` (vzhled „white label“ obalu vinylové desky).

## Technologie
- C++ (C++17 nebo novější), framework **JUCE**, build přes **CMake**.
- Formát: **VST3** pro Windows (hlavní cíl je FL Studio). Zároveň build **Standalone** pro testování. AU pro Mac později.
- Plugin je **nástroj** (načítá se do Channel Racku ve FL Studiu), ne efekt.
- Časové natahování a ladění: preferuj knihovnu s permisivní licencí (např. signalsmith-stretch, MIT). **Nepoužívej GPL/AGPL knihovny** (Rubber Band bez komerční licence, Essentia, aubio, libKeyFinder), protože plugin bude komerční. Detekci tempa a tóniny klidně napiš vlastní.
- Před přidáním jakékoli externí knihovny mi napiš její licenci.

## Funkce

### 1. Načtení samplu
- Drag & drop audio souboru na kruhový štítek desky uprostřed („DROP SAMPLE“).
- Podporované formáty: WAV, AIFF, MP3, FLAC.
- Načítání a analýza běží na pozadí (ne v audio vlákně), UI ukazuje stav zpracování.

### 2. Analýza
- Automatické rozpoznání **tempa (BPM)** a **tóniny**.
- Tempo projektu se čte z hostitele (AudioPlayHead).
- Zobrazení vpravo nahoře: `SAMPLE 94 BPM · F#m → PROJECT 140 BPM`, čitelně velké.
- Tlačítka **×2** a **÷2** na opravu detekce tempa (častá chyba 70 vs. 140) a možnost číslo ručně přepsat. Změna tempa znovu vygeneruje výsledky.

### 3. Generování loopů
- Rozděl sampl do taktů podle detekovaného tempa.
- Ohodnoť úseky: melodický obsah, energie, a hlavně jak dobře navazuje konec na začátek (loopovatelnost).
- Vyber nejlepší a vytvoř **4 loopy** (mix délek 8 a 16 taktů). Pokud je zdroj krátký, 16taktový loop poskládej z více úseků.
- Loopy přizpůsob tempu projektu (time-stretch bez změny výšky) a udělej čistý crossfade ve smyčce, aby necvakaly.
- Zobrazení jako tracklist: `A1 – Loop 8 bars`, `A2 – Loop 16 bars`, `B1`, `B2`. Délka se počítá z tempa (při 140 BPM je 8 taktů zhruba 14 s).

### 4. Generování one-shotů
- Detekce nástupů (onset detection) a vyříznutí jednotlivých zvuků od nástupu po dozvuk.
- Vyber **8 co nejrozmanitějších** zvuků (podle spektrálních vlastností), normalizuj je a přidej krátký fade-out.
- Zobrazení: `Shot 1` až `Shot 8`.

### 5. KILL (hlavní funkce)
- Každý řádek (loop i one-shot, včetně variací) má tlačítko KILL ve stylu razítka.
- KILL vygeneruje **6 variací** zobrazených odsazeně pod původním řádkem (`A2 – Kill Mix 1` až `Kill Mix 6`).
- KILL jde použít i na variaci, takže vzniká strom. Každé další kolo se vzdaluje od originálu.
- Transformace (kombinuj náhodně): přeskládání taktů a dob, reverse úseků, přeladění po tónech stupnice (v tónině samplu), halftime, stutter a zasekávání, filtrové průjezdy, výpadky not.
- Posuvník **KILL STRENGTH** (SOFT → BRUTAL) určuje, kolik transformací a jak výrazných se použije.
- Každá variace má uložený **seed**, aby šla přesně zopakovat.
- Možnost vrátit se o krok zpět (undo) a sbalit/rozbalit strom variací.

### 6. Přehrávání a MIDI
- Tlačítko play u každého řádku (náhled, přehrávání synchronizované s tempem).
- Vybraný loop nebo one-shot jde hrát z Piano Rollu: one-shoty chromaticky po klávesách (výchozí nota C3 = původní výška), loopy spouštěné notou.

### 7. Efekty (spodní řada)
- Knoby: **REVERB, DELAY** (synchronizovaný s tempem), **DISTORTION, FILTER** (jeden knob: doleva low-pass, doprava high-pass), **PITCH** (±12 půltónů), **MIX** (dry/wet).
- Přepínač **REVERSE**.
- Efekty se aplikují na přehrávání i na export přetažením.
- Použij `juce::dsp` moduly, všechny parametry jako `AudioProcessorValueTreeState` (automatizovatelné z DAW).

### 8. Export do DAW
- Každý řádek má úchyt pro přetažení (drag handle). Přetažením se výsledek vyrenderuje do WAV (s efekty) do dočasné složky a přetáhne se do FL Studia (Playlist nebo Browser) přes `performExternalDragDropOfFiles`.
- Název souboru obsahuje tempo a tóninu, např. `DiggaKilla_A2_KillMix3_140bpm_F#m.wav`.

### 9. Uložení stavu
- Při uložení projektu ve FL se uloží cesta k samplu, nastavení, seedy všech variací a stav stromu, aby se po otevření projektu vše obnovilo.

## Design (podle `design.png`)
- Pozadí: špinavě bílý, lehce opotřebovaný papír. Černá typografie, jediný akcentový tón je červená (inkoust razítka).
- Nahoře velký nápis **DIGGA KILLA** (těžké kondenzované písmo), vlevo logo **TrapVST** (velké T, malé „rap“, velké VST), vpravo tempo a tónina.
- Uprostřed kruhový štítek desky jako drop zóna.
- Vlevo sloupec LOOPS, vpravo ONE-SHOTS, dole efekty, REVERSE a KILL STRENGTH.
- Razítka KILL jsou ve výchozím stavu šedá/tlumená, červená jen při najetí myší a u vybraného řádku.
- Vybraný řádek zvýrazněný červeně.
- Žádné dekorace navíc. Rozhraní musí jít zvětšovat (resizable).

## Technické požadavky
- Žádné alokace paměti ani zámky v `processBlock`.
- Analýza, generování a KILL běží ve vlákně na pozadí.
- Plugin musí projít testem **pluginval**.
- Kód rozděl do přehledných modulů (analýza, generování loopů, one-shoty, KILL engine, efekty, UI).

## Postup práce
Stav po fázích. Po každé fázi se zastav, shrň, co je hotové, a počkej na mé potvrzení.

1. **Kostra:** JUCE projekt, VST3 + Standalone, prázdné UI podle designu, načtení samplu přetažením a jeho přehrání.
2. **Analýza:** detekce tempa a tóniny, čtení tempa z hostitele, ×2 / ÷2.
3. **Loopy:** generování 4 loopů, time-stretch, crossfade, náhled.
4. **One-shoty:** detekce a výběr 8 zvuků.
5. **KILL:** variace, strom, KILL STRENGTH, seedy, undo.
6. **Efekty a REVERSE.**
7. **Export přetažením do DAW, MIDI přehrávání, uložení stavu.**
8. **Ladění:** výkon, pluginval, dotažení designu.

Než začneš psát kód, navrhni strukturu projektu a plán první fáze.
