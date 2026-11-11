# Veiligheidsbundel 1, itemveiligheid: bewaker en bewaarpunten (newserv)

Tak `spike/veilig`, gestart op 9e40c01 (de versie op de pc). Worktree `/home/m4rck/code/worktrees/newserv/veilig-1`.

## Voortgang

| Punt | Nameting | Stand | Commit |
|---|---|---|---|
| 8-B2 herstel bij login sluit de marker te vroeg | klopt | gebouwd, test rood en groen | 6068558 |
| 8-B1 beloning uit de basis wordt teruggedraaid | klopt | gebouwd, test rood en groen | 89e0e17 |
| 7-M2 losse kamer overschrijft geparkeerde run | klopt | gebouwd, test rood en groen | 27dee5b |
| Tests van spoor intern meenemen (aanvulling regie) | n.v.t. | samengevoegd, drie verwachtingen aangepast aan 8-B2 | a8515fd, 5d0ac72 |
| Bank na een herstel (aanvulling regie) | klopt | eerlijke logregel en serverkant-controle gebouwd; teruglezen van de client niet gebouwd | 5d0ac72 |

Alle punten zijn af. De code staat op 5d0ac72; de laatste commit op de tak is die van dit rapport, en van die commit
is de binary gebouwd. Blockers: geen. Eén keuze ligt bij de regie: de bank teruglezen van de client (zie "Bank na een
herstel").

## Dekking

Gedekt: de drie bezwaren aan de serverkant, elk met een statische test die rood is zonder de fix. Niet gedekt: een
echte login met een echte geheugenkaart (dat tonen alleen de Wii en de pc), het vangnet "nooit geraakt" (andere
bouwer, veilig-1b) en de back-up (`InventoryBackup`). Er is niets gemeten op een client, in het lab of op de pc.

Basis op 9e40c01, vóór enige wijziging: `build/newserv run-static-tests` eindigt met
`-- All static tests passed`, exitcode 0, 10 W-regels, 0 E-regels. De 10 W-regels zijn de bestaande gevallen met
een bewust kapot bestand (3x Overlay, 3x RunCopyPrint, 3x run mag lean, 1x print niet opgeslagen).

Na het samenvoegen van de tests van spoor intern zijn het 12 W-regels: de twee nieuwe komen uit hun geval "the client
is gone before the restore" (`failed to restore character ...: Client has already disconnected; marker kept` en
`game exit restore ... not verified; dropping the client`), en horen bij wat dat geval speelt.

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

**Nameting: klopt.** Keten op 9e40c01:

- `SpireFlagBridge.cc:2163-2170` de kamerquest van kamer 3 naast een run die op kamer 9 geparkeerd staat: het
  bewaarpunt van de leider past niet, de game krijgt een nieuw run-id.
- `:2198-2217` de speler begint vers. De melding "je run staat geparkeerd" kijkt alleen naar andere moeilijkheden
  (`:2207` slaat de eigen moeilijkheid over).
- `:2984-2985` met `:865-870`: een kamerquest die niet op de eerste kamer begint en niet hervat is, is een oefenrun.
- `:1904-1914` en `:1941-1963`: de 'V' kijkt niet naar oefenrun of naar wat er geparkeerd staat en schrijft met
  `spire_savestate_store_rung` naar het bestand van de moeilijkheid (`:1957-1961` in de telling van de fixlijst).
- Niet nagemeten: dat de quest de 'V' al bij de eerste kamer van een kamerquest stuurt (questkant; de server schrijft
  bij elke 'V' die hij krijgt).

**Hoe de server een oefenrun herkent en hoe hervatten het bestand kiest.** De 'B' van de client draagt de startkamer
en de eerste kamer van de run; ongelijk en niet hervat is een oefenrun, en een hervatte run neemt de vlag over uit
zijn bewaarpunt (`spire_run_is_practice`). Er is één bestand per karakter per moeilijkheid
(`<serial>.<karakter>.savestate.<normal|hard|vhard|ultimate>.json`), en het oude ene bestand telt voor de
moeilijkheid die erin staat zolang die geen eigen bestand heeft. Hervatten laadt het bestand van de moeilijkheid van
de quest en hervat alleen als kamer, variant, episode en run-id passen.

**Gekozen vorm.** Een oefenrun schrijft niet als er op die moeilijkheid een echte run geparkeerd staat. Niet een eigen
bestand: de bestandsnaam wordt op zes plekken gelezen (hervatten, het einde van de run, de melding bij de game-join,
`$spire continue`, het mag-grootboek, het niveau bij hervatten); een tweede bestand per moeilijkheid raakt ze alle zes
en opent zelf nieuwe wegen naar een verkeerd gekozen bewaarpunt. Niet schrijven raakt er één. Geen bestaand bestand
wordt anders gelezen of geschreven: oude en nieuwe bestanden laden zoals voorheen.

De regel is strenger dan het voorstel van de critic: een oefenrun vervangt nooit het bewaarpunt van een echte run,
ook niet als het run-id gelijk is (de critic zonderde alleen een ander run-id uit). Gelijk run-id met een oefenrun
komt alleen voor bij de volger die vers begint in een hervatte game (7-M1) en bij een te laat hervat-antwoord (7-K1);
in beide is het echte bewaarpunt houden de veilige kant. Een geparkeerd bestand dat niet te lezen is laat een oefenrun
ook staan: het kan een echte run zijn. Een echte run schrijft zoals voorheen, ook over een onleesbaar bestand.

**Wat het hervatten van de losse kamer zelf nog kan.**

- Staat er op die moeilijkheid niets, of een eerdere oefenrun: de losse kamer wordt bewaard en is te hervatten, precies
  zoals voorheen.
- Staat er een echte run geparkeerd: de losse kamer wordt niet bewaard. Na een verbroken verbinding begint hij
  opnieuw vanaf zijn kamerquest. De speler krijgt één keer per oefenrun de regel
  "Practice is not saved: your run at room 9 stays parked." De server logt elke overgeslagen 'V'.
- Een andere moeilijkheid heeft zijn eigen bestand: daar verandert niets.

**Wat de speler meemaakt.** Vóór: je parkeert een echte run op kamer 9, oefent kamer 3 los, en bij de eerste stap in
die kamer is je run van kamer 9 weg, zonder melding. Na: je run van kamer 9 blijft staan en hervat ongeschonden; je
leest één regel dat de oefenkamer niet bewaard wordt.

**Test** (in "resume only into the same room, variant and run", blok "7-M2"), met echte bestanden in een eigen
tijdelijke map: echte run geparkeerd op kamer 9; losse kamer 3, drie kamerstarts, niet bewaard, het bestand blijft
byte voor byte gelijk; het einde van de oefenrun wist alleen bestanden van zijn eigen run-id; de echte run laadt en
hervat. Verder: de beslissing zelf (ook bij gelijk run-id), een andere moeilijkheid, een echte run die een echte of
een oefenrun vervangt zoals voorheen, het oude ene bestand, niets geparkeerd, en een onleesbaar bestand.

Rood (test erin, gedrag van 9e40c01):

```
E 828208 2026-11-11 14:59:06 - run-static-tests: failure at /home/m4rck/code/worktrees/newserv/veilig-1/src/StaticTests.cc:6084: std::string() != spire_savestate_store_v(stem, 0, practice, 42, &replaced, &kept)
exit=1
```

Groen (met de fix): `I 840011 2026-11-11 15:00:06 - -- All static tests passed`, exit 0, 10 W, 0 E.

## Tests van spoor intern (d97ca83, drie commits, alleen `src/StaticTests.cc`)

Samengevoegd met `git merge --no-edit d97ca83` op een schone werkboom, zonder conflict (a8515fd). De vier gevallen
spelen de bewaker van begin tot eind tegen een nagebootste GameCube-client die via de echte commandoafhandeling
antwoordt.

Tegen de fixes: geval 1 (run, game-exit, volgende login) bleef groen. Drie gevallen waren rood, alle drie op dezelfde
verwachting, die het oude gedrag van 8-B2 vastlegde: "na een herstel bij de login is er geen marker meer".

```
E    GUARD E2E (the connection drops mid-run: no 98, the next login restores the real character) FAILED: failure at .../src/StaticTests.cc:8409: !(no_marker(snap))
E    GUARD E2E (game exit, but the card kept the run copy: the next login pushes the real character again) FAILED: failure at .../src/StaticTests.cc:8433: !(no_marker(snap))
E    GUARD E2E (game exit, the client is gone before the restore: refused, the marker stays, the next login restores) FAILED: failure at .../src/StaticTests.cc:8459: !(no_marker(snapshot_on_disk(p2)))
```

Aangepast, met de reden in het commentaar erbij: na een herstel bij de login staat de marker op "wacht op save"
(`awaiting_only`), omdat de 97 pas daarna de deur uit gaat. Het geval "verbinding valt weg midden in een run" speelt
nu ook het vervolg van 8-B2 met de echte afhandeling: de client valt weg vóór de save, de volgende login (kaart draagt
de run-kopie) duwt opnieuw, en de login daarna (kaart draagt het echte karakter) sluit de marker zonder duw. Geen
verwachting over 8-B1 hoefde aangepast: geen van de vier gevallen speelt de basisgame. Daarna alle vier groen.

## Bank na een herstel (aanvulling regie)

**Nameting: klopt.** De bank wordt na een herstel door niets gecontroleerd.

- Wat de verificatie vergelijkt (`spire_guard_verify_restore`, `SpireFlagBridge.cc:5169-5215` op 9e40c01): uit de 61 van
  de client het level en de EXP, de items (data1 en data2, in elke volgorde, zonder item-ID) met de meseta, en de
  materiaaltellers. Niet: de bank, de technieken, de overige stats, de volgorde en de uitrustvlaggen van de items.
- Wat er in de momentopname zit (`SpireGuardSnapshot`, `SpireFlagBridge.hh:839-864`): de inventaris (items,
  uitrustvlaggen, materiaaltellers) en het disp-blok (stats met level, EXP en meseta; uiterlijk; config; technieken).
  Geen bank.
- Wat de duw doet (`:5339-5344`): hij haalt het volledige karakter op (de 30), vervangt daarin inventaris en disp door
  de momentopname en stuurt het geheel terug. De bank die teruggaat is dus de bank die de client zelf net stuurde.
- Hoe de server de bank van een GameCube-client ziet: alleen als hij er zelf om vraagt, met de functieaanroep
  GetExtendedPlayerInfo, waarop de client een 30 stuurt (`SendCommands.cc:2433-2487`, `ReceiveCommands.cc` `on_30`). De
  client stuurt hem nooit uit zichzelf: de 61 en de 98 dragen geen bank (`CommandFormats.hh:1093-1104`), en het
  bankverkeer aan de balie (6xBB, 6xBD) bestaat alleen voor BB (`ReceiveSubcommands.cc:2643-2650`, `:2714`). Op de
  GameCube leeft de bank in de client en op de kaart.

**Kan de server de bank na een herstel betrouwbaar zien?** Hij kan er een tweede keer om vragen. Ik heb dat niet
gebouwd, om drie redenen die ik niet weg kan meten vanaf hier:

1. Het is een nieuwe functieaanroep in de handdruk van het herstel, direct na de duw. Op die plek hebben
   functieaanroepen eerder clients laten vastlopen (v33, v46, v47), en geen enkel bestaand pad vraagt een 30 ná een
   duw. Of een echte client dan antwoordt en doorspeelt, is clientgedrag en dus pas waar na een meting.
2. Er is niets om tegen te vergelijken behalve de 30 van vlak vóór de duw. De vergelijking zegt dan alleen "de duw
   heeft de bank niet veranderd", niet "dit is de juiste bank".
3. De server zet de 30 om naar zijn eigen vorm en terug. Een teruggelezen bank in die vorm kan een verlies in de
   omzetting niet tonen.

Wil de regie het teruglezen toch: het is een kleine wijziging (na de 61 nog een 30, alleen bij login en game-exit;
wijkt de bank af dan geen 97 en de marker blijft; komt er geen antwoord dan doorgaan met "niet gecontroleerd"), maar
hij hoort eerst op een client gemeten.

**Wel gebouwd, zonder nieuw verkeer naar de client.**

- De logregel zegt wat er wel en niet geverifieerd is. Nieuw, uit de testrun:
  `restore verified for serial 0000004321: 61 shows Lv52 EXP 1234567; inventory 6 items, snapshot 6 (equal count);
  marker kept until a login 61 shows the same; verified by the 61: level, EXP, items, meseta, material counters; bank
  NOT checked (a 61 carries no bank): sent back as the client held it, 3 items, meseta 777000`. Het begin van de regel
  is gelijk gebleven, zodat bestaande zoekopdrachten in het journal blijven werken.
- Vóór de duw controleert de server dat het karakter dat hij gaat sturen byte voor byte de bank draagt die de client
  net stuurde (`spire_guard_push_keeps_bank`). Zo niet: niets geduwd, geen 97, marker blijft. Dat is precies de mutatie
  van intern (bank gewist aan de serverkant): die geeft nu geen "verified" meer maar een geweigerd herstel.
- De marker: er is niets veranderd om een later zichtbare bankafwijking te herstellen, omdat dat niet veilig kan. De
  server heeft geen kopie van de bank, ziet hem alleen bij een volgend herstel, en kan dan een gewiste bank niet
  onderscheiden van een bank die de speler zelf leeghaalde. Bij de geweigerde duw hierboven blijft de marker wel open
  en wordt de kaart niet geschreven, dus de bank op de kaart blijft heel en de volgende login probeert opnieuw.

**Wat de speler met zijn bank riskeert na een herstel.** Vóór: als het herstel de bank zou beschadigen, zei de server
"verified", schreef de Wii de beschadigde bank naar de kaart, en niemand merkte het. Na: een beschadiging die aan de
serverkant ontstaat houdt het herstel tegen voordat er iets naar de client gaat. Een beschadiging die in de client
zelf ontstaat ziet de server nog steeds niet; het logboek zegt dat nu ook, met het aantal items en de meseta van de
bank die is teruggestuurd, zodat achteraf te zien is wat de bank op dat moment was. Het risico uit 8-B1 (bankbezoek in
de basis gevolgd door een Wii die niet opslaat) staat onder "Gezien, niet gebouwd".

**Test** (in het blok van de eind-tot-eind-gevallen, "Bank (regie)"): een gewiste bank, één afwijkend item en afwijkende
meseta worden geweigerd; de weg die het herstel loopt (30 naar de servervorm, inventaris en disp vervangen, terug)
levert de bank van de client byte voor byte; dezelfde weg met een gewiste bank wordt geweigerd; en de logtekst noemt
de bank "NOT checked" en nooit volledig geverifieerd.

Rood (test erin, gedrag van 9e40c01: geen controle, logregel zwijgt over de bank):

```
E 933937 2026-11-11 15:10:27 - run-static-tests: failure at /home/m4rck/code/worktrees/newserv/veilig-1/src/StaticTests.cc:8498: !(!spire_guard_push_keeps_bank(real.bank, wiped.bank))
exit=1
```

Groen (met de fix): `I 941687 2026-11-11 15:11:25 - -- All static tests passed`, exit 0, 12 W, 0 E.

## Beslissingen (voor de orchestrator, die deze ronde decisions.md schrijft)

1. 8-B1 volgt bij "level en EXP niet lager", niet bij "exact gelijk". Dekking: het voorstel van de critic in de
   fixlijst, en `quests/payout/build.sh:4`/`:14` (q925 is Sega's q101 met ongewijzigde kaart, dus EXP in de basis is
   echte voortgang). Klasse 2, eigen writeSet.
2. 8-B1: het twijfelgeval-ontwerp is niet gebouwd; de reden staat in de sectie 8-B1. Wil de regie het toch (een stand
   "onbeslist" waarin de login niet duwt), dan is dat nieuw ontwerp: een nieuwe markerstand, een weigering voor runs
   in die stand en een beheerderscommando om hem te sluiten.
3. 7-M2: "niet schrijven" in plaats van "eigen bestand", en strenger dan de critic (ook bij gelijk run-id en bij een
   onleesbaar bestand). Dekking: de opdracht ("schrijft nooit over een bewaarpunt van een echte run heen").
4. 7-M2 stuurt één nieuwe regel naar de client, één keer per oefenrun. Dat is nieuw zichtbaar gedrag; de tekst staat
   in `spire_savestate_practice_kept_text`.
5. De nieuwe stukken dragen de naam van het bezwaar (8-B1, 8-B2, 7-M2) en geen volgend versienummer, zodat de tak van
   veilig-1b een eigen nummer kan kiezen zonder botsing.
6. Bank: de bank wordt niet van de client teruggelezen (geen nieuwe functieaanroep in het herstel zonder meting op een
   client). Dekking: de opdracht ("kan de server de bank op dat moment niet zien: bouw geen schijnzekerheid") en de
   projectregel dat clientgedrag pas waar is na een meting. Dit is een keuze voor de regie om om te draaien.
7. De aanpassing van de drie verwachtingen van spoor intern en het bankpunt staan samen in één commit (5d0ac72): ze
   raken dezelfde regels in `StaticTests.cc` en lieten zich niet schoon splitsen.

## Gezien, niet gebouwd

- security/datakwaliteit · de bank zit niet in de momentopname: een bankstorting of -opname in de basis gevolgd door een
  Wii die zijn kaart niet schrijft, geeft bij de volgende login inventaris uit de momentopname en bank van de kaart, dus
  één item dubbel of weg; vóór 8-B1 gebeurde dat in spiegelbeeld op de gewone route · `SpireFlagBridge.cc`
  `spire_guard_run_restore` (bank uit de verse 30) · vraagt een bank in het bestand, of meten of de bankbalie in q925 werkt.
- datakwaliteit · vóór 8-B1 kwamen items die je in de basis aan je partner gaf bij de volgende login terug (dubbel);
  8-B1 sluit dat mee, het is niet apart getest op de client · `spire_guard_base_game_follow` · niets.
- datakwaliteit · 8-M1 (spelen buiten deze server wordt teruggedraaid) geldt door 8-B2 nu ook na een herstel bij de
  login · `spire_guard_restore_awaits_save` · de fix van 8-M1 (marker sluiten zodra de kaart geschreven is, eerst meten op de Wii).
- datakwaliteit · 7-K1: een te laat hervat-antwoord maakt een oefenrun met het run-id van de echte run; 7-M2 houdt het
  bestand bij elke 'V', maar het einde van die oefenrun wist het nog steeds op run-id · `spire_savestate_on_run_end` · klein.
- datakwaliteit · een nieuwe echte run op dezelfde moeilijkheid vervangt de geparkeerde echte run nog steeds zonder
  waarschuwing (zo ontworpen, B-REGIE-18-6, en vastgezet in de test); dat is de andere helft van het bezwaar van systeem 1 ·
  `spire_savestate_may_replace` · een keuze over wat de speler dan te zien krijgt.
- datakwaliteit · 7-M3 (episode 1 en 2 delen één bewaarpunt per moeilijkheid) en 7-M1 (volger één kamer achter) zijn
  niet geraakt; bij 7-M1 houdt de volger door 7-M2 nu wel zijn echte bewaarpunt in plaats van een oefenpunt · `:695-698`, `:2175` · eigen punten.
- datakwaliteit · de verificatie na een herstel vergelijkt de technieken, de overige stats en de uitrustvlaggen niet
  (wel level, EXP, items, meseta, materialen); de eind-tot-eind-tests van intern vergelijken ze wel, aan de testkant ·
  `spire_guard_verify_restore` · klein, alles staat in de 61.
- datakwaliteit · een kopie van de bank bij het herstel in het bestand van de momentopname zou een verloren bank met de
  hand terug te halen maken; nu staat alleen het aantal items en de meseta in het logboek · `spire_guard_save` · klein,
  maar vraagt ook een importcommando om nut te hebben.
- werkwijze · de commit-hook meldt dat voor deze newserv-repo geen integratietak en uitrol zijn vastgelegd (waarschuwing,
  niets geweigerd); er is niets gepusht of samengevoegd · `.claude/omgeving.json` of `CLAUDE.md § Git flow` · één regel van de regie.

## Afwijkingen van de opdracht

- "Na elke commit die je meet: `unlink src/Revision.cc` en `make`": bij de drie codecommits is de boom gemeten vlak
  vóór de commit (gebouwd en getest, daarna ongewijzigd gecommit), niet opnieuw erna. Alleen de laatste commit is
  volgens het voorschrift gebouwd en gemeten.
- Het eind-sha en de meting op de eindbinary kunnen niet in dit bestand staan (het bestand is zelf de laatste commit);
  ze staan in de overdracht aan de orchestrator. De laatste commit wijzigt alleen dit rapport; de code is die van 5d0ac72.
- De aanvullingen van de regie (tests van intern, bank) kwamen binnen nadat de eerste eindstand al gebouwd was
  (0e5b44d, groen, 10 W, 0 E); daarna is doorgebouwd en is de eindprocedure opnieuw gedaan.

## Wat er gewijzigd is

- `src/SpireFlagBridge.cc`, `src/SpireFlagBridge.hh`: de drie fixes. Geen regel in de functies van het vangnet
  (regels 6591-6801 op 9e40c01); de laatste gewijzigde plek ligt rond regel 6325 van 9e40c01.
- `src/StaticTests.cc`: drie testblokken, bij de bestaande bewaker- en bewaarpunttests; het commentaar boven de
  assertie van regel 240 (de assertie zelf is gelijk); de 530 regels van spoor intern, met drie aangepaste
  verwachtingen, één uitgebreid geval en het bankblok.
- Niet geraakt: `src/InventoryBackup.*`, `src/SpireDrops.*`, `tests/`.
