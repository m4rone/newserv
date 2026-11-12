# Balans-vloer3: levelvloer per kamer (B-BAL-21), haalbaarheid

Worktree `/home/m4rck/code/worktrees/newserv/balans-vloer3`, tak `spike/balans-vloer3` op b305a67. Eerst alleen
gelezen (hieronder tot "Dekking"); daarna op besluit B-BAL-23 route 1 gebouwd (zie "Gebouwd: route 1").

## Uitkomst: geen route op deze client met wat newserv en het questscript nu kunnen

De doorlopende run is één quest (q920, 15 kamers in `quests/spire/spire.json` `rooms`, k2-k8 zijn vloerwissels binnen
die quest, geen nieuwe questlading). Een vloer per kamer vraagt dus dat de server midden in de quest het levende level
van één speler optilt. Daar is op GC v3 geen opdracht voor:

| Route | Waarom niet | Bron |
|---|---|---|
| 6x30 (level wijzigen) van de server | beschermd op V3: de client negeert een 6x30 over zijn eigen speler. newserv stuurt hem zelf alleen naar BB | `src/CommandFormats.hh:3582-3586`, `:4124`; `src/ReceiveSubcommands.cc:4167-4171` (`send_level_up` alleen bij BB_V4) |
| 6xBF (EXP geven) | alleen BB; op GC is 6xBF de lobbymuziek van Ep3. `send_give_experience` gooit bij een niet-BB-client | `src/CommandFormats.hh:5820-5842`; `src/SendCommands.cc:3259-3263` |
| 6xD0 (battle level-up) | alleen BB (tabel: NONE voor v3) | `src/CommandFormats.hh:5960`; `src/ReceiveSubcommands.cc:5975` |
| EXP op de server optellen | op alle niet-BB-versies telt de client zelf de EXP | `src/ReceiveSubcommands.cc:4381` |
| questopcode `ba_set_char_level` (F871) | een regel van het battleregelblok, gelezen bij het laden van de quest: dat is precies de bestaande laadvloer | `src/QuestScript.cc:1789`, `:5688-5689` |
| questopcode `ba_death_lvl_up` (F81D) | levels bij herrijzen, voor iedereen gelijk, en in de Spire staat herrijzen naar de stad uit | `src/QuestScript.cc:1523-1524`; `spire.json` `run._lives` |
| een EXP- of levelopcode in het questscript | bestaat niet op v3 (er is `pl_add_meseta`, geen `pl_add_exp`) | `src/QuestScript.cc:1082`, `:1383`; `/usr/bin/grep -n -i exp src/QuestScript.cc` geeft alleen de challenge-vermenigvuldiger (`:1548`) |
| `SetExtendedPlayerInfo` midden in de quest (de push van "opnieuw spelen") | schrijft alleen de savebuffer; de client kopieert zijn levende speler bij de volgende vloerwissel terug, dus de push haalt het levende level niet (gemeten in het lab 2026-10-26) | `system/client-functions/SetExtendedPlayerInfoGC.s:5-9`; `src/SpireFlagBridge.cc:5528-5531` |

## Wat wel kan

1. **Vloer bij het laden van een kamerquest en bij hervatten (serverkant, klein).** De server kent daar de kamer:
   `SpireRoomQuests` geeft `rq.k` (`src/SpireFlagBridge.cc:4723-4737`), het bestaande pad schrijft het level in het
   regelblok van de bin en in de runkopie (`spire_start_level_quest_bin`, `:4794-4850`). De vaste vloer uit B-BAL-21
   (k1 1 ... k8 13) kan daar naast de vloer "leider min 2" komen, met een `max`. Effect: wie een kamer los start of een
   run hervat, komt op het kamervloerlevel. Het dekt de achterblijver in een doorlopende run niet, en dat is het
   gemeten geval.
2. **Nieuwe clientfunctie (nieuw ontwerp, nieuw spoor).** Er is een voorbeeld dat de levende speler raakt op GPOP8P:
   `system/client-functions/SpireEquipItemGC.s` (`.versions 3OP0`, `TObjPlayer_for_client_id` 0x801BAC08, `:22-24`).
   Een functie die de eigen EXP-routine van de client aanroept met de EXP tot het vloerlevel, laat de client zelf
   levelen en zelf zijn 6x30 sturen; newserv's `on_level_up` zet dat dan in de runkopie (`src/ReceiveSubcommands.cc:4110-4144`).
   Daarvoor moet het adres van die routine uit de disassembly van `PsoV3.dol` komen (de iso staat onder
   `~/Downloads/gc-iso/`, `wit` en `m68kdasm` zijn er), en of het werkt beslist alleen de Wii. Het moment is de 'V'
   bij de kamerstart, waar de server `room_k` al leest (`src/SpireFlagBridge.cc:1960`). Dat is geen kleine wijziging in
   `SpireFlagBridge.cc`; het past niet in deze opdracht.
3. **Inhaal-EXP in plaats van een vloer** (bijvoorbeeld per client een EXP-vermenigvuldiger zoals
   `ExpMultiplierGC.s` op de pc): ook een clientpatch en een ander besluit dan B-BAL-21.

## Keuze

Niets gebouwd, zoals de opdracht voorschrijft als er geen route is. Route 1 is niet alvast gebouwd: hij haalt het
gemeten geval niet en de opdracht zegt bij geen route "schrijf op wat kan en stop". Route 2 hoort bij de regie als
nieuw spoor (clientfunctie, disassembly, Wii-meting); ik heb geen expansion-verzoek geschreven, omdat ik buiten deze
worktree niet mag schrijven.

## Dekking

Dit dekt de vraag of de server of het questscript op GC v3 midden in een quest het level van één speler kan optillen,
op codelezing van newserv en zijn clientfuncties. Het dekt niet of de GC-client een opgetild level werkelijk toont en
gebruikt (een waarneming op de Wii), niet de balans van de vijanden, en geen gebouwde code of tests: er is niets
gebouwd en niet gedraaid.

## Gebouwd: route 1 (B-BAL-23)

Codecommit 4b0741aa47a55ab908255f9d7769a0ba14a132e3 (`feat: fixed level floor per room quest of Episode 1`).
`build/newserv help | head -1` gaf `newserv-4b0741a built 2026-11-12 07:46:06.000000 UTC`.

**Wat het doet.** Bij het laden van een kamerquest (los starten of hervatten) krijgt elke speler die onder de
bescherming valt minstens de vaste vloer van die kamer in zijn bin en zijn run-kopie: de hoogste van de vaste vloer en
de bestaande vloer "leider min 2". Wie erboven zit verandert niet. Het echte karakter wordt niet geraakt: het level
gaat via het regelblok van de bin en `SpireStartLevelSent` naar de overlay (de run-kopie), zoals de bestaande vloer.

**Welke kamers.** De k komt uit `SpireRoomQuests` (`rq.k`, met dezelfde episodecontrole als
`spire_resume_level_for_client`). In de kamerquest-build (`quests/spire/build-rooms/newserv-config.json` in
balans-bundel4) ziet de server Episode 1 k1-k15 en Episode 2 k16-k30. Alleen Episode 1 k1-k8 heeft een vaste vloer;
k9-k15 en Episode 2 (k16-k30) houden wat er al was. q920 (de doorlopende run) staat niet in `SpireRoomQuests` en
krijgt dus geen vaste vloer.

**Wijzigingen.**
- `src/SpireMeta.hh` (+4) en `src/SpireMeta.cc` (+5): `spire_room_level_floor(episode, k)`, de tabel
  (0-based 0 3 5 7 9 10 11 12 = Lv1 4 6 8 10 11 12 13); 0 buiten Episode 1 k1-k8.
- `src/SpireFlagBridge.cc` (+9, -1), één hunk `@@ -4783 +4783,9 @@` in `spire_level_floor_for_client`: na de regel met
  `spire_level_floor_char_level` de opzoeking van `rq.k` en `max`, met een eigen logregel
  "Spire level floor: Lv{} -> Lv{} (room {}'s floor)". De oude `if (level > own)` werd `else if`. Omdat zowel de
  starttak als de hervattak van `spire_start_level_quest_bin` (`:4794-4850`) en "opnieuw spelen" deze functie
  aanroepen, krijgen alle drie de vloer zonder verdere wijziging. Het zit binnen de bestaande `try`: bij een fout komt er
  een waarschuwingsregel en geldt het eigen level, er komt geen throw uit het pad.
- `src/StaticTests.cc` (+72): een blok in de padtest voor twee clients (na "a quest the guard does not cover").

`git diff b305a67 --stat -- src/` bij 4b0741a: SpireFlagBridge.cc 10 (+9 -1), SpireMeta.cc 5, SpireMeta.hh 4,
StaticTests.cc 72.

**Test.** Het padblok dekt het volgende: alleen in k5 op Lv1 bij een start geeft Lv10, en het echte karakter blijft Lv1;
hervatten in k5 op een bewaarde Lv1 geeft Lv10, op een bewaarde Lv14 blijft het Lv14; een leider op Lv15 geeft de
achterblijver Lv13 (hoger dan Lv10), en de leider houdt Lv15; een leider op Lv1 geeft beide spelers de vloer (Lv10);
k1 verandert niets; k8 geeft Lv13; k9, k0 en een Ep2-kamer in een Ep1-spel veranderen niets; een beschermd (echt)
karakter en een quest buiten de bescherming krijgen niets.

Rood met een stomp (`spire_room_level_floor` gaf altijd 0, de aanroep stond er al), exit 1:

```
E 187002 2026-11-12 08:44:30 - run-static-tests: failure at /home/m4rck/code/worktrees/newserv/balans-vloer3/src/StaticTests.cc:10711: bin_at(9) != sent_to(fresh)
```

Groen met de tabel, en opnieuw na de herbouw met de verse revisie, exit 0:

```
I 189451 2026-11-12 08:45:16 - [C-48] Spire level floor: Lv1 -> Lv10 (room 5's floor)
I 189451 2026-11-12 08:45:16 - [C-48] Spire savestate: q987655 sent with the rule block's level Lv1 -> Lv10 (the saved run level)
I 189451 2026-11-12 08:45:16 - [C-48] Spire savestate: q987655 sent with the rule block's level Lv1 -> Lv14 (the saved run level)
I 189451 2026-11-12 08:45:16 - [C-48] Spire level floor: Lv1 -> Lv13 (the leader's Lv15 minus 2)
I 189451 2026-11-12 08:45:16 - [C-48] Spire level floor: Lv1 -> Lv13 (room 8's floor)
I 194492 2026-11-12 08:46:20 - -- All static tests passed
```

De vijf foutregels "Inventory backup: ... could not be written" komen uit bestaande tests die een kapot bestand
voeren. Ze staan zowel in de rode als in de groene run en horen niet bij het nieuwe blok.

**Keuzes.**
- De vaste vloer geldt voor iedere speler, ook voor de leider en voor wie solo speelt ("elke speler komt minstens op
  het vloerlevel"). Het level van de leider voor de vloer "leider min 2" telt de vaste vloer niet mee; dat hoeft ook
  niet, want de vaste vloer is altijd hoger dan zichzelf min 2.
- Bij "opnieuw spelen" in een kamerquest geldt de vloer ook, omdat dezelfde functie wordt aangeroepen. Er is geen apart
  pad bijgebouwd.

**Dekking van route 1.** Dit dekt het level dat de server bij het laden van een kamerquest (start, hervatten) in de bin
en de run-kopie zet, en zijn statische tests. Het dekt niet de achterblijver in de doorlopende run q920 (daar bestaat
geen route, zie boven), niet of de GC-client het opgetilde level werkelijk toont en gebruikt (een waarneming op de
Wii), en niet de balans van de vijanden.

## Hervatten van een doorlopende run: al gedekt

Een doorlopende run (q920) hervat via een kamerquest uit `SpireRoomQuests`, niet in q920 zelf. Er is dus niets
bijgebouwd en de code is ongewijzigd sinds 4b0741a.

- Alleen een quest die na de eerste kamer begint heeft het hervatpad: `RESUME = SAVESTATE and bool(START) and
  START[0] > K0` (balans-bundel4 `quests/spire/make_spire.py:837-839`). q920 begint bij K0 = k1 en vraagt dus nooit
  'J'. Het protocol zegt hetzelfde: "Resume: a room quest that starts after K0 writes 'J'" (`src/SpireFlagBridge.hh:300`).
- De melding bij binnenkomst wijst de speler naar de kamerquest: "To continue: Quest > {categorie} > Spire room {k}"
  (`src/SpireFlagBridge.cc:1036`, de naam uit `spire_savestate_room_quest_name` `:983`). De build geeft die quests
  precies die naam (`quests/spire/build.sh:508`, `name='Spire room %s'`) en zet ze in `SpireRoomQuests`
  (`build-rooms/newserv-config.json`, Ep1 k1-k15, Ep2 k16-k30).
- Het hervatlevel bestaat alleen voor een quest in `SpireRoomQuests` (`src/SpireFlagBridge.cc:4723-4725`). Die kamerquest
  gaat door `spire_level_floor_for_client` en krijgt dus de vaste vloer van 4b0741a. Het padblok test dat al: hervatten
  in k5 op een bewaarde Lv1 geeft Lv10.
- `SpireCheckpointRooms` laadt niets. Het koppelt een gewonnen kamer aan zijn k voor het checkpointrecord
  (`src/DataIndex.hh:187-191`).
- Een verse start van q920 verandert niet: q920 staat niet in `SpireRoomQuests`.

**Gezien.** De kamerquests k2-k8 hebben in de build al het vloerlevel als eigen questlevel:
`spire-expeditions.json` `room_quests_k2_k8.char_level` = k2 3, k3 5, k4 7, k5 9, k6 10, k7 11, k8 12 (0-based, zelfde
tabel als B-BAL-21; `quests/spire/build.sh:511-512`). Het hervatpad zet nooit een level onder dat van de quest
(`spire_quest_load_char_level`, `src/SpireMeta.cc:924-929`). Op de kamerquests die nu bestaan gaf de server dus al het
vloerlevel, voor zowel een start als een hervat. De vloer van 4b0741a is daar een vangnet; hij werkt pas als een
kamerquest ooit met een lager questlevel wordt gebouwd. De gemeten achterblijver (Lv1 in k1-k4) speelt in q920, en
daar is midden in de quest geen route (zie boven).
