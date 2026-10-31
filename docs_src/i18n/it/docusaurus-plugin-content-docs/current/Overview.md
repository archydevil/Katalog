---
version: "3.0"
---
# Panoramica
![3.0](https://img.shields.io/badge/Version-3.0-blue)

## Indicizzazione dei file e gestione dei dispositivi di archiviazione
Katalog è un <b>gestore di cataloghi di file</b> che aiuta a organizzare e ritrovare i file.<br/>
Offre una visione completa dei file su <b>più dispositivi di archiviazione, senza bisogno che siano collegati</b>.<br/>
Katalog crea <b>indici con cui cercare in tutta la tua raccolta di file da un unico posto</b>, che si tratti di dischi interni ed esterni, chiavette USB, archivi di rete o dischi ottici (Blu-ray, DVD, CD).

- **Cataloga tutto**: indici dettagliati dei file di qualsiasi dispositivo di archiviazione.
- **Cerca offline**: trova i file senza collegare o montare il dispositivo originale.
- **Organizza la raccolta**: dispositivi di archiviazione e cataloghi in un'unica gerarchia.
- **Backup**: copia o sposta i file che mancano in un backup, grazie ai cataloghi.

![Finestra di Katalog: le pagine Selezione, Ricerca e Risultati affiancate](/img/interface_01_overview.png)

## Pagine
La finestra mostra a sinistra la pagina **[I miei dischi](Selection)**: l'albero di tutti i dispositivi della raccolta. La scelta di un dispositivo decide su cosa lavorano le altre pagine. Ogni funzione si apre come pagina accanto a essa, dalla barra laterale (vedi [Interfaccia](Interface)):

1. [Cerca nei dischi](Search): cerca i file su tutti i dispositivi di archiviazione, **senza bisogno che siano collegati**, e agisci sui [risultati](SearchResults).
1. [Gestisci i dischi](Devices): organizza i dispositivi e i loro cataloghi in un'unica gerarchia, con dispositivi **virtuali** per raggrupparli, e [modificali](DevicesEdit).
1. [Sfoglia l'archivio](Explore): le cartelle e i file di un catalogo.
1. [Aggiungi un disco](Create): crea i cataloghi dei file.
1. [Copie di sicurezza](BackUp): copia o sposta i file con i collegamenti di backup, e controlla la copertura dei backup.
1. [Numeri dell'archivio](Statistics): segui la crescita della raccolta.
1. [Etichette](Tags): assegna etichette alle cartelle, per avere più possibilità di ricerca.
1. [Impostazioni](Settings): la raccolta e l'applicazione, cioè modalità dei dati, importazione, tema, lingua.

---
## Funzionalità principali

### Informazioni ricche sui file
- **[Riconoscimento del tipo di file](Create#file-type)**: il tipo di ogni file si riconosce dall'estensione, per centinaia di formati.
- **[Opzioni del catalogo](Create#content-options)**: includere solo un [tipo di file](Create#file-type), le sottocartelle o no, i [file nascosti](Create#include-hidden-files), ed [escludere cartelle o file](Create#global-parameters).
- **[Estrazione dei metadati](Create#include-metadata)**: dimensioni di immagini e video, durata di video e file audio, artista e album della musica, o qualsiasi metadato di altri tipi di file.
- **[Checksum dei file](Create#include-checksum)**: checksum SHA-256 per ricerche esatte di duplicati e differenze, e per verificare i file.
- **[Etichette delle cartelle](Tags)**: organizza e classifica le cartelle con le tue etichette.

### Ricerca e scoperta
- **[Criteri di ricerca](Search)**: trova i file per nome (più testi, esclusioni, espressioni regolari), cartella, dimensione, data, tipo di file e metadati.
- **[Trova i duplicati](Search#duplicates-on)**: individua i file duplicati tra i dispositivi.
- **[Trova le differenze](Search#differences-on)**: vedi le differenze tra due dispositivi, per esempio una cartella e il suo backup.
- **[Azioni sui risultati](SearchResults#batch-process)**: filtra i risultati, esportali, verifica i checksum, estrai i metadati, sposta nel cestino.

### Dispositivi e cataloghi
- **[Gerarchia dei dispositivi](Devices#model)**: gruppo fisico, dispositivi di archiviazione e cataloghi, e gruppi virtuali.
- **[Aggiornamento](Devices#context-menu)**: tieni aggiornati i cataloghi, uno per uno o tutti quelli disponibili in una volta.
- **[Cambi di percorso](DevicesEdit#path-change)**: quando un disco viene montato altrove, correggi tutti i suoi cataloghi in pochi secondi, senza rileggere il disco.
- **[Importazione e aggiornamento di una raccolta](Settings#import-update)**: importa dispositivi da un'altra raccolta Katalog o da VVV, e aggiornali in seguito.

### Analisi e gestione
- **[Esplora file](Explore)**: sfoglia il contenuto di un catalogo come se il dispositivo fosse collegato, con la dimensione di ogni cartella.
- **[Statistiche](Statistics)**: segui la crescita della raccolta e dello spazio di archiviazione.
- **[Backup e archivio](BackUp)**: copia o sposta file tra cataloghi, con anteprima, più collegamenti in una volta e controllo della copertura.
- **[Modalità dei dati](Settings#database-memory-mode)**: tieni la raccolta in una cartella di file, in un database SQLite o in un database ospitato.

### Teca {#teca}
Teca, basata su Katalog di Stéphane Couturier, aggiunge:
- **[Persone e volti](People)**: le persone dell'archivio, e i volti delle foto, che prendono un nome solo con la tua approvazione.
- **[Informazioni sui file](FileInformation)**: titolo, evento, luogo, data, autore, note, tag e persone, per un file o per molti.
- **[Proposte](Proposals)**: date, eventi e luoghi dedotti per i file, da approvare.
- **[Lettore di documenti](DocumentReader)**: lo studio di un documento, e la sua esportazione come pagina HTML.
- **[Storico delle azioni](ActionHistory)**: i punti di ripristino dei cataloghi, per tornare a com'erano.
- **[Copia dei dati dell'archivio](ArchiveCopy)**: una copia datata di ciò che esiste solo nella collezione.
- **[Ricerca nelle informazioni](SearchInformation)**: la ricerca *Nelle informazioni*, la card *Da rivedere* di *Home*, e le ricerche che non trovano nulla.

### Katalog 2
Katalog 2 resta disponibile, con la sua [documentazione](Katalog2), compresa la [riga di comando](k2/CommandLines) (Linux).

---
## Piattaforme supportate {#multi-platform-support}

| Sistema operativo | Distribuzioni / versioni minime    | Pacchetti    |
|-------------------|-------------|-------------|
| <div style={{display: 'flex', alignItems: 'center', gap: '10px'}}><img src={require('/img/linux.png').default} width="40" /> GNU/Linux</div>         | 64 bit, glibc 2.38+ <br/>32 bit, glibc 2.35     | [FlatHub/Flatpak](https://flathub.org/en/apps/io.github.stephanecouturier.Katalog)<br/>[AppImage](https://sourceforge.net/projects/katalogg/files/) |
| <div style={{display: 'flex', alignItems: 'center', gap: '10px'}}><img src={require('/img/windows.png').default} width="40" /> Microsoft Windows</div> | 64 bit: Windows 10 e Windows 11    | [Programma di installazione](https://sourceforge.net/projects/katalogg/files/) <br/> [Portabile](https://sourceforge.net/projects/katalogg/files/)       |
| <div style={{display: 'flex', alignItems: 'center', gap: '10px'}}><img src={require('/img/macos.png').default} width="40" /> Apple macOS</div>       | 15 (Sequoia) | [Programma di installazione](https://sourceforge.net/projects/katalogg/files/)<br/> [Portabile](https://sourceforge.net/projects/katalogg/files/)       |


### Compatibilità con macOS

- **Versione minima di macOS**: macOS 15 (Sequoia) o successiva
- **Provato su**: macOS 15 (Sequoia), Apple Silicon
- **Architettura**: solo Apple Silicon (M1/M2/M3). I Mac Intel **non** sono supportati: Rosetta 2 traduce il software Intel per farlo funzionare su Apple Silicon, non il contrario, quindi questa versione non può funzionare su un Mac Intel.
- **Ambiente di compilazione**: compilata su macOS 15 (Sequoia) con Qt 6 e KDE Frameworks 6 (KF6), tramite Homebrew

**Importante: l'app non è firmata né autenticata da Apple.**

macOS blocca l'app per impostazione predefinita. Per avviare Katalog, uno di questi metodi:

1. Clic destro sull'app → «Apri» (non doppio clic) → conferma nella finestra
2. Oppure Impostazioni di Sistema → Privacy e sicurezza → scorri in basso e fai clic su «Apri comunque»
3. Oppure nel Terminale: `xattr -cr /percorso/di/Katalog.app`, poi apri normalmente. Aggiungi `sudo` davanti se l'autorizzazione viene negata.

**Limite noto**:

sui Mac gestiti da un'organizzazione (profilo MDM/aziendale), questi metodi possono essere bloccati dalle regole di sicurezza. Per ora non c'è soluzione per i Mac gestiti: servirebbe firmare l'app con un certificato Apple Developer.

È disponibile anche una versione precedente (v2.5, solo Qt6, senza dipendenze KF6), con gli stessi requisiti di macOS.

### App portabile Teca per macOS {#teca-macos}
Il titolare del fork Teca prepara un'app Teca portabile per macOS, come DMG, per i propri Mac. Non è pubblicata, ed è un pacchetto diverso dai pacchetti macOS descritti sopra.

| Voce | Dettaglio |
|------|-----------|
| **Mac** | Solo Apple Silicon. |
| **Sistema** | macOS 26 (Tahoe) o successivo. |
| **Firma** | Firma ad hoc, senza autenticazione Apple: macOS chiede conferma al primo avvio. |
| **Dentro l'app** | I programmi dell'analisi AI (video e audio, parlato in testo, testo e pagine dei PDF, OCR, metadati delle foto) e i loro modelli (parlato, rilevamento della voce, volti, OCR in italiano e in inglese). |
| **Non incluso** | Ollama, il servizio di AI locale per descrizioni, riassunti e ricerca per significato: si installa a parte. |

**Primo avvio.** L'app arriva da un DMG e non è autenticata da Apple, quindi Gatekeeper la ferma la prima volta:
1. Si apre il DMG e si trascina *Teca* in *Applicazioni*.
2. Al primo avvio, macOS dice che non può verificare l'app, e non la apre.
3. In *Impostazioni di Sistema › Privacy e sicurezza*, verso il fondo, compare *Apri comunque* per Teca; macOS chiede conferma ancora una volta, e la apre.
4. Da quel momento Teca si apre normalmente.

**Programmi e modelli.** Un programma o un modello indicato nelle *Impostazioni*, o già installato sul Mac, ha la precedenza; la copia dentro l'app si usa solo quando non c'è nessuno dei due. Per sceglierla non si scrive nulla nelle impostazioni.

:::note
Una sola Teca alla volta su una collezione: l'app portabile e un'altra Teca (un'altra copia, o una versione compilata dai sorgenti) non vanno mai aperte sulla stessa collezione nello stesso momento. Scriverebbero entrambe gli stessi file. Prima di aprire quella collezione con l'una, l'altra va chiusa.
:::

---
## Lingue supportate

### Lingue dell'applicazione
Katalog è disponibile in:

| Locale | Lingua | Locale | Lingua | Locale | Lingua |
|--------|----------|--------|----------|--------|----------|
| bg_BG  | Bulgaro    | hi_IN  | Hindi      | pl_PL  | Polacco    |
| cs_CZ  | Ceco       | hr_HR  | Croato     | pt_PT  | Portoghese |
| da_DK  | Danese     | hu_HU  | Ungherese  | ro_RO  | Rumeno     |
| de_DE  | Tedesco    | id_ID  | Indonesiano | si_SI | Sloveno    |
| en_US  | Inglese    | it_IT  | Italiano   | sk_SK  | Slovacco   |
| es_ES  | Spagnolo   | ja_JP  | Giapponese | sr_RS  | Serbo      |
| et_EE  | Estone     | lt_LT  | Lituano    | sv_SE  | Svedese    |
| fi_FI  | Finlandese | lv_LV  | Lettone    | uk_UA  | Ucraino    |
| fr_FR  | Francese   | nb_NO  | Norvegese  | zh_CN  | Cinese     |
| el_GR  | Greco      | nl_NL  | Olandese   |        |            |

### Lingue della documentazione
La documentazione è disponibile in:
| Locale   | Lingua    |
|----------|-------------|
| en_US    | Inglese     |
| cs_CZ    | Ceco        |
| fr_FR    | Francese    |
| it_IT    | Italiano (le pagine delle funzioni di Teca) |

---

**Pronto a organizzare la tua raccolta di file?** Inizia dal **[Tutorial](tutorial)** per creare il tuo primo catalogo in meno di 5 minuti.
