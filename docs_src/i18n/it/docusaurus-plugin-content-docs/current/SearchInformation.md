---
id: SearchInformation
title: Ricerca nelle informazioni
description: "La ricerca Nelle informazioni di Teca, i chip di Dettagli del file, la card Da rivedere in Home, e cosa succede quando una ricerca non trova nulla"
version: "3.0"
---
# Ricerca nelle informazioni
![Status](https://img.shields.io/badge/Status-Teca%20only-purple) ![3.0](https://img.shields.io/badge/Version-3.0-blue)

## Riepilogo
Questa pagina descrive tre cose che aiutano a trovare e a rivedere ciò che è stato scritto sui file:

* il modo di cercare [Nelle informazioni](#in-the-information), in *Cerca nei dischi*, e i [chip](#chips) di *Dettagli del file* che lo avviano;
* la card [Da rivedere](#to-review) in *Home*;
* cosa succede [quando una ricerca non trova nulla](#nothing-found).

Gli altri criteri di ricerca sono descritti in [Cerca nei dischi](Search), e i risultati in [Risultati della ricerca](SearchResults) (pagine in inglese).

Teca si basa su Katalog di Stéphane Couturier.

![Cerca nei dischi con Nelle informazioni scelto, e i suoi risultati in un elenco a parte](/img/searchinformation_01_overview.png)

## Nelle informazioni {#in-the-information}
In *Cerca nei dischi*, la card **Cosa cerchi** offre tre modi di cercare. Se ne possono attivare uno o più insieme:

| Modo | Dove cerca |
|------|------------|
| *Per nome* | Nei nomi dei file e delle cartelle. |
| *Per significato* | Anche con parole diverse, nelle descrizioni, nei riassunti e nelle trascrizioni fatte dall'AI. |
| **Nelle informazioni** | Nel titolo, evento, luogo, data, autore, note, tag e persone scritti per i file. |

*Nelle informazioni* trova i file le cui [informazioni](FileInformation) — o i nomi, i ruoli e le organizzazioni delle cui persone — contengono le parole digitate nel campo *Un nome, un evento, un luogo, un tag…*:
* trova le parole **così come sono scritte**, non per significato: ogni parola cercata deve essere presente;
* maiuscole e minuscole non contano;
* cerca nei cataloghi dei dischi scelti in *I miei dischi*, come gli altri modi;
* i risultati di una ricerca per nome non cambiano.

`Invio` nel campo, oppure *Cerca*, avvia la ricerca.

### I risultati {#results}
I file trovati compaiono in un elenco a parte nella pagina dei risultati, sotto **Nelle informazioni**: *File le cui informazioni o persone contengono ogni parola cercata. Ognuno si apre in Sfoglia.* Un clic su un file, oppure `Invio`, lo apre in *Sfoglia l'archivio*, selezionato e mostrato in *Dettagli del file*.

Quando nulla corrisponde, l'elenco dice *Nessun file ha queste parole nelle sue informazioni.*

*Nelle informazioni* trova le parole esatte. Per trovare le informazioni con altre parole dello stesso significato, *Per significato* cerca anche nelle informazioni e nelle persone dei file.

:::note
*Nelle informazioni* non è disponibile per una collezione su un database ospitato (modalità *Hosted*).
:::

### Da Dettagli del file: i chip {#chips}
In *Dettagli del file*, l'evento, il luogo, ogni tag e ogni persona di un file compaiono come chip (vedi [Informazioni sui file](FileInformation#chips)). Attivando un chip si avvia la ricerca *Nelle informazioni* con quel solo valore, e se ne vedono i risultati: per esempio tutte le foto dello stesso evento, o tutti i file in cui compare la stessa persona.

## Da rivedere, in Home {#to-review}
La card **Da rivedere** di *Home* dice ciò che aspetta la tua approvazione, ogni voce con il suo numero:

| Voce | Pulsante | Apre |
|------|----------|------|
| *Volti da approvare* | *Persone* | La pagina [Persone](People#faces-to-approve), dove i volti proposti si approvano o si respingono. |
| *Proposte da approvare* | *Sfoglia* | *Sfoglia l'archivio* su una cartella con delle proposte, con [Proposte per questa cartella](Proposals#per-folder) aperta. |

* La card compare solo quando qualcosa è in attesa.
* I suoi numeri vengono riletti mentre *Home* è aperta, per esempio durante un'analisi.
* *Home* non aggiunge azioni proprie: le approvazioni si fanno nei posti che apre.
* La card non compare per una collezione su un database ospitato.

## Quando una ricerca non trova nulla {#nothing-found}
Quando una ricerca non trova alcun file, la pagina dei risultati mostra la card **Cerca di nuovo con una modifica**: *Non è stato trovato nulla. La stessa ricerca può ripartire in un altro modo o in più posti.*

Ogni pulsante riavvia subito la stessa ricerca, con una modifica:

| Pulsante | Modifica |
|----------|----------|
| *Nomi file o percorsi cartelle* | Cerca le parole anche nei percorsi delle cartelle, quando la ricerca guardava solo i nomi dei file. |
| *Per significato* | Cerca le stesse parole per significato. Compare quando il modello di AI su questo computer è raggiungibile. |
| *Nelle informazioni* | Cerca le stesse parole nelle informazioni dei file. |
| *Tutti i dischi* | Esegue la ricerca in tutti i dischi, quando era stata fatta in un solo disco o gruppo. La scelta in *I miei dischi* non cambia. |

Un modo già usato dalla ricerca non viene riproposto. Nient'altro cambia nei criteri di ricerca.
