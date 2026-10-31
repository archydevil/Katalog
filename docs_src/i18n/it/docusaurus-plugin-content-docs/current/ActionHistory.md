---
id: ActionHistory
title: Storico delle azioni
description: "Lo storico delle azioni di Teca: i punti di ripristino dei cataloghi, Annulla, Ripristina com'era prima, Apri come catalogo a parte e la ricomposizione dei cataloghi divisi"
version: "3.0"
---
# Storico delle azioni
![Status](https://img.shields.io/badge/Status-Teca%20only-purple) ![3.0](https://img.shields.io/badge/Version-3.0-blue)

## Riepilogo
Ogni modifica a un catalogo lascia un **punto di ripristino**. La pagina **Storico delle azioni**, nella barra laterale sotto *Archivio*, mostra cosa è successo e può riportare i cataloghi a com'erano prima.

* [Punti di ripristino](#restore-points): le ultime 10 versioni di ogni catalogo.
* [Annulla](#undo), subito dopo un'azione.
* [Ripristina com'era prima](#restore) e [Apri come catalogo a parte](#open-separate).
* [Ricomporre i cataloghi divisi](#join) in uno solo.

Niente in questa pagina legge, scrive o cancella qualcosa sui dischi: lavora solo sui cataloghi.

Teca si basa su Katalog di Stéphane Couturier.

![Storico delle azioni: in alto i cataloghi divisi da ricomporre, poi le azioni dalle più recenti, ognuna con i suoi cataloghi e Ripristina com'era prima](/img/actionhistory_01_overview.png)

## Punti di ripristino {#restore-points}
Prima che Teca aggiorni, divida, ricomponga, modifichi o elimini un catalogo, registra un punto di ripristino di ogni catalogo coinvolto. Un punto di ripristino contiene il catalogo com'era: la sua definizione, l'elenco dei suoi file e delle sue cartelle e le sue impostazioni di analisi AI, con la data e una descrizione dell'azione.

* Si conservano le **ultime 10 versioni** di ogni catalogo. Quando se ne registra una più recente, la più vecchia oltre le 10 viene tolta.
* I punti di ripristino sono conservati nella cartella della collezione, separati dalla collezione stessa: registrarli non cambia la collezione.
* Lo storico ha bisogno della cartella della collezione su questo computer. Per una collezione su un database ospitato senza questa cartella, la pagina dice che lo storico non viene tenuto.

## Le azioni {#actions}
La sezione **Azioni** elenca le azioni registrate, dalle più recenti. Ognuna mostra:
* la sua descrizione (per esempio i cataloghi aggiornati) e la sua data;
* ogni catalogo coinvolto, con il numero di file che aveva allora, e *Apri come catalogo a parte*;
* *Ripristina com'era prima*;
* il tag *Annullata* quando è stata ripristinata.

Quando non è ancora cambiato nulla, la pagina lo dice: un'azione compare qui appena un catalogo viene aggiornato, diviso, ricomposto, modificato o eliminato.

## Annulla, subito dopo un'azione {#undo}
Subito dopo un'azione che ha registrato un punto di ripristino, una notifica dice cosa è stato fatto (per esempio *Aggiornato:* seguito dai nomi dei cataloghi) e offre **Annulla**. *Annulla* rimette subito i cataloghi com'erano, come fa *Ripristina com'era prima*.

## Ripristina com'era prima {#restore}
*Ripristina com'era prima*, su un'azione, rimette i suoi cataloghi come erano prima di essa.

Una conferma lo chiede prima, con la descrizione dell'azione: i cataloghi tornano com'erano prima; i file sui dischi non vengono toccati; i risultati dell'AI seguono; e prima si salva lo stato attuale, così anche questo si può annullare.

* *Ripristina* conferma. Una notifica dice *Ripristinato.*
* *Annulla* non cambia nulla.

Cosa succede poi:
1. Lo stato attuale dei cataloghi viene salvato come nuovo punto di ripristino.
2. I cataloghi nati dall'azione vengono sostituiti dalle versioni registrate.
3. I risultati dell'AI dei file — anteprime, descrizioni, trascrizioni, informazioni, persone, volti — seguono i loro file nei cataloghi ripristinati.

## Apri come catalogo a parte {#open-separate}
*Apri come catalogo a parte*, accanto a un catalogo di un'azione, aggiunge quella versione precedente alla collezione come **nuovo catalogo**. Il suo nome è il nome del catalogo seguito dalla data dell'azione, per esempio *Foto 2015 (versione del 30/10/2026 18:42)*.

* Il catalogo attuale e i suoi risultati dell'AI non cambiano.
* Il nuovo catalogo non ha risultati dell'AI: anteprime, descrizioni, trascrizioni e classificazioni non vengono copiate.
* Da quel momento è un catalogo come gli altri, in *I miei dischi*. Una notifica dice *La versione precedente ora è un catalogo a sé, in I miei dischi.*

Serve per vedere cosa conteneva un catalogo prima di un aggiornamento, senza perdere quello attuale.

## Ricomporre i cataloghi divisi {#join}
Un disco può contenere più cataloghi, uno per cartella, per esempio dopo che un catalogo è stato diviso. La sezione **Cataloghi divisi** elenca questi dischi: ogni card mostra il disco, quanti cataloghi contiene e in quale cartella, e i nomi dei cataloghi.

Come ricomporli:
1. *Ricomponi in un solo catalogo*, sulla card, apre una finestra.
2. La finestra dice quanti cataloghi diventano uno solo, con tutti i file, le cartelle e i risultati dell'AI, e che sul disco non si tocca nulla e prima si salva un punto di ripristino.
3. *Nome del catalogo* contiene il nome del nuovo catalogo; si può cambiare.
4. *Ricomponi* li riunisce; *Annulla* non cambia nulla.

Il risultato:
* un solo catalogo, la cui origine è la cartella madre comune dei cataloghi, con le sue sottocartelle;
* tutti i loro file e le loro cartelle, e tutti i loro risultati dell'AI, appartengono ora a esso;
* le impostazioni di analisi AI vengono riportate;
* i vecchi cataloghi vengono tolti dalla collezione;
* l'indice di ricerca del nuovo catalogo viene costruito in secondo piano.

Una notifica dice *I cataloghi sono di nuovo uno solo:* seguito dal nome. La ricomposizione è un'azione dell'elenco: si può annullare come le altre.
