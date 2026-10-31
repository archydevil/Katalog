---
version: "3.0"
---
# Příkazová Řádka
![3.0](https://img.shields.io/badge/Version-3.0-blue)

## Shrnutí
Tato stránka popisuje dostupné funkce a možnosti příkazové řádky, které lze spouštět z konzole.
Bez potřeby grafického rozhraní lze tedy automatizovat některé úkoly.

Tyto příkazy fungují pod **Linuxem** a **macOS**.

![Výstup nápovědy příkazové řádky zobrazující dostupné akce a možnosti](/img/k2/commandlines_01_help.png)

## Syntaxe

```bash
Katalog [AKCE] [MOŽNOSTI] [ARGUMENTY]
```

### Spuštění příkazu `Katalog`
Příklady na této stránce používají jako název příkazu `Katalog`. Způsob, jak Katalog spustit z terminálu, závisí na způsobu instalace:

- **Flatpak** (Flathub): akce a možnosti se zadávají za identifikátor aplikace.
  ```bash
  flatpak run io.github.stephanecouturier.Katalog list_catalogs
  ```
  Flatpak také instaluje spouštěč s názvem `io.github.stephanecouturier.Katalog`, který lze volat přímo, pokud je jeho složka v `PATH`.
- **AppImage**: akce a možnosti se zadávají za soubor AppImage.
  ```bash
  ./Katalog_linux_v3.0-qt6.AppImage list_catalogs
  ```
- **macOS** (Teca, přenosná aplikace): program je uvnitř aplikace.
  ```bash
  /Applications/Teca.app/Contents/MacOS/Teca list_catalogs
  ```

Chcete-li psát `Katalog` jako v příkladech, definujte alias, například v `~/.bashrc`:
```bash
alias Katalog='flatpak run io.github.stephanecouturier.Katalog'
```

Příkazová řádka používá stejný soubor nastavení jako okno, takže bez `--collection` pracuje s kolekcí naposledy otevřenou v Katalogu.

## Akce

Následující akce jsou dostupné jako poziční argumenty:

### `list_catalogs`
Vypíše všechny katalogy s jejich ID, aktivním stavem a názvem.

**Použití:**
```bash
Katalog list_catalogs [MOŽNOSTI]
```

**Příklad:**
```bash
Katalog list_catalogs --verbose
```

![Výstup příkazové řádky se seznamem všech katalogů s ID, stavem a názvem](/img/k2/commandlines_02_list.png)

### `update_catalog`
Aktualizuje konkrétní Katalog podle ID zařízení.

**Použití:**
```bash
Katalog update_catalog <deviceID> [MOŽNOSTI]
```

**Argumenty:**
- `deviceID` - ID zařízení katalogu k aktualizaci

**Příklad:**
```bash
Katalog update_catalog 5
```
![Výstup příkazové řádky po aktualizaci konkrétního katalogu](/img/k2/commandlines_03_catalog.png)

### `update_all_active`
Aktualizuje všechny aktivní katalogy v kolekci.

**Použití:**
```bash
Katalog update_all_active [MOŽNOSTI]
```

**Příklad:**
```bash
Katalog update_all_active --verbose
```
![Výstup příkazové řádky po aktualizaci všech aktivních katalogů](/img/k2/commandlines_04_all.png)

### `search`
Provede vyhledávání pomocí posledních kritérií vyhledávání z historie, s volitelnými přepsáními.

**Použití:**
```bash
Katalog search [MOŽNOSTI]
```

**Příklad:**
```bash
Katalog search --text "dovolená" --type image --limit 100
```

### `analyze`
Spustí AI analýzu jednoho katalogu s profilem analýzy, který je pro něj uložen v aplikaci Teca: náhledy, popisky, přepisy, shrnutí a další analýzy, které profil vybírá, ve stejném pořadí jako v aplikaci. Stejně jako v aplikaci je analýza přírůstková: co už je analyzováno, se znovu nedělá, a zastavená analýza pokračuje tam, kde skončila.

**Použití:**
```bash
Katalog analyze <deviceID> [MOŽNOSTI]
```

**Argumenty:**
- `deviceID` - ID zařízení katalogu k analýze

**Příklad:**
```bash
Katalog analyze 5
```

Každý provedený krok vypíše jeden řádek: číslo souboru, analýzu, její výsledek a soubor. Krok, kterému chybí program nebo model (například model whisper.cpp pro přepisy), se přeskočí a důvod je uveden na jeho řádku. Kroky AI potřebují na tomto počítači spuštěnou Ollamu: když není dostupná, selžou a provedou se znovu při další analýze.

Zdroj katalogu musí být připojen. Na nepřipojený disk se nečeká: katalog se neanalyzuje a další spuštění v něm pokračuje.

### `analyze_all`
Spustí AI analýzu každého katalogu, který má uložený profil analýzy, jeden po druhém, a na konci vypíše shrnutí. Katalog, jehož disk není připojen, se přeskočí a započítá do shrnutí.

**Použití:**
```bash
Katalog analyze_all [MOŽNOSTI]
```

**Příklad:**
```bash
Katalog analyze_all --collection "/cesta/ke/kolekci"
```

### Zámek kolekce a zastavení
`update_catalog`, `update_all_active`, `analyze` a `analyze_all` drží po dobu běhu zámek kolekce (soubor `.teca.lock` ve složce kolekce). Nespustí se, dokud má Teca kolekci otevřenou nebo dokud ji používá jiné spuštění z příkazové řádky: skončí s návratovým kódem 3 a nic nezmění.

Ctrl+C nebo zastavení zaslané systémem (SIGTERM) ukončí analýzu čistě: dosud provedené analýzy zůstanou zachovány a další spuštění pokračuje.

## Obecné Možnosti

### `-h, --help`
Zobrazí informace o nápovědě a ukončí program.

### `-v, --version`
Zobrazí informace o verzi a ukončí program.

### `-c, --collection <cesta>`
Určuje cestu ke složce kolekce.

**Příklad:**
```bash
Katalog search --collection "/cesta/k/mé/kolekci"
```

### `--verbose`
Povolí podrobný výstup pro ladění a detailní informace.

**Příklad:**
```bash
Katalog list_catalogs --verbose
```

## Možnosti Vyhledávání

Tyto možnosti jsou dostupné při použití akce `search` pro přepsání kritérií vyhledávání:

### `--limit <číslo>`
Omezí počet souborů k zobrazení ve výsledcích vyhledávání.

**Příklad:**
```bash
Katalog search --limit 50
```

### `--selectedDeviceID <deviceID>`
Určuje ID zařízení, ve kterém se má vyhledávat.
- Výchozí: používá hodnotu ze souboru nastavení
- Při použití s `--collection`: výchozí je 0 (Všechna zařízení)

**Příklad:**
```bash
Katalog search --selectedDeviceID 2
```

### `--text <hledaný-výraz>`
Určuje text nebo frázi k vyhledání.

**Příklad:**
```bash
Katalog search --text "rodinné fotky"
```

### `--type <typ-souboru>`
Filtruje výsledky podle typu souboru.

**Platné hodnoty:**
- `all` (výchozí)
- `audio`
- `image`
- `text`
- `video`

**Příklad:**
```bash
Katalog search --type audio
```

### `--size-min <velikost>`
Nastaví filtr minimální velikosti souboru.

**Formát:** Číslo následované jednotkou (např. 1MB, 5GB)

**Příklad:**
```bash
Katalog search --size-min 1MB
```

### `--size-max <velikost>`
Nastaví filtr maximální velikosti souboru.

**Formát:** Číslo následované jednotkou (např. 100MB, 2GB)

**Příklad:**
```bash
Katalog search --size-max 100MB
```

### `--date-after <datum>`
Filtruje soubory upravené po zadaném datu.

**Formát:** YYYY-MM-DD

**Příklad:**
```bash
Katalog search --date-after 2023-01-01
```

### `--date-before <datum>`
Filtruje soubory upravené před zadaným datem.

**Formát:** YYYY-MM-DD

**Příklad:**
```bash
Katalog search --date-before 2023-12-31
```

### `--case-sensitive`
Povolí vyhledávání textu citlivé na velikost písmen.

**Příklad:**
```bash
Katalog search --text "MůjSoubor" --case-sensitive
```

### `--search-in <rozsah>`
Definuje rozsah vyhledávání pro shodu textu.

**Platné hodnoty:**
- `filenames` (výchozí)
- `files-and-folders`
- `folder-paths`

**Příklad:**
```bash
Katalog search --text "dokumenty" --search-in folder-paths
```

### `--text-criteria <kritéria>`
Určuje, jak má být vyhledáván text.

**Platné hodnoty:**
- `all-words` (výchozí)
- `exact-phrase`
- `begins-with`
- `any-word`

**Příklad:**
```bash
Katalog search --text "dovolená fotka" --text-criteria exact-phrase
```

### `--exclude <výrazy-k-vyloučení>`
Vyloučí soubory obsahující zadané výrazy.

**Příklad:**
```bash
Katalog search --text "fotka" --exclude "záloha temp"
```

### `--no-history`
Začne s výchozími kritérii vyhledávání místo načítání z historie vyhledávání.

**Příklad:**
```bash
Katalog search --no-history --text "novýsoubor"
```

## Návratové Kódy

- **0**: Úspěch
- **1**: Chyba nebo selhání (neplatná možnost nebo akce, kolekce nenalezena, katalog neaktualizován nebo neanalyzován)
- **2**: Analýza provedena, ale některé kroky selhaly (například Ollama nebyla dostupná); provedou se znovu příště
- **3**: Kolekci používá Teca nebo jiné spuštění z příkazové řádky; nic nebylo provedeno
- **4**: Tento počítač nesmí do kolekce zapisovat; nic nebylo provedeno
- **130** / **143**: Analýza zastavena pomocí Ctrl+C (SIGINT) nebo SIGTERM; dosud provedené analýzy zůstanou zachovány
- **-1**: Interní kód pro pokračování v GUI režimu (nevrací se uživateli)

## Příklady

### Základní Správa Katalogů

Vypsat všechny katalogy:
```bash
Katalog list_catalogs
```

Aktualizovat konkrétní Katalog s podrobným výstupem:
```bash
Katalog update_catalog 3 --verbose
```

Aktualizovat všechny aktivní katalogy:
```bash
Katalog update_all_active
```

### Příklady Vyhledávání

Jednoduché textové vyhledávání:
```bash
Katalog search --text "dovolená"
```

Vyhledat obrázky větší než 5MB:
```bash
Katalog search --type image --size-min 5MB
```

Vyhledat soubory upravené v roce 2023:
```bash
Katalog search --date-after 2023-01-01 --date-before 2023-12-31
```

Složité vyhledávání s více kritérii:
```bash
Katalog search --text "projekt" --type text --search-in files-and-folders --case-sensitive --limit 200
```

Vyhledat v konkrétním zařízení:
```bash
Katalog search --selectedDeviceID 2 --text "dokumenty" --verbose
```

### Použití Vlastní Cesty Kolekce

Vyhledat v jiné kolekci:
```bash
Katalog --collection "/cesta/ke/kolekci" search --text "fotky"
```

Aktualizovat katalogy v konkrétní kolekci:
```bash
Katalog --collection "/cesta/ke/kolekci" update_all_active
```

## Plánované spouštění (macOS)
Mac, který zůstává zapnutý, může sám udržovat katalogy aktuální a analyzované, například každou noc, aby ostatní Macy dostávaly aktuální kopii. Teca neinstaluje žádný plánovač: macOS spouští příkazy pomocí **launchd**.

Tento soubor, uložený jako `~/Library/LaunchAgents/local.teca.nightly.plist`, aktualizuje aktivní katalogy a poté analyzuje každý katalog s uloženým profilem, každou noc ve 02:30. `USERNAME` označuje uživatelské jméno v macOS.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>local.teca.nightly</string>
    <key>ProgramArguments</key>
    <array>
        <string>/bin/sh</string>
        <string>-c</string>
        <string>/Applications/Teca.app/Contents/MacOS/Teca update_all_active; /Applications/Teca.app/Contents/MacOS/Teca analyze_all</string>
    </array>
    <key>StartCalendarInterval</key>
    <dict>
        <key>Hour</key>
        <integer>2</integer>
        <key>Minute</key>
        <integer>30</integer>
    </dict>
    <key>StandardOutPath</key>
    <string>/Users/USERNAME/Library/Logs/teca-nightly.log</string>
    <key>StandardErrorPath</key>
    <string>/Users/USERNAME/Library/Logs/teca-nightly.log</string>
</dict>
</plist>
```

Jednou se načte pomocí:
```bash
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/local.teca.nightly.plist
```

Spuštění lze pro kontrolu vyvolat hned pomocí:
```bash
launchctl kickstart gui/$(id -u)/local.teca.nightly
```

Dobré vědět:
- Bez `--collection` příkazy používají kolekci naposledy otevřenou v aplikaci Teca. S `--collection "/cesta/ke/kolekci"` za každou akcí používají tuto kolekci.
- Znak `;` mezi oběma příkazy spustí analýzu i tehdy, když aktualizace selhala.
- Mac musí být zapnutý a jeho uživatel přihlášený: LaunchAgent běží v relaci uživatele. Pro kroky AI musí být spuštěná Ollama.
- Dokud má Teca kolekci otevřenou, oba příkazy skončí s návratovým kódem 3 a nic nezmění.
- Plánované spuštění nemůže odpovědět na dotazy macOS na přístup ke složkám a externím diskům. Když jsou katalogy na externích discích nebo v chráněných složkách, potřebuje Teca tento přístup předem, v **Nastavení systému > Soukromí a zabezpečení** (Plný přístup k disku, nebo Soubory a složky).
- Každé spuštění zapíše své řádky a shrnutí do souboru protokolu; návratové kódy jsou uvedeny výše.

## Poznámky

- Když není zadána žádná akce, Katalog se spustí v GUI režimu
- Kritéria vyhledávání jsou ve výchozím nastavení načtena z historie vyhledávání, pokud není použito `--no-history`
- Možnosti příkazové řádky přepisují hodnoty z historie vyhledávání
- Všechny operace vyhledávání respektují aktivní stav katalogů
- Podporované jednotky velikosti souborů: KB, MB, GB, TB (necitlivé na velikost písmen)
- Formáty data musí být ve formátu YYYY-MM-DD

## Řešení Problémů

**Neplatné ID zařízení:**
Ujistěte se, že ID zařízení existuje spuštěním `Katalog list_catalogs` nejprve.

**Problémy s připojením k databázi:**
Ověřte, že cesta kolekce je správná a přístupná.

**Vyhledávání nevrací žádné výsledky:**
Zkuste použít `--no-history` pro začátek s výchozími kritérii, nebo zkontrolujte, zda vybrané zařízení obsahuje indexované soubory.

**Chyby oprávnění:**
Ujistěte se, že Katalog má přístup pro čtení/zápis ke složce kolekce a souborům databáze.
