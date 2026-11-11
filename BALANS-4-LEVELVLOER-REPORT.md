# Balans 4: levelvloer voor de achterblijver (B-REGIE-20-5)

Worktree `/home/m4rck/code/worktrees/newserv/balans-1`, tak `spike/balans`, gebouwd op 9e40c01.
Codecommits: e91dba414ea3ed5c85927beba929d21d5c865ef5 (`feat: level floor for the trailing player at quest load`),
e3394dd04c0493eea675951ba9ca14ff8500c796 (`fix: the level floor follows the quest leader in slot 0, not the game
leader`) en 00897e4c68e63e1e6f3cd8de92d112df663750d1 (`fix: the heat follows the quest leader in slot 0, as the level
floor does`). Overal waar hieronder "leider" staat is dat sinds de tweede commit de questleider: de speler op de plek
uit `SpireFollowLeader` (anders plek 0), niet de spelleider (`l->leader_id`). Zie "Vervolg: de questleider is de maat"
en "Hitte volgt de questleider". De uitvoer onder "Tests" is van de eerste commit; de uitvoer van de eindstand staat
onder "Hitte volgt de questleider".

## Wat er nu gebeurt

Bij elke questlading rekent newserv per client uit welk level hij voor deze lading zelf heeft (bij hervatten het
bewaarde level, anders questlevel plus gekocht startlevel). Wie niet de leider van de game is en daarmee onder
"level van de leider min 2" uitkomt, krijgt dat vloerlevel in zijn quest-bin en in zijn runkopie op de server. Wie op of
boven de vloer zit, de leider zelf en een solo-speler krijgen precies wat ze voorheen kregen.

## Verdict per acceptatiecriterium

| AC | Stand | Bewijs |
|---|---|---|
| 1 vloerlevel in de bin van de achterblijver | gehaald | padtest: leider Lv15, achterblijver krijgt bin met Lv13 in plaats van Lv1 |
| 2 nooit omlaag, leider en solo ongewijzigd | gehaald | pure test (op, boven, voor de leider uit) en padtest (leider wisselt, solo: dezelfde bin terug) |
| 3 start, kamerquest, hervatten | gehaald voor start en kamerquest op het echte pad; hervatten alleen via de pure samenstelling en codelezing | zie "Wat niet gemeten is" |
| 4 runkopie op de server klopt met de bin | gehaald | padtest: overlay van de achterblijver staat op Lv13, die van de leider op Lv15 |
| 5 heat verandert niet | gehaald op codelezing, niet apart getest | zie "Heat" |
| 6 0-based, niet boven het maximum | gehaald | pure test: leider 199 geeft 197, ook bij 250 en 0xFFFFFFFF; leider op 0, 1, 2 geeft 0 |

## Wijzigingen

- `src/SpireMeta.hh`, `src/SpireMeta.cc`: `SPIRE_LEVEL_FLOOR_GAP = 2`,
  `spire_quest_load_char_level(base, bonus, resume_level)` (eigen level voor een questlading) en
  `spire_level_floor_char_level(own_level, leader_level, is_leader)` (de vloer). Alle rekenlogica staat hier.
- `src/StaticTests.cc`: drie testblokken (regels 1525, 1567 en 7682) en `#include "Compression.hh"`.
- `src/SpireFlagBridge.cc`, `src/SpireFlagBridge.hh`: alleen de aanroep, zie hieronder.

## Geraakte regels in SpireFlagBridge (voor het samenvoegen)

Regelnummers van vóór de wijziging (9e40c01) en van erna (00897e4, alle drie de commits samen).
`git diff 9e40c01 --stat -- src/SpireFlagBridge.cc src/SpireFlagBridge.hh` geeft:

```
 src/SpireFlagBridge.cc | 51 ++++++++++++++++++++++++++++++++++++++++++++------
 src/SpireFlagBridge.hh |  5 ++++-
 2 files changed, 49 insertions(+), 7 deletions(-)
```

Samen 49 regels erbij en 7 weg (de vloer 38 erbij en 4 weg, de hitte 11 erbij en 3 weg); dat is boven de
oorspronkelijke richtwaarde van 25. Reden: de geldigheidscontrole en het level van de leider hebben de bestandslokale
functies `spire_start_level_bonus_for_client` en `spire_resume_level_for_client` nodig, dus de lijm kan niet in
SpireMeta staan.

`src/SpireFlagBridge.cc`:
- na oud 4654 (nieuw 4655-4682): nieuwe statische functie `spire_level_floor_for_client`, 28 regels, pure toevoeging
  tussen `spire_resume_level_for_client` en `spire_start_level_quest_bin`. De leider komt uit nieuw 4667-4670
  (`spire_follow_leader`, anders plek 0).
- na oud 4663 (nieuw 4692), hervat-tak: één regel erbij,
  `level = spire_level_floor_for_client(c, l, resume_level, 0).value_or(level);`
- oud 4683 (nieuw 4712-4713): `if (!bonus || !bin_contents)` werd `auto floor_level = ...` plus
  `if ((!bonus && !floor_level) || !bin_contents)`.
- oud 4689 (nieuw 4719): `uint32_t level = floor_level.value_or(spire_start_level_char_level(base, bonus));`
- oud 4698 (nieuw 4728-4730): de `spire_start_level_sent`-toewijzing; bij een vloer
  `SpireStartLevelSent{lobby_id, quest_number, 0, level, bonus}`, anders ongewijzigd.
- oud 4700 (nieuw 4732): laatste argument van de logregel, `floor_level ? bonus : (level - base)`.
- oud 4715 (nieuw 4747), hitte: eerste commentaarregel boven `spire_heat_for_leader`, "game leader's" werd
  "quest leader's".
- na oud 4717 (nieuw 4750-4751), hitte: twee commentaarregels erbij (wie de leider is, en de terugval).
- oud 4720 (nieuw 4754-4759), hitte: de regel `auto leader = (l->leader_id < ...) ? l->clients[l->leader_id] : nullptr;`
  werd zes regels: de plek uit `spire_follow_leader` (anders 0), de terugval op `l->leader_id` als daar niemand zit,
  en de client op die plek.

`src/SpireFlagBridge.hh`:
- na oud 1366 (nieuw 1367-1368): twee commentaarregels boven de declaratie van `spire_start_level_quest_bin`.
- oud 1372 (nieuw 1374-1375), hitte: één commentaarregel boven `spire_heat_quest_bin` werd er twee.

`spire_create_battle_overlay`, `spire_resume_level_for_client`, `spire_base_leader_serial` en `spire_base_new_run` zijn
niet aangeraakt.

## Tests

Bouw: `cmake -DCMAKE_BUILD_TYPE=Release ..` in `build/` (Unix Makefiles, Release, zoals kwaliteit-2), daarna
`make -C build -j20`. Tests: `./build/newserv run-static-tests` vanuit de worktree-root.

### Rood zonder de fix

De vloerfunctie was eerst een stomp die het eigen level teruggaf (het gedrag van vóór de wijziging); de aanroep in
SpireFlagBridge stond er al. Uitvoer, exitcode 1:

```
I 569792 2026-11-11 14:22:36 - -- Spire start level: overlay level = quest level + owned start_level, rule block raised once (v52)
I 569792 2026-11-11 14:22:36 - -- Spire level floor: the trailing player loads a quest at the leader's level minus 2 at least (B-REGIE-20-5)
E 569792 2026-11-11 14:22:36 - run-static-tests: failure at /home/m4rck/code/worktrees/newserv/balans-1/src/StaticTests.cc:1536: 18u != spire_level_floor_char_level(0, 20, false)
```

De testrunner stopt bij de eerste fout. Om het tweede blok (de bin van twee spelers) ook rood te zien is het eerste
blok tijdelijk overgeslagen, met dezelfde stomp. Uitvoer, exitcode 1:

```
I 574438 2026-11-11 14:23:26 - -- Spire level floor: the quest .bin of two players, each patched to its own floored level (B-REGIE-20-5)
E 574438 2026-11-11 14:23:26 - run-static-tests: failure at /home/m4rck/code/worktrees/newserv/balans-1/src/StaticTests.cc:1594: bin_at(12) != bin_for(0, trailing, leader, false)
```

De regelnummers 1536 en 1594 zijn in de commit 1537 en 1595 (er kwam daarna één include-regel bij).

Het derde blok (het echte pad met twee clients) is geschreven nadat de fix erin zat en is niet rood gedraaid. Dat het
zonder de wijziging faalt volgt alleen uit de oude code: bij bonus 0 gaf `if (!bonus || !bin_contents) return
bin_contents;` de ongewijzigde bin terug, dus Lv1 en niet Lv13.

### Groen met de fix

Het padblok, letterlijk (client C-A bezit startlevel 3, C-B bezit niets; C-A is leider):

```
I 596706 2026-11-11 14:27:39 - -- Spire level floor: spire_start_level_quest_bin and the overlay for two clients of one game (B-REGIE-20-5)
I 596706 2026-11-11 14:27:39 - [C-A] Spire start level: q987655 sent with the rule block's level Lv1 -> Lv15 (start_level +14)
I 596706 2026-11-11 14:27:39 - [C-B] Spire level floor: Lv1 -> Lv13 (the leader's Lv15 minus 2)
I 596706 2026-11-11 14:27:39 - [C-B] Spire start level: q987655 sent with the rule block's level Lv1 -> Lv13 (start_level +0)
I 596706 2026-11-11 14:27:39 - [C-A] Spire start level: q987655 sent with the rule block's level Lv1 -> Lv15 (start_level +14)
I 596706 2026-11-11 14:27:39 - [C-A] Spire start level: q987655 sent with the rule block's level Lv1 -> Lv15 (start_level +14)
I 596706 2026-11-11 14:27:39 - [C-B] Spire level floor: Lv1 -> Lv13 (the leader's Lv15 minus 2)
I 596706 2026-11-11 14:27:39 - [C-B] Spire start level: q987655 sent with the rule block's level Lv1 -> Lv13 (start_level +0)
I 596706 2026-11-11 14:27:39 - [C-A] Spire guard: run started for serial 0000004300 in q987655; marker set, snapshot  Lv1 meseta 0
I 596706 2026-11-11 14:27:39 - [C-A] Spire start level: overlay Lv1 -> Lv15 (start_level +14)
I 596706 2026-11-11 14:27:39 - [C-A] Created battle overlay
I 596706 2026-11-11 14:27:39 - [C-B] Spire guard: run started for serial 0000004301 in q987655; marker set, snapshot  Lv1 meseta 0
I 596706 2026-11-11 14:27:39 - [C-B] Spire savestate: overlay Lv1 -> Lv13 (the saved run level, as its .bin)
I 596706 2026-11-11 14:27:39 - [C-B] Created battle overlay
I 596706 2026-11-11 14:27:39 - [C-B] Spire savestate: the run's start_level bonus +0 (saved)
```

De staart van de hele statische testrun (14040 regels uitvoer), met de exitcode als eigen regel:

```
I 596706 2026-11-11 14:27:39 - [C-B] Spire savestate: the run's start_level bonus +0 (saved)
I 596706 2026-11-11 14:27:39 - [C-B] Deleted
I 596706 2026-11-11 14:27:39 - [C-A] Deleted
I 596706 2026-11-11 14:27:39 - [Game:7A03] Deleted
I 596706 2026-11-11 14:27:39 - -- All static tests passed
0
```

In de hele uitvoer staan 10 waarschuwingsregels en geen foutregel. Alle 10 horen bij bestaande tests die een kapot
bestand voeren (7 keer "Spire guard: ... unreadable/not stored", 3 keer "run mag ledger ... has lean"); geen ervan
komt uit de nieuwe blokken. Er zijn geen overgeslagen tests gemeld.

## Heat

De heat van de game komt uit `spire_heat_for_leader`, die het gekochte startlevel van de leider leest via
`spire_start_level_bonus_for_client`. De heat leest alleen wat er gekocht is, nooit het level in de bin, dus de vloer
verandert hem niet. Sinds de derde commit is de leider van de heat dezelfde als die van de vloer (de questleider; zie
"Hitte volgt de questleider"), en die krijgt zelf nooit een vloer. Voor een
achterblijver met een vloer blijft de bonus van zijn run het gekochte aantal (in de uitvoer hierboven "+0"), dus zijn
eigen startantwoord, de startvloeren van de run-mag en het bewaarde `StartBonus` rekenen niet met de vloerlevels. Dit
is gelezen en in het padblok zichtbaar in de logregel, maar er is geen aparte heat-test voor geschreven.

## Keuzes (met het alternatief)

1. **Wie de leider is (herzien in de vervolgopdracht):** de questleider, de speler op de plek uit `SpireFollowLeader`
   (anders plek 0), precies zoals `spire_resume_level_for_client` en de quest hem nemen. De eerste commit nam de
   spelleider (`l->leader_id`); die keuze is vervangen. Sinds de derde commit neemt de heat dezelfde leider. Eén
   verschil blijft: zit er niemand op de plek van de questleider, dan valt de heat terug op de spelleider en geeft de
   vloer geen vloer (zie "Hitte volgt de questleider").
2. **Hoe de vloer bij de runkopie komt:** via het bestaande veld `resume_level` van `SpireStartLevelSent` (een
   absoluut level), met de gekochte bonus als bonus van de run. Daardoor hoefde `spire_create_battle_overlay` niet te
   veranderen en lekt de vloer niet in bonus, heat of mag. Prijs: voor een speler met een vloer zeggen twee bestaande
   logregels "Spire savestate: ... (the saved run level, as its .bin)" en "(saved)" terwijl er niets hervat wordt.
   Alternatief: een eigen veld in de struct en een eigen tak in de overlay, ongeveer 15 regels extra in het bestand van
   het andere spoor.
3. **Leider die niet onder de bescherming valt** (andere clientversie, beschermd karakter): zijn level telt dan als het
   questlevel, dus er komt geen vloer. Alternatief: zijn echte level opzoeken; niet gedaan.
4. **Nieuwe partner zonder bewaarde run naast een leider die hervat:** hij krijgt de vloer van het bewaarde level van
   de leider. Dat volgt uit "elke questlading"; het alternatief (alleen een vloer als hij zelf ook hervat) is strenger
   dan het besluit.

## Vervolg: de questleider is de maat

Aanleiding: A maakt het spel (plek 0), B komt erbij, A gaat naar de lobby en terug. A zit weer op plek 0, maar B
blijft spelleider. In de run is A de questleider; de vloer moet A's level als maat nemen.

Test eerst: het blok voor twee clients (`src/StaticTests.cc:7682`) kreeg een spel met `l->leader_id = 1` en de sterkste
speler op plek 0. Rood tegen de code van e91dba4, exitcode 1, letterlijk:

```
E 682839 2026-11-11 14:37:09 - run-static-tests: failure at /home/m4rck/code/worktrees/newserv/balans-1/src/StaticTests.cc:7793: bin_at(12) != sent_to(fresh)
```

Regel 7793 is de speler op plek 1 die de vloer van plek 0 hoort te krijgen terwijl hij zelf spelleider is; de oude
code gaf hem de ongewijzigde bin (Lv1).

De fix: `spire_level_floor_for_client` neemt de leider met dezelfde twee regels als `spire_resume_level_for_client`
(`follow ? follow->first : 0`, daarna de client op die plek). Geen gedeelde hulpfunctie: die had de functie van het
andere spoor moeten wijzigen en de wijziging groter gemaakt. Is de plek van de questleider leeg, dan komt er geen
vloer. Het commentaar boven de functie, de twee commentaarregels in `SpireFlagBridge.hh` en het commentaar in
`SpireMeta.hh` noemen nu de questleider.

Het blok dekt nu op het echte pad: spelleider op plek 0 en op plek 1 met de sterkste op plek 0 (plek 1 krijgt Lv13,
plek 0 houdt Lv15); `SpireFollowLeader` op plek 1 (beide houden hun eigen level, bij beide spelleiders); de speler
zonder aankopen op plek 0 (beide houden hun eigen level, bij beide spelleiders); alleen op plek 0 en alleen op plek 1;
een quest buiten de bescherming.

Groen met de fix: de staart van de hele run (14046 regels, 10 waarschuwingsregels en geen foutregel, dezelfde tien
als eerder) met de exitcode als eigen regel:

```
I 705279 2026-11-11 14:41:29 - [C-B] Spire guard: run started for serial 0000004301 in q987655; marker set, snapshot  Lv1 meseta 0
I 705279 2026-11-11 14:41:29 - [C-B] Spire savestate: overlay Lv1 -> Lv13 (the saved run level, as its .bin)
I 705279 2026-11-11 14:41:29 - [C-B] Created battle overlay
I 705279 2026-11-11 14:41:29 - [C-B] Spire savestate: the run's start_level bonus +0 (saved)
I 705279 2026-11-11 14:41:29 - [C-B] Deleted
I 705279 2026-11-11 14:41:29 - [C-A] Deleted
I 705279 2026-11-11 14:41:29 - [Game:7A03] Deleted
I 705279 2026-11-11 14:41:29 - -- All static tests passed
0
```

Dekking van dit vervolg: welke speler de maat van de vloer is op de drie laadmomenten. Niet: de heat (toen nog op
`l->leader_id`; verlegd in de volgende sectie) en niet de Wii.

## Hitte volgt de questleider

Aanleiding: hetzelfde lobby-en-terug-geval. A zit op plek 0 en leidt de quest (kamers, pacten, klok), maar B is
spelleider en B's aankopen bepaalden hoe sterk de vijanden zijn.

### Aanroepers en wat de quest ermee doet

- `spire_heat_for_leader` heeft één aanroeper: `spire_heat_quest_bin` (`src/SpireFlagBridge.cc:4813`), die bij het
  laden van de quest voor elke client de rijen van één hittestap in de bin schrijft. Alle clients van een game
  krijgen dezelfde bytes (één keer gecomprimeerd per bin en stap, `:4820-4825`), omdat de leider voor alle clients
  dezelfde is. Dat blijft zo: de leider hangt alleen van de game af, niet van de client die de bin krijgt.
- `spire_heat_quest_bin` onthoudt per client de stap die hij stuurde (`spire_heat_sent`, `:4816`, `:4824`, `:4839`).
- Het startantwoord van de winkel (`:3315-3321`) stuurt elke client die onthouden stap als de hitte van de game
  (`spire_send_register(c, SPIRE_SHOP_HEAT_REGISTER, heat)`, `:3333`), niet zijn eigen berekende hitte. De quest
  speelt dus de rijen die in zijn bin staan en krijgt het bijbehorende getal; dat paar blijft kloppen, wie de leider
  ook is. Buiten het startmoment stuurt dezelfde regel de eigen hitte van de client (winkelscherm); dat is niet
  gewijzigd.
- Verder leest niets de leider van de hitte. Verleggen verandert dus alleen wiens aankopen de stap bepalen.

### Lege plek van de questleider

De oude code nam altijd de spelleider. De nieuwe neemt de plek uit `SpireFollowLeader` (anders plek 0); zit daar
niemand, dan valt hij terug op de spelleider (`l->leader_id`). Een lege plek wordt dus nooit als leider genomen. Staat
er ook op `l->leader_id` niemand, dan meldt hij zoals voorheen "no GC leader" en speelt de game stap 0.

Verschil met de vloer: bij een lege leidersplek geeft de vloer geen vloer (zo opgeleverd in het vorige vervolg), de
hitte neemt de spelleider. Voor de vloer is "geen leider, geen vloer" een veilige uitkomst; de hitte moet altijd een
stap kiezen. Wil de regie dat ook de vloer dan op de spelleider terugvalt, dan is dat één regel in
`spire_level_floor_for_client`.

Geen gedeelde hulpfunctie: vloer en hitte hebben nu elk hun eigen regels voor de leider (de hitte met terugval, de
vloer zonder). Een helper van ongeveer acht regels had er zes vervangen door twee en het totaal laten groeien; de
voorwaarde in de opdracht was dat het niet groeit.

### Test

Het blok voor twee clients (`src/StaticTests.cc:7682`, het hittedeel vanaf regel 7825) kreeg een derde client die alle
treden van hp_mat en atp_grind bezit (sterkte 7, hittestap 2 voor een HUmar op laddertrede 0) naast de client zonder aankopen
(stap 0), en een hittebestand waarin één byte de stap noemt. De test leest de stap terug uit de bin die elke client
krijgt.

Rood tegen de code van 3f1e031, exitcode 1, letterlijk (de twee regels ervoor laten zien wiens hitte genomen werd):

```
I 765350 2026-11-11 14:48:55 - [C-B] Spire heat: q987655 sent at H 2 (leader 0000004302 HUmar, P 7, rung 0, H_auto 2, H_keuze 0), the same .bin as the game's other clients
I 765350 2026-11-11 14:48:55 - [C-C] Spire heat: q987655 sent at H 0 (leader 0000004301 HUmar, P 0, rung 0, H_auto 0, H_keuze 0), the quest's own rows
E 765350 2026-11-11 14:48:55 - run-static-tests: failure at /home/m4rck/code/worktrees/newserv/balans-1/src/StaticTests.cc:7860: step != heat_step_sent_to(c)
```

Regel 7860 is de controle in de hulpfunctie; het falende geval is de aanroep op regel 7868: de sterke client op plek
0, `l->leader_id = 1`. De oude code nam de hitte van de client zonder aankopen (stap 0 in plaats van 2).

Groen met de fix dekt het blok: de sterke op plek 0 met spelleider 0 en 1 (stap 2 voor beide clients); de zwakke op
plek 0 met spelleider 0 en 1 (stap 0); `SpireFollowLeader` op plek 1 (de client op plek 1 is de maat, bij beide
spelleiders, in beide opstellingen); `SpireFollowLeader` op een lege plek 2 (terugval op de spelleider: stap 2 bij
spelleider 0, stap 0 bij spelleider 1); plek 0 leeg met de clients op plek 1 en 2 (terugval: stap 2 bij spelleider 1,
stap 0 bij spelleider 2).

De staart van de hele run (14073 regels, 10 waarschuwingsregels van bestaande tests, geen foutregel) met de exitcode
als eigen regel:

```
I 779764 2026-11-11 14:51:55 - [C-B] Created battle overlay
I 779764 2026-11-11 14:51:55 - [C-B] Spire savestate: the run's start_level bonus +0 (saved)
I 779764 2026-11-11 14:51:55 - [C-B] Deleted
I 779764 2026-11-11 14:51:55 - [C-A] Deleted
I 779764 2026-11-11 14:51:55 - [Game:7A03] Deleted
I 779764 2026-11-11 14:51:55 - -- All static tests passed
0
```

Dekking van dit deel: welke speler de hitte van een game bepaalt bij het laden van de quest. Niet: de basisstroom
(zie "Gezien"), niet wat de client toont, niet de Wii.

## Wat niet gemeten is

- Hervatten op het echte pad: de hervat-tak is één regel en gebruikt dezelfde functies, maar het padblok laadt geen
  kamerquest met bewaarde runs. Gedekt door het pure blok (bewaard Lv12 naast Lv31 geeft Lv29) en door codelezing.
- Het derde testblok is niet rood gedraaid (zie boven).
- Een kamerquest met een ander basislevel (Lv28) is alleen in het pure blok getest, niet op het echte pad.

## Dekkingsclaim

Dit dekt: het level dat de server in de quest-bin van een niet-leider schrijft op de drie laadmomenten, en de
consistentie met wat de server aan de client meldt (de runkopie op de server). Het dekt niet: of de GameCube-client dat
level op de Wii werkelijk toepast voor een tweede speler (alleen de Wii beslist dat), en niet het effect op de balans
(dat meet een sim-agent).

## Gezien, niet gebouwd

- toekomstbestendigheid · "opnieuw spelen" binnen dezelfde quest (`spire_create_battle_overlay` met `run_again`) bouwt
  de runkopie uit het gekochte startlevel van nu, zonder vloer; de achterblijver valt daar terug op zijn eigen level.
  Het is geen questlading en staat niet in de drie momenten · `src/SpireFlagBridge.cc:4862-4863` · een aanroep in de
  overlay-functie van het andere spoor.
- codekwaliteit · de twee misleidende logregels uit keuze 2 · `src/SpireFlagBridge.cc:4878` en `:4900` · een eigen
  veld of een eigen logtekst bij het samenvoegen.
- codekwaliteit · de voorwaarde "bewaakte Spire-quest met runkopie voor een GC-client" staat nu drie keer uitgeschreven
  (bonus, hervatlevel, vloer) · `src/SpireFlagBridge.cc:4587`, `:4621`, `:4662` · één gedeelde hulpfunctie.
- datakwaliteit, bestond al · het startantwoord en de heat van de leider vergelijken de bonus in levels met het aantal
  gekochte treden (`run_bonus < spire_shop_level(upgrades, 0)`); sinds de bonus per trede meer dan 1 is kloppen die
  eenheden niet. Het heeft nu geen effect omdat startlevels 0 aan de sterkte bijdragen · `src/SpireFlagBridge.cc:3303`
  en `:4775` · uitzoeken en één regel per plek.
- codekwaliteit · de regels die de questleider bepalen staan nu op vier plekken (`:2159`, `:4637`, `:4667`, `:4754`) ·
  `src/SpireFlagBridge.cc` · één gedeelde hulpfunctie, in het bestand van het andere spoor.
- datakwaliteit, basisstroom, niet aangeraakt · `spire_base_leader_serial` (`src/SpireFlagBridge.cc:5641-5642`) neemt
  de spelleider en legt zijn serienummer vast als degene die na de run het volgende spel maakt (`:5671`, gebruikt op
  `:5799`). In het lobby-en-terug-geval is dat B: B maakt het basisspel ("the previous leader creates the next game,
  so he leads it again", `:5797`). Komt de maker daar op plek 0 (niet nagelezen), dan is B vanaf dan questleider én
  spelleider en de maat van vloer en hitte in de volgende run, terwijl A de run leidde · omzetten naar de questleider
  is één regel plus een test.
- gebruiksvriendelijkheid, basisstroom, niet aangeraakt · `spire_base_new_run` (`src/SpireFlagBridge.cc:5849-5856`)
  laat alleen de spelleider een nieuwe run starten (`c->lobby_client_id != l->leader_id`, `:5852`). Is in een basisspel
  de spelleider een ander dan de speler op plek 0, dan krijgt de speler op plek 0 "Only the party leader can start a
  new run." terwijl de quest hem als leider behandelt · dezelfde omzetting.
- datakwaliteit, niet aangeraakt · "opnieuw spelen" geeft de drops de spelleider mee
  (`spire_drops_quest_loaded(l, leader ? leader : c)`, `src/SpireFlagBridge.cc:5952-5953`); in het lobby-en-terug-geval
  is dat B en niet de questleider A. Wat de drops met die leider doen is niet uitgezocht · uitzoeken en zo nodig één
  regel.
- werkwijze · de commit-hook meldt dat voor deze worktree geen integratietak en uitrol zijn vastgelegd; er is niets
  gepusht of samengevoegd.
