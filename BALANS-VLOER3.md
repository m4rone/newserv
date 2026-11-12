# Balans-vloer3: levelvloer per kamer (B-BAL-21), haalbaarheid

Worktree `/home/m4rck/code/worktrees/newserv/balans-vloer3`, tak `spike/balans-vloer3` op b305a67. Alleen gelezen,
niets aan de code gebouwd.

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
