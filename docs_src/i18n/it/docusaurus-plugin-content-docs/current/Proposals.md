---
id: Proposals
title: Proposte
description: "Le proposte di Teca: date, eventi e luoghi dedotti per i file, approvati per file o per cartella, ricerche sul web solo se attivate, e il tuo valore mai sostituito"
version: "3.0"
---
# Proposte
![Status](https://img.shields.io/badge/Status-Teca%20only-purple) ![3.0](https://img.shields.io/badge/Version-3.0-blue)

## Riepilogo
Teca deduce dei valori per le [informazioni](FileInformation) dei file — soprattutto la data, ma anche l'evento, il luogo, il titolo o l'autore — e li **propone**. Una proposta viene scritta nelle informazioni del file solo quando la approvi.

* [Da dove vengono le proposte](#sources): i metadati, i nomi, le cartelle, il contesto, i documenti di riferimento, e il web solo se è attivato.
* Approvare [per file](#per-file) o [per cartella](#per-folder).
* [Il tuo valore non viene mai sostituito](#your-value).
* [Ricerche sul web](#web), spente finché non vengono attivate per la collezione.

Teca si basa su Katalog di Stéphane Couturier.

![Proposte per questa cartella: la data e l'evento dedotti per i suoi file, ognuno con le sue fonti, la certezza e Approva per n file](/img/proposals_01_folder.png)

## Da dove vengono le proposte {#sources}
L'analisi **Date e contesto** esamina foto e video. Per ogni file raccoglie indizi sulla sua data e sugli altri campi delle sue informazioni, poi propone dei valori. Non scrive mai da sola le informazioni di un file.

| Indizio | Si vede come |
|---------|--------------|
| La data registrata in una foto, o l'ora di creazione di un video, letta dal file mentre il suo disco è collegato. | *Metadati della foto*, *Metadati del video* |
| Il nome del file. | *Nome file* |
| I nomi delle sue cartelle, per esempio `Monaco 2015`. | *Cartella* |
| Le date degli altri file della stessa cartella. | *Altri file della cartella* |
| Ciò che l'AI ha visto nell'immagine, compreso il testo visibile. | *Descrizione dell'immagine* |
| Il contesto dell'archivio e le note del catalogo, scritti in *Conoscenza dell'archivio*. | *Contesto dell'archivio* |
| I documenti di riferimento dell'archivio. | *Documento di riferimento* |
| Il web, solo se è attivato (vedi [Ricerche sul web](#web)). | *Web* |

Ogni proposta mostra il campo, il valore proposto, quanto Teca ne è sicura (*Certezza: n%*) e le sue fonti, a parole: per esempio *Metadati della foto: 12/07/2015* o *Cartella: Monaco 2015*.

L'analisi usa il modello di AI della collezione, che per impostazione predefinita funziona su questo computer. Lavora in secondo piano, dopo la creazione del catalogo, e solo sui file non ancora esaminati. È attiva per impostazione predefinita, e si avvia in uno di questi modi:
* con l'interruttore *Date e contesto* della sezione *Analisi AI*, quando si aggiunge un disco (*Aggiungi un disco*) o con *Analizza con l'AI* in *Sfoglia l'archivio*;
* con *Deduci date e contesto per questo disco*, in [Proposte per questa cartella](#per-folder).

## Approvare per un file {#per-file}
In *Dettagli del file* (in *Sfoglia l'archivio* e nei risultati di ricerca), la sezione **Proposte** elenca le proposte del file selezionato: *Dedotte dai metadati, dai nomi, dalla cartella, dal contesto e dai documenti di riferimento. Nulla viene scritto finché non lo approvi.*

| Pulsante | Effetto |
|----------|---------|
| *Approva* | Scrive il valore nelle informazioni del file. |
| *Scarta* | Scarta la proposta. Non viene più proposta per quel file finché la sua fonte resta la stessa. |

## Approvare per una cartella {#per-folder}
In *Sfoglia l'archivio*, l'azione *Proposte* della barra degli strumenti apre **Proposte per questa cartella**: le proposte dei file della cartella mostrata e delle sue sottocartelle, raggruppate per campo e valore. Ogni gruppo mostra il numero di file (*n file*), quanto Teca ne è sicura (*Certezza: n%*) e le sue fonti.

| Pulsante | Effetto |
|----------|---------|
| *Approva per n file* | Scrive il valore nelle informazioni di ogni file del gruppo. |
| *Scarta* | Scarta le proposte del gruppo. |
| *Deduci date e contesto per questo disco* | Avvia l'analisi per tutto il disco, in secondo piano. Le proposte compaiono nella finestra man mano. |
| *Chiudi* | Chiude la finestra. |

Quando la cartella non ha nulla da approvare, la finestra lo dice: le proposte compaiono dopo l'analisi AI del disco (*Date e contesto*).

La card *Da rivedere* di *Home* conta le proposte in attesa, e il suo pulsante *Sfoglia* apre *Sfoglia l'archivio* su una cartella che ne ha, con questa finestra aperta (vedi [Ricerca nelle informazioni](SearchInformation#to-review)).

## Il tuo valore non viene mai sostituito {#your-value}
Un valore che hai scritto tu è tuo: nessuna approvazione lo sostituisce, né per un file, né per una cartella, né per molti file insieme.

* Quando un campo del file ha già un tuo valore, la proposta lo mostra sotto il valore proposto: *Tuo: …*
* Approvando una proposta così, il tuo valore resta com'è. Un messaggio dice quante proposte restano da approvare, perché il file ha già un suo valore per quel campo.
* Il tuo valore cambia solo quando lo modifichi tu, con *Modifica le informazioni* (vedi [Informazioni sui file](FileInformation#one-file)).

## Ricerche sul web {#web}
Le ricerche sul web aiutano a datare eventi, luoghi e anni trovati nei nomi. Sono **spente per impostazione predefinita** e si attivano per ogni collezione separatamente.

In *Proposte per questa cartella*, la card **Cerca sul web**:
1. L'interruttore *Ricerche sul web consentite per questa collezione* le attiva o le spegne per la collezione.
2. *Prepara le ricerche* elenca le ricerche che verrebbero inviate. Quando i nomi non contengono eventi da verificare, lo dice.
3. Nulla esce da questo computer finché non si preme *Invia queste ricerche*.
4. Alla fine, una notifica dice quante ricerche sono state inviate e quante hanno avuto una risposta. Le risposte diventano fonti delle proposte, indicate come *Web*.

Escono da questo computer solo le parole cercate: mai un file, un'immagine, un'anteprima o un brano di testo. Le ricerche vanno a DuckDuckGo e a Wikipedia (in italiano e in inglese), senza account.

In *Home*, la card *Dati inviati* conta le ricerche web inviate, e dice quando le ricerche sul web sono consentite per la collezione.
