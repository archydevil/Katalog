---
id: FileInformation
title: Informazioni sui file
description: "Le informazioni sui file in Teca: titolo, evento, luogo, data, autore, note, tag e persone scritti per un file o per molti, e i chip cliccabili di Dettagli del file"
version: "3.0"
---
# Informazioni sui file
![Status](https://img.shields.io/badge/Status-Teca%20only-purple) ![3.0](https://img.shields.io/badge/Version-3.0-blue)

## Riepilogo
Un file può avere informazioni scritte da te: cosa mostra, quando, dove, chi l'ha realizzato e chi c'è dentro. Le foto sono il caso principale, ma qualsiasi file può averle.

* I [campi](#fields): titolo, descrizione, evento, luogo, data, autore, note, tag.
* Per [un file](#one-file) o per [molti file insieme](#many-files).
* Le [persone](#people) presenti in un file, aggiunte a mano.
* I [chip cliccabili](#chips) di *Dettagli del file*, che trovano gli altri file con lo stesso evento, luogo, tag o persona.

Teca si basa su Katalog di Stéphane Couturier.

![Il modulo Informazioni di una foto: titolo, descrizione, evento, luogo, data, autore, tag, note e le persone presenti](/img/fileinformation_01_form.png)

Queste informazioni sono tue:
* restano separate dalle date e dai metadati del file, dalle descrizioni e classificazioni fatte dall'AI e dalle *Etichette* delle cartelle, e non le cambiano mai;
* sui dischi e nei file non si scrive nulla;
* si conservano quando la collezione viene chiusa e riaperta, e si vedono che il disco sia collegato o no;
* le [proposte](Proposals) dedotte da Teca non le sostituiscono mai.

:::note
Le informazioni sui file e le persone non sono disponibili per una collezione su un database ospitato (modalità *Hosted*).
:::

## I campi {#fields}
Ogni campo è facoltativo.

| Campo | Contenuto |
|-------|-----------|
| *Titolo* | Un nome per il file. |
| *Descrizione* | Cosa mostra o contiene. |
| *Evento* | L'occasione: un matrimonio, una partita, un viaggio. |
| *Luogo* | Dove è stato ripreso o realizzato. |
| *Data* | Testo libero, per esempio `2015-07-12` o `luglio 2015`. |
| *Autore* | Chi l'ha realizzato: il fotografo, chi l'ha scritto. |
| *Tag* | Parole libere, separate da virgole. |
| *Note* | Tutto il resto. |
| *Persone* | Le persone presenti: vedi [Persone](#people). |

## Un file {#one-file}
Il modulo *Informazioni* si apre in tre modi:
* in *Sfoglia l'archivio*, dal menu di un file (clic destro): *Aggiungi informazioni…*;
* in *Sfoglia l'archivio*, con l'azione *Informazioni* della barra degli strumenti, per il file selezionato;
* in *Dettagli del file* (in *Sfoglia l'archivio* e nei risultati di ricerca), con *Modifica le informazioni*.

Il modulo mostra i valori che il file ha già. *Salva* li scrive, e una notifica dice *Informazioni salvate.*; *Annulla* non cambia nulla. Un campo svuotato e salvato viene cancellato.

## Molti file insieme {#many-files}
Nell'elenco di *Sfoglia l'archivio*, più file si selezionano con il mouse:
* `Cmd`+clic su macOS (`Ctrl`+clic su Linux e Windows) aggiunge un file alla selezione o lo toglie;
* `Maiusc`+clic seleziona un intervallo di file.

*Informazioni (n)* nella barra degli strumenti, oppure *Aggiungi informazioni a n file…* nel menu di un file, apre allora *Informazioni per n file*. Il modulo parte vuoto:
* ai file si applicano solo i campi compilati; gli altri restano come sono in ciascun file;
* tag e persone vengono aggiunti a ogni file; i tag e le persone che un file ha già restano.

*Salva* li scrive, e una notifica dice *Informazioni salvate per n file.*

Un clic semplice seleziona sempre un solo file, e un doppio clic lo apre sempre. La selezione di più file funziona nella vista a elenco, con il mouse.

## Persone {#people}
Il modulo elenca le persone del file. Ogni persona compare come un pulsante: un clic la toglie dal file quando il modulo viene salvato.

*Aggiungi una persona* apre *Chi è?*:
* compaiono le persone dell'archivio, e il campo *Nome, ruolo o organizzazione…* restringe l'elenco; un clic sceglie una persona;
* sotto *Nuova persona*, *Aggiungi e scegli* crea una persona da *Nome*, *Cognome* e *Ruolo*, e la sceglie;
* *Annulla* chiude la finestra.

Per molti file, le persone scelte vengono aggiunte a ogni file.

Non serve un volto: una persona si può aggiungere a qualsiasi file, a un documento o a un video come a una foto. Una persona conta anche su una foto quando uno dei suoi volti è approvato per lei (vedi [Persone e volti](People)). La pagina *Persone* elenca le foto di ogni persona.

## In Dettagli del file {#file-details}
*Dettagli del file* mostra, per il file selezionato, la sezione **Informazioni e persone**: le informazioni del file e le sue persone. Quando non c'è ancora nulla, lo dice: *Ancora nulla di scritto: un titolo, un evento, un luogo, le persone presenti…*

| Pulsante | Effetto |
|----------|---------|
| *Modifica le informazioni* | Apre il modulo *Informazioni* per questo file. |
| *Aggiungi una persona* | Apre *Chi è?*; la persona scelta viene aggiunta subito al file. |

Per una foto con volti che non hanno ancora un nome approvato, una riga dice quanti sono, e i volti sono incorniciati sull'anteprima: vedi [I volti in Dettagli del file](People#file-details).

### Chip cliccabili {#chips}
L'evento, il luogo, ogni tag e ogni persona del file compaiono come **chip**. Attivando un chip (un clic, oppure `Invio` o `Spazio` con la tastiera) si trovano i file che hanno lo stesso valore nelle loro informazioni: le altre foto dello stesso evento, dello stesso luogo o della stessa persona. Il suo suggerimento dice *Trova i file che hanno questo nelle loro informazioni*.

Il chip avvia la ricerca *Nelle informazioni* con quel valore, nei dischi scelti in *I miei dischi*, e mostra i risultati: vedi [Ricerca nelle informazioni](SearchInformation#chips).

## Ricerca per significato
Le informazioni dei file e i nomi, i ruoli e le organizzazioni delle loro persone si trovano anche con la ricerca *Per significato* di *Cerca nei dischi*. Per questo vengono lette dal modello di AI della collezione, che per impostazione predefinita funziona su questo computer.
