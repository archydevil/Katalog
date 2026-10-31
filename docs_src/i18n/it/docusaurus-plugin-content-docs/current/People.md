---
id: People
title: Persone e volti
description: "La pagina Persone di Teca: le persone dell'archivio, i volti trovati nelle foto e approvati da te, i volti simili, i gruppi di volti senza nome e la riservatezza dei dati dei volti"
version: "3.0"
---
# Persone e volti
![Status](https://img.shields.io/badge/Status-Teca%20only-purple) ![3.0](https://img.shields.io/badge/Version-3.0-blue)

## Riepilogo
La pagina **Persone** raccoglie le persone dell'archivio: chi sono e in quali foto compaiono. Si trova nella barra laterale, sotto *Intelligenza*.

* Le [persone](#people) e le loro informazioni: nome, ruolo, organizzazione, note.
* I [volti](#how-faces-are-found) trovati nelle foto. Teca propone soltanto chi è un volto: un volto prende un nome solo quando lo approvi, un volto alla volta o molti insieme.
* I [volti simili](#similar-faces) di una persona, nelle altre foto.
* I [volti senza nome](#faces-without-a-name), raggruppati per somiglianza, per dare un nome a un intero gruppo in una volta.
* [Ignora questi volti](#ignore) e [Mostra la foto](#face-menu).
* [Cancella tutti i dati dei volti](#delete-face-data).
* [Riservatezza](#privacy): i volti restano su questo computer.

Teca si basa su Katalog di Stéphane Couturier.

![Pagina Persone: in alto i volti da approvare, poi l'elenco delle persone, la persona scelta con le sue foto e i gruppi di volti senza nome](/img/people_01_overview.png)

:::note
Persone e volti non sono disponibili per una collezione su un database ospitato (modalità *Hosted*).
:::

## Persone {#people}
Una persona ha *Nome*, *Cognome*, *Ruolo*, *Organizzazione* e *Note*. Serve un nome o un cognome; gli altri campi sono facoltativi.

| Azione | Effetto |
|--------|---------|
| *Nuova persona* | Apre una scheda persona vuota. *Salva* aggiunge la persona. |
| *Nome, ruolo o organizzazione…* | Il filtro sopra l'elenco: mostra solo le persone il cui nome, ruolo o organizzazione contiene il testo digitato. |
| *Modifica* | Sulla persona scelta: apre la sua scheda, con *Salva*, *Elimina* e *Annulla*. |

Ogni persona è una card con il suo volto (o le sue iniziali), il ruolo e l'organizzazione, il numero di foto in cui compare e, quando dei suoi volti aspettano l'approvazione, quanti sono (*Da approvare: n*).

Scegliendo una persona compaiono:
* le sue note;
* *Foto (n)*: le foto in cui compare. Selezionandone una la si vede in *Sfoglia l'archivio*. Oltre le 60 foto, *Mostra tutte le n foto* mostra le altre;
* *Volti simili*: vedi [più sotto](#similar-faces).

Una foto conta per una persona in due modi: la persona le è stata aggiunta a mano (vedi [Informazioni sui file](FileInformation#people)), oppure uno dei suoi volti è stato approvato per quella persona.

Una persona si può anche creare sul momento, da una foto: *Aggiungi una persona* apre *Chi è?*, dove *Aggiungi e scegli* crea una nuova persona da nome, cognome e ruolo.

### Eliminare una persona {#delete-person}
*Elimina*, sulla scheda della persona, chiede prima *Eliminare questa persona?*.
* Se confermi, la persona non sarà più tra le persone né nelle foto in cui compare. I suoi volti tornano senza nome.
* Le foto e i file restano come sono: sui dischi e nei cataloghi non cambia nulla.
* *Annulla* non cambia nulla.

## Come si trovano i volti {#how-faces-are-found}
L'analisi **Volti delle persone** cerca i volti nelle foto di un disco, su questo computer, in secondo piano. *Lavori in corso* mostra l'avanzamento. È spenta per impostazione predefinita, e si avvia in uno di questi due modi:
* nella pagina *Persone*, nella card *Trova i volti in un disco*: si sceglie il disco nell'elenco, e *Trova i volti* avvia la ricerca;
* con l'interruttore *Volti delle persone* della sezione *Analisi AI*, quando si aggiunge un disco (*Aggiungi un disco*) o con *Analizza con l'AI* in *Sfoglia l'archivio*.

Nulla viene attribuito senza la tua approvazione: l'analisi trova i volti, poi propone chi sono quando ne hai già nominato qualcuno.

Quando questa copia di Teca non può riconoscere i volti (ne manca una parte o mancano i suoi modelli), la pagina dice perché. Le persone si possono comunque aggiungere alle foto a mano, da *Dettagli del file*.

### I primi nomi {#first-names}
All'inizio nessun volto ha un nome: i volti trovati sono raggruppati sotto [Volti senza nome](#faces-without-a-name). Dare un nome a qualche gruppo fornisce all'analisi i primi esempi. Da quel momento propone queste persone per i volti che somigliano ai loro, sotto [Volti da approvare](#faces-to-approve) e in [Volti simili](#similar-faces).

## Volti da approvare {#faces-to-approve}
L'analisi propone chi sono questi volti. Ogni volto mostra il nome proposto e quanto è simile (*Somiglianza: n%*). Prendono il nome solo quelli approvati; gli altri restano senza.

Ogni volto ha una spunta: un clic, oppure `Invio` o `Spazio` quando il volto ha il focus della tastiera, lo spunta o toglie la spunta. Passando sopra un volto con il mouse si vede da quale foto viene.

Con uno o più volti spuntati:

| Pulsante | Effetto |
|----------|---------|
| *Approva i selezionati (n)* | Ogni volto spuntato prende il nome proposto per lui. |
| *Non sono loro* | Le proposte vengono rimosse; i volti restano senza nome. Quella persona non viene più proposta per questi volti. |
| *Ignora i selezionati (n)* | Vedi [Ignorare i volti](#ignore). |

Anche la card *Da rivedere* di *Home* conta i volti da approvare, e apre questa pagina (vedi [Ricerca nelle informazioni](SearchInformation#to-review)).

## Volti simili {#similar-faces}
Per la persona scelta, *Cerca volti simili* elenca i volti nelle altre foto che somigliano ai volti approvati di quella persona, dai più simili. I volti che hanno già un nome, e quelli già respinti per quella persona, non compaiono.

Il confronto avviene in secondo piano (*Confronto dei volti…*). Se non trova nulla, un messaggio lo dice: approvare altri volti di questa persona aiuta.

| Pulsante | Effetto |
|----------|---------|
| *Attribuisci a …* | Seguito dal nome della persona e dal numero di volti spuntati: i volti spuntati prendono quel nome. |
| *Spunta tutti* | Spunta tutti i volti dell'elenco. |
| *Non è …* | Seguito dal nome della persona: i volti spuntati non sono quella persona, e non le vengono più proposti. |

Si può attribuire anche un volto alla volta: basta che sia spuntato solo quel volto.

## Volti senza nome {#faces-without-a-name}
I volti che si somigliano sono raggruppati, in modo che un gruppo contenga probabilmente una sola persona. Ogni gruppo mostra il numero dei suoi volti (*Volti: n*) e i primi 12; *Mostra tutti i n volti* mostra gli altri.

Come un gruppo prende un nome:
1. Si spuntano i volti che sono davvero la stessa persona.
2. *Dai un nome a questi volti* apre *Chi è?*: si sceglie una persona dell'elenco (il campo *Nome, ruolo o organizzazione…* restringe l'elenco), oppure se ne crea una nuova con *Aggiungi e scegli*.
3. I volti spuntati prendono quel nome, e contano ora tra le foto della persona.

Se nessun volto del gruppo è spuntato, il nome va a tutto il gruppo, compresi i volti non mostrati. Una conferma lo chiede prima: *Dare un nome a tutti i n volti del gruppo?* I gruppi sono approssimativi: spuntare prima i volti giusti è più sicuro. *Dai un nome a questi volti* conferma; *Annulla* non cambia nulla.

## Ignorare i volti {#ignore}
Alcuni volti non meritano un nome: un manifesto, una folla sullo sfondo, una forma scambiata per un volto. Ignorarli li toglie dagli elenchi:
* *Ignora i selezionati (n)*, sotto *Volti da approvare* o in un gruppo, per i volti spuntati;
* *Ignora questo volto*, nel [menu di un volto](#face-menu).

Una conferma lo chiede prima: *Ignorare n volti?*
* I volti ignorati non vengono più proposti né raggruppati, e nessun elenco della pagina li fa tornare.
* Non si cancella nulla: sulla loro foto, in *Dettagli del file*, i volti si vedono ancora e si possono ancora nominare.
* Una nuova ricerca dei volti li lascia ignorati.
* *Annulla* non cambia nulla.

## Il menu di un volto {#face-menu}
Ogni volto della pagina ha un menu. Si apre con *Altre azioni* (il pulsante *⋯* in alto a destra del volto), con un clic destro o con il tasto menu:
* **Mostra la foto** apre *Sfoglia l'archivio* sul disco e sulla cartella della foto, con la foto selezionata e mostrata in *Dettagli del file*. Funziona con il disco collegato o no. *Indietro* riporta a *Persone*. La scelta in *I miei dischi* non cambia.
* **Ignora questo volto**: vedi [Ignorare i volti](#ignore).

## I volti in Dettagli del file {#file-details}
In *Dettagli del file*, una foto con dei volti mostra un riquadro su ogni volto: verde quando il nome è approvato, nel colore dell'AI quando un nome è solo proposto. Passando sopra un riquadro si vede il nome, *Forse …* con il nome proposto, oppure *Chi è?*.

Un clic su un riquadro apre il suo menu:

| Voce | Effetto |
|------|---------|
| *È …* | Approva il nome proposto. |
| *Chi è?…* | Sceglie la persona in *Chi è?*. Per un volto che ha già un nome, la voce è *Un'altra persona…*. |
| *Non è …* | Respinge il nome proposto. |
| *Rimuovi il nome* | Il volto torna senza nome. |
| *Ignora questo volto* | Come nella pagina *Persone*. |

## Cancellare tutti i dati dei volti {#delete-face-data}
La card *Dati dei volti*, in fondo alla pagina, ha *Cancella tutti i dati dei volti*: dimentica tutti i volti di questa collezione. Una conferma dice prima cosa si cancella e cosa resta: *Cancellare tutti i dati dei volti?*

| Si cancella | Resta |
|-------------|-------|
| Ogni volto trovato in questa collezione, con il suo ritaglio. | Le persone. |
| I nomi dati attraverso i volti. | I nomi aggiunti alle foto a mano. |
| La traccia della ricerca dei volti, così una nuova ricerca riparte da zero. | Le informazioni dei file, e le foto stesse. |

*Elimina* conferma, e una notifica dice *Dati dei volti cancellati.*; *Annulla* non cambia nulla. Non è possibile mentre è in corso un'analisi AI. Una nuova ricerca dei volti si può fare in seguito.

## Riservatezza {#privacy}
I volti sono dati personali. Teca li tiene su questo computer:
* i volti vengono trovati e confrontati su questo computer, senza alcuna connessione a Internet;
* i volti, i loro ritagli e i nomi dati attraverso di essi non vengono mai inviati da nessuna parte, a nessun servizio di AI, qualunque siano le impostazioni;
* sono conservati solo nei dati dell'AI della collezione e, per i ritagli, nella cartella scelta per le anteprime;
* l'esportazione HTML non contiene mai dati dei volti;
* *Cancella tutti i dati dei volti* li elimina tutti, in qualsiasi momento.
