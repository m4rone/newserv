# Kwaliteit 3, serverkant: alleen op plek 2, en "Start a new run" zonder trede

Tak `spike/kwaliteit-3`, start `054db56`. Twee commits met code: `ea902ec` (punt A) en `219fc01` (punt B).
Questbron gelezen op commit `83d3f9c` van de questrepo (`quests/spire/make_spire.py`, `spire.json`, `score.py`,
`build.sh`).

## Dekking

Dit dekt: wat de server doet bij de hervatvraag en het startniveau voor één speler die niet op de leiderplek zit, en
de ontgrendeltoets op "Start a new run", elk met een statische test (rood op een mutatie die het gedrag van `054db56`
terugzet, groen met de fix).

Niet gedekt: de questkant (alleen gelezen, niets gebouwd), wat de client toont (dat beslist alleen de Wii: of de
tickerregel verschijnt en leesbaar is, is niet gemeten), en een echte run. De tweede toets van punt B (vlak voor de
queststart in de nieuwe game) heeft geen eigen statische test.

## Punt A: alleen op plek 2

### Meting server

- `spire_resume_answer` (`src/SpireFlagBridge.cc:2131`): de eerste 'J' van een game kiest de leider als
  `follow ? follow->first : 0` (regel 2202) en leest `l->clients[leader_id]`. Is die plek leeg, dan is `leader_ss`
  leeg, wordt de run "new" (regel 2209) en krijgt de vrager "fresh". Bevestigd.
- `spire_resume_level_for_client` (regel 4712): zelfde keuze (regel 4733). Lege leiderplek geeft geen bewaard niveau:
  de quest houdt zijn eigen niveau. Bevestigd.

### Meting quest (83d3f9c): een speler alleen kan geen run starten of hervatten, op geen enkele plek

- De leider is een vaste plek: `leader = engine.get('leader_client', 0)` (`make_spire.py:857`), `spire.json:32`
  `"leader_client": 0`. Elke rolkeuze vergelijkt `get_client_id` met die constante.
- Aankomst op de startvloer (`make_spire.py:2525-2552`): de leider vraagt `UI['start']`, elke andere client
  `UI['ready']`. `ui_ready` (2236-2272) geeft de kit of de hervatte spullen, stuurt zijn plek en Ready, toont
  "Your run has started. Waiting for player 1." en wacht dan op de klok van de leider. De run zelf (`run_started`,
  de kamerdraad, de klok, 'B' via de `run_start`-haak in `score.py:598-610`) start alleen in `ui_start` (2276-2373),
  dus alleen op de leiderclient. `t_mirror` draait alleen op de niet-leider (1157-1158) en volgt die klok.
- Een speler alleen op plek 2 vraagt dus wel 'J' (`rs_begin`, 1827-1852, ook vanuit het volgerpad 2246-2247), maar
  start nooit een run. Zijn `t_timer` (4764-4827) telt vóór de start de spelers en toont bij minder dan
  `run.min_players` "The second player left. The run ends here." (4824-4827).
- `spire.json:56` `"min_players": 2`, en geen bouwstap wijzigt dat (`build.sh` raakt `s['run']` alleen voor de
  tijdslimiet van q930; `spire-ep2.json` wordt per sleutel over `spire.json` gelegd). Daardoor start ook de leider
  alleen geen run: `ui_start` springt bij minder dan twee spelers naar `start_alone_ui` (2304-2306, 2374-2377) met
  dezelfde tekst. Een run alleen bestaat in deze quest niet, op plek 1 niet en op plek 2 niet.

### Gekozen vorm: de regel, geen halve hervatting

De quest kan op plek 2 niet verder. Volgens de opdracht is dan niets gebouwd dat een halve hervatting oplevert. De
andere vorm (de laagste bezette plek telt als leider) is wel gemeten, als mutatie: de speler krijgt dan "resume" en
zijn spullen voor een run die niet begint, en een echt run-einde in die game wist zijn bewaarpunt (uitvoer hieronder).

Gebouwd (`ea902ec`):

- `spire_alone_off_leader_slot` (`SpireFlagBridge.cc:725`, puur): precies één client in de game en die staat niet op
  de leiderplek. Met twee clients, of met de ene client op de leiderplek, is hij onwaar en verandert er niets.
- In het "fresh"-pad van 'J' (regel 2247-2261): heeft zo'n client een bewaarpunt dat bij deze quest past, dan blijft
  het antwoord "fresh" (zoals het was), leest hij één keer
  `Your saved run stays saved. Leave this game and make a new one to go on.` (`SPIRE_SAVESTATE_ALONE_TEXT`,
  `SpireFlagBridge.hh:511`, één tickerregel van 72 tekens), en onthoudt de server dat (`savestate_kept`).
- In 'V' (regel 1969-1974): voor die client in die game wordt geen bewaarpunt meer weggeschreven. Zo kan een verse run
  het bewaarpunt niet overschrijven, wat de quest daarna ook stuurt. Een run-einde wist het niet, want de run van de
  game heeft een eigen nummer.
- Het startniveau is niet aangeraakt: zonder hervatting blijft het niveau van de quest, wat bij "fresh" hoort.

### Test (`src/StaticTests.cc:8085`, geval "plek 2")

Rood op de mutatie die het gedrag van `054db56` terugzet (de voorwaarde op regel 2254 uitgezet), exit 1:

```
K3 measure: alone on slot 1 with a savestate of k20 rest: 'J' answer 4A000000 (fresh), told "Your saved run stays saved. Leave this game and make a new one to go on." 0 time(s); savestate file as it was after the 'J': yes, after a fresh 'B' and two 'V': NO, after the run end: NO
E ... K3 (plek 2: a client alone off the leader's slot gets no half resume, reads what to do and keeps its savestate) FAILED: failure at .../src/StaticTests.cc:8155: 1u != told
E ... run-static-tests: failure at .../src/StaticTests.cc:8302: 0u != k3_failed
```

De andere vorm als mutatie (alleen in de game telt de eigen plek als leider), exit 1:

```
[Game:7B02] Spire savestate: run over (records: run finished) with 1 of 1 client(s); deleted system/spire-bridge/0000005200.0BEA824D.savestate.normal.json (run 7B4E7599 room 20 variant 2 (1 items, 0 lives, 0 s, start room 16, Lv1))
K3 measure: alone on slot 1 with a savestate of k20 rest: 'J' answer 4A100101 (resume), told "Your saved run stays saved. Leave this game and make a new one to go on." 0 time(s); savestate file as it was after the 'J': yes, after a fresh 'B' and two 'V': yes, after the run end: NO
E ... K3 (plek 2: ...) FAILED: failure at .../src/StaticTests.cc:8154: SPIRE_RESUME_ANSWER_TAG != answer
```

Groen met de fix (bronnen van `219fc01`), exit 0:

```
K3 measure: alone on slot 1 with a savestate of k20 rest: 'J' answer 4A000000 (fresh), told "Your saved run stays saved. Leave this game and make a new one to go on." 1 time(s); savestate file as it was after the 'J': yes, after a fresh 'B' and two 'V': yes, after the run end: yes
K3 measure: two clients, the leader on slot 0: 'J' answers 4A100101 and 4A100101, told 0 and 0 time(s)
K3 measure: two clients on slots 1 and 2: 'J' answer 4A000000, told 0 time(s), savestate replaced by the fresh run: yes
K3 measure: alone on slot 1 in the room quest of k19: 'J' answer 4A000000, told 0 time(s), savestate replaced by the fresh run: yes
```

De laatste drie regels zijn het ongewijzigde gedrag: twee spelers met de leider op plek 1 hervatten allebei, twee
spelers zonder leider en een speler wiens bewaarpunt niet bij de quest past starten vers en overschrijven zoals voorheen.

Wat de test niet toont: dat de quest zonder 'B' blijft (dat is de lezing hierboven), en wat de client met de regel doet.

## Voor de quest

De serverregel is een noodoplossing. Wil je dat een speler alleen op plek 2 echt hervat, dan moet de quest dit
veranderen, en pas daarna de server:

1. **De leiderrol op looptijd kiezen.** Nu is `leader` de constante `engine.leader_client` (0). Een client die alleen
   in de game zit op een andere plek moet de leiderkant draaien: `UI['start']` en `ui_start` in plaats van
   `UI['ready']` en `ui_ready` (2536-2544), geen `t_mirror` (1157-1158), het leiderdeel van het bord (`hub_open`,
   1456), en elke andere `jmpi_eq/jmpi_ne(R.client_id, leader, ...)`. Komt er later een tweede speler bij, dan moeten
   beide clients dezelfde leider aanwijzen.
2. **Alleen mogen starten.** Met `run.min_players` 2 eindigt elke start alleen in `start_alone_ui` (2304-2306) en
   in de telling van `t_timer` (4768-4773). Zolang dat zo blijft, hervat niemand alleen, ook niet op plek 1, en helpt
   het advies van de serverregel alleen als er daarna een partner bij komt.
3. **De server laten weten dat de quest het kan**, bijvoorbeeld met een bit in de 'J'-waarde of een sleutel die
   `build.sh` naast `SpireFollowLeader` in de config zet. De server kan het niet zelf zien.

Daarna aan de serverkant: in `spire_resume_answer` (regel 2202) en `spire_resume_level_for_client` (regel 4733) de
leiderplek vervangen door de plek van de client zodra `spire_alone_off_leader_slot` waar is, en `savestate_kept` met
zijn regel weghalen. De volgwarp (`SpireFollowLeader`, regel 7464-7478) gebruikt dezelfde leiderplek en moet mee.

## Punt B: "Start a new run" zonder trede

### Meting

- Welke quest: `spire_base_new_run` (`SpireFlagBridge.cc:5924`) neemt de runquest waar de basisgame uit kwam
  (`spire_base_game_run_quests`, gezet op regel 5902 uit `mark.run_quest_number`, dat op regel 5726 het questnummer
  van de run-game is), anders q921 (regel 5932).
- Kan een partner zonder de trede in de game zitten? Ja, langs twee vensters:
  - De basisgame bestaat even zonder quest. `Lobby::join_error_for_client` (`src/Lobby.cc:613`) toetst een quest alleen
    als er een loopt (regel 653) en weigert tijdens laden (regel 664). `spire_base_game_start` kijkt elke seconde
    (regel 5766): met de hele groep binnen is het venster korter dan een seconde, met een groepslid dat niet komt
    tot `SPIRE_BASE_GAME_WAIT` (40 s). Wie dan via de gamelijst binnenkomt, zit in de basisgame als "Start a new run"
    iedereen meeneemt.
  - De nieuwe run-game ("Spire run") heeft hetzelfde venster vóór zijn queststart.
  - Na de start van de basisquest kan niemand meer binnen: q925 heeft geen `Joinable` (geen json in
    `quests/payout/build.sh`, standaard onwaar in `src/QuestMetadata.hh:45`), dus `QUEST_IN_PROGRESS` (regel 625).
- Het questbord toetst vóór het laden `quest_include_condition` over alle clients; `set_lobby_quest` op het
  nieuwe-run-pad deed dat niet. Bevestigd.

### Gebouwd (`219fc01`)

- `quest_unlocked_for_all_players` (`src/ReceiveCommands.cc:2880`) is de toets van het questbord als één functie; het
  bord roept hem zelf aan (regel 2920). De tekst staat één keer in `QUEST_NOT_UNLOCKED_TEXT`
  (`src/ReceiveCommands.hh:26`); het bord zet er zijn kleurcode voor.
- `spire_base_new_run` (regel 5942-5950): dezelfde toets in de basisgame, vóór iemand weggestuurd wordt. Bij weigering
  blijft iedereen in de basisgame en leest de leider
  `This quest has not been unlocked for all players in this game.` als tickerregel (geen venster, zoals elke Spire-tekst).
- `spire_base_game_start` (regel 5828-5846, vlak voor `set_lobby_quest` op regel 5847): dezelfde toets nog eens in de nieuwe run-game vlak voor de queststart,
  voor wie daar in het venster binnenkwam. Bij weigering start er geen quest en leest de leider van die game de regel.
  Alleen voor de Battle-game van de nieuwe run; de basisquest zelf start zoals voorheen.

### Test (`src/StaticTests.cc:8223`, geval "new run")

Rood op de mutatie die het gedrag van `054db56` terugzet (de toets op regel 5945 uitgezet), exit 1:

```
K3 measure: base game on Hard from q981, leader with Hard open, partner without: the board's test says NOT unlocked for all; after "Start a new run" sent out of the game: leader 1, partner 1; "This quest has not been unlocked for all players in this game." to the leader 0 time(s), to the partner 0 time(s)
E ... K3 (new run: "Start a new run" asks what the quest board asks and tells the leader the board's line) FAILED: failure at .../src/StaticTests.cc:8299: 0u != sent_out(leader) + sent_out(partner)
E ... run-static-tests: failure at .../src/StaticTests.cc:8409: 0u != k3_failed
```

Groen met de fix (bronnen van `219fc01`), exit 0:

```
K3 measure: base game on Hard from q981, leader with Hard open, partner without: the board's test says NOT unlocked for all; after "Start a new run" sent out of the game: leader 0, partner 0; "This quest has not been unlocked for all players in this game." to the leader 1 time(s), to the partner 0 time(s)
K3 measure: the same with Hard open for both: the board's test says unlocked for all; sent out of the game: leader 1, partner 1; the line 0 time(s)
```

De test gaat door de echte ingang (`spire_bridge_handle_register` met de 'O' sub 2 van de basisquest) met q981 en de
voorwaarde die `build.sh --ladder` schrijft (`V_Difficulty == 1 && SPIRE_RUN1 >= 1`). De toets in
`spire_base_game_start` zit in een coroutine achter timers en het maken van een game; die heeft geen statische test en
is alleen gelezen en gecompileerd.

## Statische tests

`build/newserv run-static-tests`, volledige uitvoer gelezen. Basis `054db56`: exit 0, tien W-regels. Met beide fixes
(bronnen van `219fc01`): exit 0, slotregel `-- All static tests passed`, tien W-regels, dezelfde tien (één "run copy
print not stored", zes "Spire guard ... unreadable", drie "run mag ledger ... lean"). De twee nieuwe gevallen geven
geen W- of E-regel. De run op de eindbinary staat in het antwoord aan de regie: dit rapport is gecommit vóór de
eindbuild.

## Beslissingen (voor het beslis-log van de regie; decisions.md ligt buiten mijn writeSet)

1. Punt A: de regel in plaats van de laagste bezette plek als leider. Dekking: de opdracht zelf ("kan de quest daarna
   toch niet verder, bouw dan niets wat een halve hervatting oplevert") plus de questmeting hierboven.
2. Punt A: 'V' van zo'n client schrijft in die game niets weg. Zo is "kost nooit zijn bewaarpunt" een eigenschap van de
   server en hangt het niet af van wat de quest stuurt.
3. Punt A: de regel komt alleen bij een bewaarpunt dat bij de quest past; zonder passend bewaarpunt is er niets te
   beschermen en blijft alles zoals het was.
4. Punt B: de toets en de tekst als één functie en één constante in `ReceiveCommands`, twee regels gewijzigd in het
   bord zelf, zodat bord en nieuwe run niet uit elkaar kunnen lopen.
5. Punt B: twee plekken. De eerste zegt het de leider terwijl hij nog in de basis staat; de tweede dicht het venster
   in de nieuwe game.
6. Punt B: tickerregel in plaats van een venster, zonder kleurcode, zoals de andere weigeringen in dezelfde functie.
7. Een testhaak `spire_static_test_base_run_quest` naast de bestaande `spire_static_test_forget`.

## Gezien, niet gebouwd

- quest · alleen starten bestaat niet op 83d3f9c, ook niet op plek 1 (`run.min_players` 2) · `spire.json:56`,
  `make_spire.py:2304-2306` · questkeuze, zie "Voor de quest".
- server · twee spelers zonder leider (plekken 2 en 3) starten stil vers en niemand leidt de quest · zelfde pad in
  `spire_resume_answer` · bewust ongemoeid (opdracht: met twee spelers verandert niets).
- server · "Start a new run" valt terug op q921 als de basisgame geen runquest kent (basis via het loket, of een tweede
  verzoek na het eerste), ook op Hard · `SpireFlagBridge.cc:5932` · weigert daar nu met de bordregel in plaats van q921
  op Hard te starten; de juiste quest per moeilijkheid kiezen is niet gebouwd.
- server · gelezen, niet gemeten: de basisgame na een Hard-run wordt als Normal-modus-game op Hard gemaakt; de
  ladderpoort opent het minimumniveau alleen voor Battle-games (`src/SpireMeta.cc:376-383`,
  `src/ReceiveCommands.cc:4618-4637`), dus een karakter onder het Hard-minimum zou geen basisgame krijgen.
- server · `$quest` (alleen, of debug) toetst de ontgrendeling niet (`src/ChatCommands.cc:2347-2381`); een basis uit zo'n
  run weigert "Start a new run" nu als de quest niet ontgrendeld is.
- server · na een weigering in de nieuwe run-game zit de groep in een Battle-game zonder quest: via het bord kiezen of
  weggaan.
- werkwijze · de commit-haak meldt dat voor deze newserv-worktree geen integratiebranch en uitrol zijn vastgelegd ·
  niets gepusht of samengevoegd.
