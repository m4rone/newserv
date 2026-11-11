# Veiligheidsbundel 1, itemveiligheid: bewaker en bewaarpunten (newserv)

Tak `spike/veilig`, gestart op 9e40c01 (de versie op de pc). Worktree `/home/m4rck/code/worktrees/newserv/veilig-1`.

## Voortgang

| Punt | Nameting | Stand | Commit |
|---|---|---|---|
| 8-B2 herstel bij login sluit de marker te vroeg | klopt | gebouwd, test rood en groen | 6068558 |
| 8-B1 beloning uit de basis wordt teruggedraaid | klopt | gebouwd, test rood en groen | 89e0e17 |
| 7-M2 losse kamer overschrijft geparkeerde run | klopt | in bouw | nog niet |

Eindstand (sha, eerste regel van `newserv help`, slotregel van de statische tests) staat onderaan zodra 7-M2 af is.

## Dekking

Gedekt: de drie bezwaren aan de serverkant, elk met een statische test die rood is zonder de fix. Niet gedekt: een
echte login met een echte geheugenkaart (dat tonen alleen de Wii en de pc), het vangnet "nooit geraakt" (andere
bouwer, veilig-1b) en de back-up (`InventoryBackup`). Er is niets gemeten op een client, in het lab of op de pc.

Basis op 9e40c01, vóór enige wijziging: `build/newserv run-static-tests` eindigt met
`-- All static tests passed`, exitcode 0, 10 W-regels, 0 E-regels. De 10 W-regels zijn de bestaande gevallen met
een bewust kapot bestand (3x Overlay, 3x RunCopyPrint, 3x run mag lean, 1x print niet opgeslagen).

## 8-B2: herstel bij de login sluit de marker voordat de kaart geschreven is

**Nameting: klopt.** Keten op 9e40c01:

- `SpireFlagBridge.cc:6320` de login roept `spire_guard_run_restore(..., "login")`.
- `:5354` `spire_guard_verify_restore(..., !strcmp(where, "game exit"), in_game)`: voor "login" is dat `false`.
- `:5207-5210` `restore_pending = false; awaiting_save = after_game;` en het bestand wordt geschreven: beide markers uit.
- `ReceiveCommands.cc:436-443` pas na terugkeer gaat `97` naar de client en wacht de server op het antwoord.
- `:6252-6258` de volgende login vindt geen open marker en keert terug zonder herstel.

**Wat de speler meemaakt.** Vóór: een run eindigt hard, je logt in, de server zet je echte karakter terug, en in
de seconden daarna valt de Wii of de verbinding weg. Op de kaart staat nog de run-kopie; bij de volgende login zet
niemand je karakter terug en je krijgt geen melding. Na: de volgende login ziet dat de kaart niet gelijk is aan de
momentopname en zet het echte karakter opnieuw terug. Staat het echte karakter al op de kaart, dan sluit die login
de marker zonder iets te sturen.

**Gebouwd.** De kleinste fix van de critic: ook "login" laat de marker op "wacht op save" staan
(`spire_guard_restore_awaits_save`), de markerstap is een losse functie (`spire_guard_restore_verified_apply`).

**Gevolg dat erbij hoort.** Na een herstel bij de login staat de marker nu open tot de volgende login of tot de
speler een gewone game verlaat. In die tijd weigert `$kitlobby` ("Wacht: herstel loopt"), zoals nu al na een herstel
bij het verlaten van een game. En het bezwaar "spelen buiten deze server wordt teruggedraaid" (8-M1) geldt nu ook
na een herstel bij de login: dat is dezelfde marker en zit niet in deze bundel.

**Test** (`StaticTests.cc`, "a restore at login leaves the marker open until the card is written (8-B2)"): herstel
bij login via een echt bestand in een eigen tijdelijke map; client weg vóór de save; volgende login met de
run-kopie duwt opnieuw; volgende login met het gelijke karakter sluit zonder duw.

Rood (test erin, gedrag van 9e40c01):

```
E 751129 2026-11-11 14:46:10 - run-static-tests: failure at /home/m4rck/code/worktrees/newserv/veilig-1/src/StaticTests.cc:2056: !(!snap.restore_pending && snap.awaiting_save && !snap.run_in_progress)
exit=1
```

Groen (met de fix): `I 766563 2026-11-11 14:49:07 - -- All static tests passed`, exit 0, 10 W, 0 E.

## 8-B1: wat de speler in de basis ophaalt, wordt bij de volgende login teruggedraaid

**Nameting: klopt.** Keten op 9e40c01:

- `SpireFlagBridge.cc:5031-5040` bij de 98 van de basisgame onder "wacht op save" gebeurt alleen
  `spire_guard_base_game_left.emplace` en een logregel; de momentopname blijft die van vóór de run.
- `:4481-4483` `spire_guard_98_follows` geeft `false` voor de basis (`&& !base_game`), vastgezet in `StaticTests.cc:240`.
- `:6259-6299` met `:4470-4479`: de login-61 met de beloning is ongelijk aan de momentopname, `restore_pending` gaat
  aan, en `:6320` duwt de oude momentopname terug.
- `:4459-4462` "Start a new run" vanuit de basis: de data verschilt, de oude momentopname blijft (KEPT_UNCONFIRMED), en
  het herstel na die tweede run duwt de stand van vóór run 1 terug.
- Questkant, gelezen in `quests/payout/make_payout.py:8-16`: de kwartiermeester maakt Photon Drops met `item_create2`,
  betaalt meseta en smeedt wapens. `quests/payout/build.sh:4` en `:14`: de kaart van q925 is Sega's q101 ongewijzigd.
  Niet gemeten: of de speler in q925 echt het bos in kan en daar EXP haalt (de code van de fix gaat ervan uit dat het kan).
- Niet na te meten vanaf hier: dat het pad op de pc nog niet gelopen is (geen toegang tot het journal).

**Waarom v59 de basis uitsloot** (commit 06cbc9c, `SpireFlagBridge.hh:1144-1147`, en het labverslag
`E01-run-engine/tests/labrun-2026-10-26-basis-terugkeer.md:544-563` en `:586-613`). Tot v59 deed de 98 van de basisgame
twee dingen in één stap: de momentopname volgde het karakter, en de marker ging dicht ("de kaart draagt het echte
karakter"). Dat tweede was fout: de client schrijft zijn kaart bij elke game-join uit een kaartkopie met alleen de
uitgeruste items (8 van 23, 7 van 20) en schreef daarna niets meer. Met de marker dicht ving niets dat op: bij de
volgende login had Kamako 8 items en BidX 7. v59 haalde daarom de hele stap weg voor de basis. De 98 zelf was in die
meting wel het echte karakter: `snapshot follows the real character: Kamako Lv104 ... (23 items)` uit het levende
object. De uitsluiting ging dus om de kaart, niet om de inhoud van de 98; dat de momentopname niet meer meeliep was
bijvangst van één functie die beide deed.

**Wat de server weet en niet weet.**

- Weet: de marker staat alleen op "wacht op save". Dat kan alleen na een herstel dat door een 61 bevestigd is, en er is
  sindsdien geen bewaakte run gestart. De client heeft sinds dat herstel dus het echte karakter in het geheugen.
- Weet: de 98 is van dezelfde naam, klasse en section ID, en level en EXP liggen niet onder de momentopname. Een echt
  karakter verliest nooit level of EXP (regel v56); data daaronder is een run-kopie.
- Weet niet: wat er op de kaart staat. Daarom blijft de marker open.
- Weet niet: de bank. Die zit niet in de 98, niet in de 61 en niet in de momentopname (zie "Gezien, niet gebouwd").

**Gekozen vorm.** Het voorstel van de critic: de twee helften uit elkaar. De momentopname volgt de 98 van de basisgame
onder dezelfde voorwaarden als bij een gewone game (zelfde karakter, level en EXP niet lager), en de marker blijft
open. Daarmee zijn beide gevallen zonder raden goed:

- De kaart draagt het echte karakter met de beloning: de login-61 is gelijk aan de momentopname, de marker sluit,
  er wordt niets geduwd.
- De kaart draagt nog de run-kopie (of alleen de uitgeruste items): de login-61 is ongelijk, de momentopname wordt
  geduwd, en die bevat nu de beloning. De momentopname wordt nooit de run-kopie: een 98 met lager level of lagere EXP
  volgt niet.

Ik heb "niet lager" gekozen en niet "exact gelijk": q925 draait op Sega's q101 met de kaart ongewijzigd, dus EXP uit
het bos van de basisquest is echte voortgang. "Exact gelijk" zou die voortgang, plus de beloning, alsnog terugdraaien.

Het twijfelgeval-ontwerp uit de opdracht (niets terugdraaien, regel aan de speler, momentopname bewaren) is niet
gebouwd, omdat de twee genoemde gevallen wel te onderscheiden zijn: de keuze valt niet meer bij de login maar bij de
98, en daar beslist kennis van de server. Het enige geval waarin de 98 niet volgt (level of EXP lager, of een ander
karakter) is geen twijfel maar een run-kopie; dan blijft de momentopname staan en zet de volgende login het echte
karakter terug, zoals nu. Er gaat geen nieuwe tekst naar de client.

**Wat de speler meemaakt.** Vóór: je wint een run, haalt in de basis je Photon Drops op (of ruilt ze voor meseta of
een S-rank-wapen), logt uit, en bij de volgende login is alles weer zoals vóór de run. De beloning is weg en ophalen
kan niet meer, want de server onthoudt dat hij uitbetaald is. Hetzelfde na "Start a new run" en een tweede run. Na:
wat je in de basis ophaalt, verkoopt, afgeeft of verdient blijft, ook als de Wii zijn kaart niet schreef.

**De bestaande assertie op `StaticTests.cc:240`** (`spire_guard_98_follows(true, true, true) == false`) is niet
gewijzigd: die functie betekent nog steeds "volgen én sluiten", en dat mag in de basis nog steeds niet. Alleen het
commentaar erboven is aangepast, omdat "never follows" niet meer waar is. Het volgen zonder sluiten is een eigen
beslissing (`spire_guard_98_base_follows`) met een eigen stap (`spire_guard_base_game_follow`).

**Test** ("the base game's 98 moves the snapshot along and leaves the marker open (8-B1)"). Geval 1, echt karakter
met beloning: de momentopname krijgt 21 items, de marker blijft open; login met het gelijke karakter sluit zonder duw;
login met de run-kopie of met alleen de uitgeruste items duwt en de duw bevat de beloning; een nieuwe run vanuit de
basis neemt de beloning mee. Geval 2, de 98 is een run-kopie: de momentopname blijft het echte karakter en de volgende
login duwt het terug. Verder: EXP uit het bos volgt, minder items volgt, ander karakter volgt niet, elke andere
markerstand verandert niets, en de twee beslissingen gaan nooit samen.

Rood (test erin, gedrag van 9e40c01):

```
E 782699 2026-11-11 14:52:10 - run-static-tests: failure at /home/m4rck/code/worktrees/newserv/veilig-1/src/StaticTests.cc:2138: !(spire_guard_base_game_follow(followed, paid.first, paid.second))
exit=1
```

Groen (met de fix): `I 809503 2026-11-11 14:56:09 - -- All static tests passed`, exit 0, 10 W, 0 E.

## 7-M2: een losse kamer overschrijft de geparkeerde run

In bouw; deze sectie wordt aangevuld.
