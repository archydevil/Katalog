---
id: ArchiveCopy
title: Copia dei dati dell'archivio
description: "La copia dei dati dell'archivio in Teca: cosa contiene, come funziona in secondo piano, il promemoria dopo 30 giorni, e perché titoli, eventi, persone e volti approvati ne hanno bisogno"
version: "3.0"
---
# Copia dei dati dell'archivio
![Status](https://img.shields.io/badge/Status-Teca%20only-purple) ![3.0](https://img.shields.io/badge/Version-3.0-blue)

## Riepilogo
**Salva una copia…**, in *Home*, scrive una copia datata dei dati dell'archivio in una cartella scelta da te: la collezione, i suoi dati dell'AI, lo storico delle azioni e le impostazioni.

* [Perché è importante](#why): titoli, eventi, persone e volti approvati esistono solo nella collezione.
* [Cosa contiene la copia](#content).
* [Come salvare una copia](#how): funziona in secondo piano, e Teca continua a lavorare.
* [Il promemoria](#reminder) dopo 30 giorni.

Teca si basa su Katalog di Stéphane Couturier.

![Home con la card Copia dei dati dell'archivio: la data dell'ultima copia e Salva una copia…](/img/archivecopy_01_home.png)

## Perché è importante {#why}
Un catalogo si può sempre rifare: Teca rilegge il disco. Il lavoro che aggiungi sopra, invece, no:
* i titoli, gli eventi, i luoghi, le date e le note scritti per i file;
* le persone, e i volti approvati per loro;
* le proposte approvate;
* il contesto dell'archivio e lo storico delle azioni.

Tutto questo esiste **solo nella collezione**, su questo computer. Non è sui dischi, e non è dentro i file. Se il disco di questo computer si guasta, o la cartella della collezione va persa, si perde con essa. Una copia tenuta altrove — un disco esterno, un disco di backup — lo mette al sicuro.

## Cosa contiene la copia {#content}
Ogni copia è una **nuova cartella**, con la data e l'ora nel nome, per esempio `Teca copy 2026-10-31 18-45`, dentro la cartella scelta. Contiene una copia di:

| Parte | Contenuto |
|-------|-----------|
| La collezione | Il suo file di database, oppure i file della sua cartella. |
| I dati dell'AI | Le descrizioni, le trascrizioni, i riassunti e gli studi fatti dall'AI, le informazioni dei file, le persone, i volti, le proposte e il contesto dell'archivio. |
| Lo storico delle azioni | I punti di ripristino dei cataloghi (vedi [Storico delle azioni](ActionHistory)). |
| Le impostazioni | Il file delle impostazioni. |

* Ogni parte viene copiata solo se esiste.
* La copia è coerente anche mentre Teca continua a scrivere, per esempio durante un'analisi AI.
* Una copia precedente non viene mai sovrascritta né cancellata: ogni copia ha la sua cartella.
* I file copiati non vengono modificati, spostati o cancellati.
* Le immagini e i clip delle anteprime, tenuti nella loro cartella di destinazione, non fanno parte della copia.

Per una collezione su un database ospitato, la copia contiene tutto tranne quel database, che è conservato sul suo server; la card lo dice prima che la copia inizi.

## Come salvare una copia {#how}
1. In *Home*, la card **Copia dei dati dell'archivio** mostra la data dell'ultima copia (*Ultima copia:* seguito dalla data), oppure *Ancora nessuna copia*.
2. *Salva una copia…* apre una finestra per scegliere una cartella. La cartella deve già esistere, e stare fuori dalla cartella della collezione: un disco esterno è un buon posto.
3. La copia funziona in secondo piano. L'avanzamento si vede sulla card e nel [pannello delle attività](Interface#activity-panel). Nel frattempo l'analisi AI continua, e Teca continua a funzionare.
4. Alla fine, una notifica dice *Copia salvata in* seguito dalla cartella, e la card mostra la nuova data.

Quando la copia non si può salvare, la card dice perché, e non viene scritto nulla:
* *Per la copia serve una cartella esistente.*
* *Per la copia serve una cartella fuori dalla cartella della collezione.*
* la destinazione non ha abbastanza spazio libero: il messaggio indica lo spazio necessario e quello rimasto;
* *Si sta già salvando una copia dei dati dell'archivio.*

## Il promemoria {#reminder}
In cima a *Home* compare un promemoria quando:
* non è ancora stata salvata nessuna copia dei dati dell'archivio, oppure
* l'ultima copia ha **più di 30 giorni**.

Ricorda che titoli, eventi, persone e volti approvati esistono solo in questa collezione, e offre *Salva una copia…*. Il tag della card *Copia dei dati dell'archivio* diventa allora un avviso. Una volta salvata una copia, il promemoria scompare per 30 giorni.

La data dell'ultima copia è conservata per ogni collezione separatamente.
