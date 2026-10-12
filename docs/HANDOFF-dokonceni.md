Zadání pro nový chat: dokončit TrapWin Sampler a dát ho do prodeje

Pracuješ na mém počítači (Windows, Claude Desktop s přístupem k počítači). Udělej všechno sám, já jen potvrzuju. Nic se mě neptej, pokud to není nutné. Piš česky a stručně.

1. Kontext

Plugin TrapWin Sampler (VST3/AU, Windows a Mac) je hotový. Rozdělí song na stopy (vokál, bicí, 808, melodie), nařeže z něj 8/16taktové loopy a one-shoty a obsahuje editor vlny, převod audia na MIDI („skartovačka“) a jednoduché efekty (halftime, repeat, filtr, delay, reverb).

- Repo: https://github.com/txcjournal-byte/digga-killa, větev `claude/confident-bardeen-4yc3b2` (nemerguj, nedělej PR).
- CI: GitHub Actions, workflow „Build“ postaví Windows instalátor a Mac verzi. Výsledek je artefakt `trapwin.com` se složkou `TrapWin Sampler 1.0.0/` (`Windows/…zip`, `macOS/…zip`, `Docs`, `Shop`).
- Mac verze je zatím nepodepsaná: chybí GitHub Secrets v tomto repu.
- Shopify obchod: TrapWin (trapwin.com).
  - Produkt `gid://shopify/Product/11222750789970`, varianta `gid://shopify/ProductVariant/55018534207826`.
  - Je ve stavu DRAFT, cena 49 USD, bez přeškrtnuté ceny.
  - V popisu i na coveru je „$189 VALUE · GET IT FOR $49“.
  - Popis, cover, typ Plugin a tagy jsou hotové.
  - Interní název je zatím „TrapWin Sample“. Při zveřejnění ho přejmenuj na „TrapWin Sampler“: `publish-digital-product` s title „TrapWin Sampler“.

2. Úkoly v tomto pořadí

A) Podpis pro Mac
1. V `Dokumenty\TrapWin-signing` najdi podpisové soubory a hodnoty (stejné, jaké používá repo effector-killa).
2. V prohlížeči otevři https://github.com/txcjournal-byte/digga-killa/settings/secrets/actions a přidej tyto Repository secrets (New repository secret):
   - `MACOS_CERT_P12`: obsah .p12 certifikátu „Developer ID Application“ v base64 (v PowerShellu `[Convert]::ToBase64String([IO.File]::ReadAllBytes("cesta\cert.p12"))`).
   - `MACOS_CERT_PASSWORD`: heslo k .p12.
   - `APPLE_API_KEY_P8`: obsah souboru AuthKey_XXXX.p8 (celý text, i řádky BEGIN/END).
   - `APPLE_API_KEY_ID`: ID klíče (XXXX z názvu AuthKey_XXXX.p8).
   - `APPLE_API_ISSUER_ID`: Issuer ID z App Store Connect.
   - Volitelně, pokud je místo API klíče jen Apple ID: `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD`.
3. Hodnoty klíčů nikam nevypisuj ani neukládej mimo GitHub Secrets.

B) Nový build
1. Na https://github.com/txcjournal-byte/digga-killa/actions/workflows/build.yml dej „Run workflow“ na větvi `claude/confident-bardeen-4yc3b2`.
2. Počkej na dokončení (asi 25 minut).
3. V jobu macos krok „Sign & notarize“ musí napsat „signed …, notarized and stapled“. Mac ZIP se pak jmenuje `TrapWinSampler-1.0.0-macOS.zip`, bez přípony UNSIGNED.
4. Pokud podpis selže, přečti chybu, oprav secret a spusť build znovu.

C) Stažení
1. Stáhni artefakt `trapwin.com` z posledního úspěšného běhu.
2. Rozbal ho na plochu do složky `trapwin.com`.

D) Test ve FL Studiu (rychlý)
1. Spusť `TrapWinSampler-1.0.0-Windows-Setup.exe`, ve FL dej Options → Manage plugins → Find installed plugins a přidej TrapWin Sampler jako generator.
2. Přetáhni na vinyl nějaký celý song (MP3 nebo WAV) a zkontroluj:
   - rozbor doběhne (asi 20–60 s), objeví se loopy a záložky DRUMS / 808 & BASS / MELODY / VOCALS,
   - přetažení loopu do playlistu jako WAV, přepnutí na MIDI a přetažení do piano rollu,
   - okno se vejde na obrazovku a jde přečíst.
3. Udělej screenshot. Pokud něco nefunguje, napiš mi přesně co.

E) Shopify
1. Do produktu výše nahraj jako soubory ke stažení oba ZIPy: `Windows/TrapWinSampler-1.0.0-Windows.zip` a `macOS/TrapWinSampler-1.0.0-macOS.zip` (Shopify Digital Products, nebo MCP nástroj `upload-digital-product-file`).
2. Zveřejni produkt pod názvem „TrapWin Sampler“.
3. Ověř na trapwin.com, že je produkt vidět, cena je 49 USD a jde koupit.
4. Cenu ani popis neměň.

3. Na konci mi pošli

Krátké shrnutí: podpis OK/NE, test ve FL OK/NE (se screenshotem), odkaz na produkt v obchodě.
