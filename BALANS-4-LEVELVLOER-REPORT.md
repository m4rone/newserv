# Balans 4: levelvloer voor de achterblijver (B-REGIE-20-5)

Worktree `/home/m4rck/code/worktrees/newserv/balans-1`, tak `spike/balans`, gebouwd op 9e40c01.
Codecommit: e91dba414ea3ed5c85927beba929d21d5c865ef5 (`feat: level floor for the trailing player at quest load`).

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

Regelnummers van vóór de wijziging (9e40c01) en van erna (e91dba4). Samen 34 regels erbij en 4 weg; dat is boven de
richtwaarde van 25. Reden: de geldigheidscontrole en het level van de leider hebben de bestandslokale functies
`spire_start_level_bonus_for_client` en `spire_resume_level_for_client` nodig, dus de lijm kan niet in SpireMeta staan.

`src/SpireFlagBridge.cc`:
- na oud 4654 (nieuw 4655-4678): nieuwe statische functie `spire_level_floor_for_client`, 24 regels, pure toevoeging
  tussen `spire_resume_level_for_client` en `spire_start_level_quest_bin`.
- na oud 4663 (nieuw 4688), hervat-tak: één regel erbij,
  `level = spire_level_floor_for_client(c, l, resume_level, 0).value_or(level);`
- oud 4683 (nieuw 4708-4709): `if (!bonus || !bin_contents)` werd `auto floor_level = ...` plus
  `if ((!bonus && !floor_level) || !bin_contents)`.
- oud 4689 (nieuw 4715): `uint32_t level = floor_level.value_or(spire_start_level_char_level(base, bonus));`
- oud 4698 (nieuw 4724-4726): de `spire_start_level_sent`-toewijzing; bij een vloer
  `SpireStartLevelSent{lobby_id, quest_number, 0, level, bonus}`, anders ongewijzigd.
- oud 4700 (nieuw 4728): laatste argument van de logregel, `floor_level ? bonus : (level - base)`.

`src/SpireFlagBridge.hh`:
- na oud 1366 (nieuw 1367-1368): twee commentaarregels boven de declaratie van `spire_start_level_quest_bin`.

`spire_create_battle_overlay` is niet aangeraakt.

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
`spire_start_level_bonus_for_client`. Die functie is niet gewijzigd en de leider krijgt nooit een vloer. Voor een
achterblijver met een vloer blijft de bonus van zijn run het gekochte aantal (in de uitvoer hierboven "+0"), dus zijn
eigen startantwoord, de startvloeren van de run-mag en het bewaarde `StartBonus` rekenen niet met de vloerlevels. Dit
is gelezen en in het padblok zichtbaar in de logregel, maar er is geen aparte heat-test voor geschreven.

## Keuzes (met het alternatief)

1. **Wie de leider is:** de leider van de game (`l->leader_id`), zoals de heat. Alternatief: de leider die het
   hervatten gebruikt (`SpireFollowLeader`, anders client 0). Die twee kunnen verschillen; gekozen voor de aanwijzing
   in de opdracht.
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
  Het is geen questlading en staat niet in de drie momenten · `src/SpireFlagBridge.cc:4851-4852` · een aanroep in de
  overlay-functie van het andere spoor.
- codekwaliteit · de twee misleidende logregels uit keuze 2 · `src/SpireFlagBridge.cc:4867` en `:4889` · een eigen
  veld of een eigen logtekst bij het samenvoegen.
- codekwaliteit · de voorwaarde "bewaakte Spire-quest met runkopie voor een GC-client" staat nu drie keer uitgeschreven
  (bonus, hervatlevel, vloer) · `src/SpireFlagBridge.cc:4587`, `:4621`, `:4662` · één gedeelde hulpfunctie.
- datakwaliteit, bestond al · het startantwoord en de heat van de leider vergelijken de bonus in levels met het aantal
  gekochte treden (`run_bonus < spire_shop_level(upgrades, 0)`); sinds de bonus per trede meer dan 1 is kloppen die
  eenheden niet. Het heeft nu geen effect omdat startlevels 0 aan de sterkte bijdragen · `src/SpireFlagBridge.cc:3303`
  en `:4764` · uitzoeken en één regel per plek.
- werkwijze · de commit-hook meldt dat voor deze worktree geen integratietak en uitrol zijn vastgelegd; er is niets
  gepusht of samengevoegd.
