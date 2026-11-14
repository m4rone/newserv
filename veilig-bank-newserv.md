# veilig-bank: bank bij de duw na de basis (B-REGIE-21-20) en herhaalplanning (B-REGIE-21-21)

Tak `spike/veilig-bank`, gemaakt op `4641c07` (spike/veilig-k7b).

| | sha |
|---|---|
| bankverzoening | `16f516cc09de98ec5db58c3f418662fb2c5cc6d3` |
| herhaalplanning | `68375cc7a9af79ca93d7bbbc9d2d5e8100b32496` |
| binary (`build/newserv help \| head -1`) | `newserv-16f516c+ built 2026-11-14 10:00:28 UTC`: gebouwd uit de boom die daarna `68375cc` werd (de revisie wordt bij het bouwen gelezen, vóór de commit) |

Eindstand `build/newserv run-static-tests`: exit 0, `-- All static tests passed`, 24 GUARD E2E (13 van 4641c07 plus 11 nieuwe), 0 FAILED.
SpireDrops: `git diff --quiet ee0fa66 HEAD -- src/SpireDrops.cc src/SpireDrops.hh; echo $?` geeft `0`.
Diff t.o.v. 4641c07: alleen `src/SpireFlagBridge.cc` (+173 −3), `.hh` (+34), `src/StaticTests.cc` (+427). De bankfix voegt alleen regels toe; de startlevel-aanroepen zijn niet geraakt.

## 1. Bankverzoening

### Per geval: rood zonder fix, groen met fix

Rood gemeten op de binary met de tests en zonder fix (bron 4641c07 plus de tests), groen op `16f516c`.

| geval | zonder fix | met fix |
|---|---|---|
| (a) storting, val vóór de kaartschrijf | `FAILED: the deposited item is there 0 times after the login` | groen; log: `1 item(s) back, 0 taken out` |
| (b) opname, dezelfde val | `FAILED: the withdrawn item is there 2 times after the login` | groen; log: `0 item(s) back, 1 taken out (still in the bank)` |
| (c) beloning, dezelfde val | groen | groen: 10 Drops, vlag dicht |
| (d) a, b, c en meseta, maar de kaartschrijf komt aan | groen | groen (geen duw, er verandert niets) |
| (e) opraap door een ander (K7b) | groen | groen |
| (e) ruil, partner blijft | groen | groen |
| (e) ruil, partner was al weg | groen | groen |
| meseta-storting, dezelfde val | `FAILED: meseta and bank meseta are 827000 after the login, 831321 before the base` | groen; log: `meseta +4321` |

De letterlijke regels (`.lab/tests-red.log`):
```
GUARD E2E (B-REGIE-21-20 (a) the base game, a deposit on the bank, the client drops before the card write: the item exists once) FAILED: the deposited item is there 0 times after the login
GUARD E2E (B-REGIE-21-20 (b) the base game, a withdrawal from the bank, the client drops before the card write: the item exists once) FAILED: the withdrawn item is there 2 times after the login
GUARD E2E (B-REGIE-21-20 (meseta) the base game, a meseta deposit on the bank, the client drops before the card write: no meseta lost) FAILED: meseta and bank meseta are 827000 after the login, 831321 before the base
```

- **(c) is ook zonder fix groen, en dat klopt.** De wisvlag van de kwartiermeester (0x3A0) staat niet alleen op de kaart. q925 staat in `SpireMetaQuests`, dus newserv legt de wis van 0x3A0-0x3A2 zelf vast (`spire_bridge_record_flag`, `spire_meta_record_applies`). Bij elke start van q925 levert hij die vlag weer af, vóór de quest hem leest (`spire_meta_delivery_entries`). Na de val staat de vlag op de kaart nog open, maar de volgende basis krijgt hem dicht. Daarom meet de test de vlag zoals de volgende basis hem leest. B2 uit de critic ging ervan uit dat de vlag alleen op de kaart staat. Met de fix blijven de Drops staan: het is een stapel, en een stapel wordt nooit uit de duw gehaald.
- **(d) en (e) zijn bewakingstests:** ze tonen dat de fix niets breekt.
  - Om te tonen dat (e) iets meet, heb ik de fix gemuteerd: de regel voor een gedeelde basis uitgezet (`.lab/tests-mutation.log`). Daarmee worden beide ruilgevallen rood: `FAILED: the item traded to the partner came back to the first player`.
  - Het opraapgeval blijft dan groen. K7b haalt het item al uit de momentopname vóór de 98, en de test meet dat zelf ook.

Ook toegevoegd: een unittest naast de K7-tests.
- Het bestandsveld heen en terug. Een oud bestand zonder het veld leest als "geen", een kapot veld ook, en de rest laadt gewoon.
- Wanneer het record ontstaat en weer vervalt.
- Een stapel die deels terugkomt, een volle inventaris, en dat een tweede verzoening niets meer doet.

### De verzoeningsregel

De 98 van een basisgame die de speler alleen had, bewaart naast de momentopname de inventaris en meseta van vóór die basis (`BaseBefore` in het snapshotbestand). Alleen de duw bij de login gebruikt dat, één keer, tegen de bank van de kaart (de 30). Items worden vergeleken op hun data; het item-ID telt niet mee.
- Een gewoon item dat er vóór de basis was, nu niet meer, en ook niet in de bank van de kaart staat: komt terug.
- Een stapel die nu kleiner is: het verschil komt terug.
- Een gewoon item dat erbij kwam en in de bank van de kaart staat: gaat eruit. Een stapel gaat er nooit uit, zodat Photon Drops die naast een stapel op de bank staan blijven.
- Minder meseta dan vóór de basis: terug naar het bedrag van vóór de basis. Meer meseta blijft staan.

Het record vervalt:
- bij een login waarvan de 61 gelijk is aan de momentopname;
- bij de 98 van een gewone game;
- bij een runstart;
- bij elke overdracht via de vloer (K7b);
- bij een gedeelde basis;
- na gebruik.

### Wat de server wel en niet kan onderscheiden

- **Wel:** een overdracht via de vloer (K7b, precies) en of er een ander in de basisgame was. Dat laatste meet de vertrekhaak: elk vertrek uit een game met anderen erin markeert de vertrekker en iedereen die blijft.
- **Niet:** op V3 is een bankstorting een 6x29, net als een verkoop, de mag voeren, de tekker of een gebruikt item. Het ruilvenster volgt de guard niet; `on_D2_V3_BB` heeft geen guard-haak. Een geruild item verdwijnt dus net als een gestort item.
- **Gekozen (B-AF-53):** in een gedeelde basis wordt niets verzoend. De duw is daar dezelfde als nu.

### Wat er overblijft

1. **Gedeelde basis met een val vóór de kaartschrijf:** een storting is weg, een opname dubbel, net als nu. Sluiten kan met één haak in `on_D2_V3_BB` (ReceiveCommands.cc) die geruilde items uit het record haalt. Dat bestand valt buiten deze schrijfset; uitbreidingsverzoek: `pso-roguelike/new-features/roguelike/STATE/expansion-veilig-bank-1794650588187.json`.
2. **Aanvaard: dubbel bij één speler, geen verlies.** Een verkocht, gevoerd, getekt of gebruikt item komt bij een val terug. Hetzelfde geldt voor ingewisselde Drops naast de meseta ervoor, en voor een aankoop (de meseta komt terug).
3. **Meseta-opname bij een val:** blijft dubbel bij één speler. Een opname is niet te onderscheiden van een verkoop, meseta van de vloer of de inwissel bij de kwartiermeester. Ik heb er geen test voor; de storting is wel gesloten en getest.
4. **Een volle inventaris (30):** een item dat terug moet maar niet past, wordt niet toegevoegd. Dat blijft verlies, maar het komt zelden voor.
5. **Een tweede basisbezoek dat zelf een val heeft vóór zijn eigen 98:** het record is dan dat van het eerste bezoek. Wat in het eerste bezoek verkocht is, kan terugkomen; ook dat is dubbel bij één speler.
6. **Al eerder zo (pre-existing):** een ruil en daarna een val zonder 98 geeft de ruiler zijn item terug. De momentopname ziet geen ruil (K7-kop: "trade window ... changes no snapshot").

## 2. Herhaalplanning (B-REGIE-21-21)

`spire_replay_after_floor_load` plant nu ook als het spel nog geen herhaalstaat heeft. De vroege return is weg; `spire_replay_send` leest de staat pas wanneer hij verstuurt.
- **Test:** twee GC-clients in een brugquest.
  - De leider meldt zijn vloer vóór er staat is.
  - Daarna noteert de volger r229 en r212.
  - De `io_context` draait `SPIRE_REPLAY_DELAY` + 100 ms.
  - Verwacht: precies één 6x77 r212 = 1 en één r229 = 1 naar de leider, niets naar de volger.
- **Zonder fix:** `run-static-tests: failure at .../src/StaticTests.cc:10611: 1u != r212.size()` (`.lab/tests-replay-red.log`).
- **Met fix:** groen; log `Spire replay: 2 register(s) to client 0 after its load of floor 01 (send 1)` (`.lab/tests-final2.log`).

## Beslissingen

### B-VB-1 — Geen verzoening in een gedeelde basis
- **Datum:** 2026-11-14
- **Route:** zelf
- **Type:** technisch
- **Klasse:** 2
- **Dekking:** B-AF-53 en de opdracht B-REGIE-21-20: "Kan de server het niet onderscheiden, kies dan de kant zonder dubbel tussen spelers en meld dat."
- **Beslissing:** het record wordt alleen bewaard voor een basisgame die de speler alleen had.
- **Waarom:** de ruil is op V3 niet te onderscheiden van een storting.

### B-VB-2 — Stapels en meseta alleen teruggeven, nooit eruit halen
- **Datum:** 2026-11-14
- **Route:** zelf
- **Type:** technisch
- **Klasse:** 2
- **Dekking:** de veiligheidsregel uit de opdracht: "dubbel bij één speler, geen verlies".
- **Beslissing:** een stapel en meseta komen alleen terug en worden nooit uit de duw gehaald.
- **Waarom:** een opname is niet te onderscheiden van de beloning van de kwartiermeester of een verkoop, en eruit halen kan een beloning kosten.

### B-VB-3 — Beloning gemeten zoals de volgende basis de vlag leest
- **Datum:** 2026-11-14
- **Route:** zelf
- **Type:** technisch
- **Klasse:** 1
- **Dekking:** de bestaande levering van de betaalvlaggen (pso-regie-9, B-BEL-4).
- **Beslissing:** geval (c) meet de vlag zoals de volgende basis hem leest. De questvlaggen in de duw zijn niet aangeraakt.
- **Waarom:** de server is daar al de bron, en de vlaggen horen bij systeem 4/5.

## Gezien, niet gebouwd

- **K7b-logregel:** `spire_guard_note_floor_drop` schrijft "no credit on the floor item" terwijl het tegoed al is opgeslagen. Dat gebeurt als `describe_item` gooit, wat in de test voorkomt omdat daar geen itemnaamindex is. Het gedrag klopt; alleen de logtekst niet.
- **git-flow:** de hook meldt dat de newserv-boom geen `Integratiebranch:`/`Uitrol:` heeft vastgelegd.
