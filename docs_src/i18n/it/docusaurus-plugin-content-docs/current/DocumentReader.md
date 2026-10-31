---
id: DocumentReader
title: Lettore di documenti
description: "Lo studio di un documento in Teca (sintesi, fatti principali, frammenti, OCR), la vista Leggi il documento e l'esportazione HTML"
version: "3.0"
---
# Lettore di documenti
![Status](https://img.shields.io/badge/Status-Teca%20only-purple) ![3.0](https://img.shields.io/badge/Version-3.0-blue)

## Riepilogo
Teca può **studiare** un documento di testo: lo legge e ne conserva una sintesi, i fatti principali e il documento diviso in frammenti, ognuno con il suo riassunto.

* [Lo studio di un documento](#study): cosa trova, e come si leggono le pagine scansionate (OCR).
* [Leggi il documento](#reader): la sintesi, i fatti e i frammenti in un'unica vista, anche con il disco non collegato.
* [Esporta in HTML](#export): una pagina web di un documento, di una cartella o di un intero catalogo, leggibile senza Teca.

Teca si basa su Katalog di Stéphane Couturier.

![Lettura di un documento: la sintesi, i fatti principali, poi il documento frammento per frammento](/img/documentreader_01_overview.png)

## Lo studio di un documento {#study}
L'analisi **Studio dei documenti** lavora sui documenti di testo. È attiva per impostazione predefinita nella sezione *Analisi AI*, quando si aggiunge un disco (*Aggiungi un disco*) o con *Analizza con l'AI* in *Sfoglia l'archivio*. Funziona su questo computer, in secondo piano; *Lavori in corso* mostra l'avanzamento.

Dal testo del documento, il modello di AI della collezione trova:

| Parte | Contenuto |
|-------|-----------|
| **Sintesi** | Il succo dell'intero documento, in poche righe. |
| **Fatti principali** | Le date, le persone, i luoghi, le organizzazioni, gli eventi e i destinatari che nomina. |
| **Tag** | Alcune parole che lo descrivono. |
| **Frammenti** | Il documento in ordine di lettura, diviso in parti, ognuna con le sue pagine e il suo riassunto. |
| **Collegamenti all'archivio** | I termini del glossario che usa, i documenti di riferimento e le persone dell'archivio a cui si collega (vedi *Conoscenza dell'archivio* e [Persone e volti](People)). |

Una persona nominata in un documento compare come menzione: non viene aggiunta da sola alle persone del file.

**Documenti scansionati.** Una pagina PDF senza testo (una scansione) viene letta con l'OCR, su questo computer, in italiano e in inglese. Un documento così mostra il tag *Letto con OCR*. Quando il programma di OCR manca, queste pagine vengono saltate, con il motivo.

Il modello di AI funziona su questo computer per impostazione predefinita: il documento e il suo testo non vengono inviati a Internet.

## In Dettagli del file {#file-details}
Per un documento studiato, *Dettagli del file* mostra la card **Studio del documento**: il tipo e la data del documento, l'inizio della sua sintesi e il pulsante *Leggi il documento*.

## Leggi il documento {#reader}
*Leggi il documento* apre la vista *Lettura:* seguita dal nome del file. Dall'alto in basso:
1. Il nome del documento, il tipo, la data e l'autore, con i tag *Letto con OCR* (per una scansione), la lingua e il numero di frammenti.
2. **Sintesi**.
3. **Fatti principali**: *Date*, *Persone*, *Luoghi*, *Organizzazioni*, *Eventi*, *Destinatari* e *Tag*.
4. **Nel contesto dell'archivio**: *Glossario*, *Documenti di riferimento* e *Persone dell'archivio*, quando il documento vi si collega.
5. **Il documento, frammento per frammento**: ogni parte con il suo titolo (o *Parte n*), le sue pagine, il suo riassunto e i suoi tag. *Mostra il testo* apre il testo del frammento sotto di esso; *Nascondi il testo* lo chiude.

La vista funziona con il disco collegato o no, e con la tastiera e un lettore di schermo. *Chiudi* (o `Esc`) la chiude.

Mentre un'analisi è in corso, la vista la segue senza perdere il punto di lettura né chiudere i frammenti aperti.

**Un documento non ancora studiato.** La vista lo dice, e offre *Studia i documenti di questo disco*: avvia lo studio del disco che contiene questo documento, in secondo piano.

## Esporta in HTML {#export}
**Esporta in HTML** scrive una pagina web che si legge in qualsiasi browser, senza Teca, e che si può condividere. Si trova:
* nel lettore di documenti, per quel documento;
* nella barra degli strumenti di *Sfoglia l'archivio*, con tre scelte: *Il file selezionato*, *Questa cartella*, *L'intero catalogo*. Per una cartella o un catalogo, la pagina inizia con un indice dei suoi file.

Una finestra di salvataggio chiede dove scrivere la pagina. Un file viene scritto subito. Una cartella o un intero catalogo vengono scritti in secondo piano: l'avanzamento si vede nel [pannello delle attività](Interface#activity-panel) in fondo alla finestra, e Teca continua a funzionare. Alla fine, una notifica dice *Esportato:* seguito dal percorso.

**Cosa contiene la pagina:** per ogni file, le sue informazioni e le sue persone, le proposte con le loro fonti, la descrizione e la classificazione fatte dall'AI, le anteprime, e per un documento il suo studio completo con tutti i frammenti. Non contiene mai dati dei volti.

**Accessibile:** titoli in ordine, regioni della pagina, testi alternativi per le immagini, la lingua della pagina e un'impaginazione per la stampa.

**Dove va:** non si invia nulla; la pagina viene scritta solo dove scegli. Una cartella di questo computer è il posto giusto: Teca legge soltanto i dischi catalogati.

:::note
La pagina contiene i nomi delle persone collegate ai file e le informazioni scritte su di loro. Condividere la pagina significa condividere anche questi dati.
:::
