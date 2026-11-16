# Koude review (doel) van beloningen-treden, ronde 1, opus

## Maatstaf en scope

Ik toets één ding: haalt de wijziging het doel van `docs/review-loop/goals/beloningen-treden.md`? Daarbij gelden de vaste
grenzen: geen itemverlies, geen vastloper, een opgeslagen run leest zijn tellers nog goed, en op Normal verandert niets.

Beoordeeld:
- newserv `git diff a7881eb HEAD` (HEAD 07f2d6f): `src/SpireRewards.hh`, `src/SpireRewards.cc`, `src/SpireFlagBridge.cc`,
  `system/spire-rewards.json`, `src/StaticTests.cc` en `.claude/review-loop.json`.
- quest-repo `git diff 595f26e0 HEAD` (HEAD 0f450e6f): `sim/curve_spire.py`, `quests/spire/make_spire_rewards.py`,
  `quests/spire/data/rewards-{hard,vhard,ultimate}.json`, `quests/spire/data/pool-order.json`.

Daarnaast gelezen, omdat de wijziging ze aanroept of erdoor wordt aangeroepen: `spire_reward_make_offer` en
`spire_reward_draw_offers` (SpireFlagBridge.cc), `spire_meta_ladder_quest_rung` (SpireMeta.cc), de leesgrens van de draw
(SpireDraw.hh/.cc), `check_pool_order.py`, de telregisters in `rewards.py` (`EXTRA_WAIT_MAX_ENTRIES`, `pick_inc`) en de
trede-poort in `check_ladder.py`.

## Per punt van het doel: gehaald of niet, met bewijs

1. **De drie trede-bestanden zijn opnieuw gemaakt met `sim/curve_spire.py rewards`.** Gehaald. Opnieuw gemaakt naar
   /tmp/review-loop en vergeleken: `hard identical`, `vhard identical`, `ultimate identical` (`cmp`). Ze lopen dus niet
   achter op de generator.
2. **Elke serverregel van Normal staat ook op de treden; het kit-frame heeft vier slots.** Gehaald. Per trede en archetype
   vergeleken met `data/rewards.json`: geen enkele Normal-regel met `srv` ontbreekt of heeft een ander soort. De enige
   uitzondering is F39 "Disk" op Ultimate (zie punt 5, zo bedoeld). Buiten de wapens die een trede ophoogt zijn alle
   regels gelijk aan Normal. Het kit-frame is op alle drie de treden en voor HU, RA en FO `01 01 00 00 00 04 ...`.
   In de gebouwde tabellen staan de regels ook echt als serverregel: `build-ladder/q981.draw.json` FO telt 38 regels,
   met Weapon 1, New line 2, Mag food 3, Unit 5 en 19 schijfregels met soort 4, onder meer index 32 Barta en 33 Rabarta.
3. **Een opgeslagen run leest zijn tellers nog goed.** Gehaald. Per pool vergeleken met de bestanden op 595f26e0: elke
   oude regel staat op dezelfde index met dezelfde sleutel (de bytes). Alleen de id's van de trede-schijven zijn
   hernummerd (F38/F39/F40 werden F40/F41/F42), en de telling loopt per index, niet per id
   (`check_pool_order.py` regel 9). Wat erbij komt, staat achteraan: Unit, en op Hard en Very Hard ook Disk. In
   `pool-order.json` veranderen alleen de drie trede-secties, telkens zeven regels: H12, H14, H28, R12, R14, R29 en F24.
   De sectie `rewards.json` en de `bron` zijn ongewijzigd. Uitvoer: `pool order: 4 file(s), 0 FAIL` en
   `selftest: 0 of 4 cases wrong`. Het hoogste telregister haalt index 37 (r186 bits 21-23), onder EXTRA_WAIT
   (bits 24-31).
4. **De schijven die een trede zelf toevoegt zijn serverregels, met naam en tekst van de gewone regel en zonder
   level.** Gehaald. Op Hard en Very Hard zijn F40 en F41 "Barta" en "Rabarta", op Ultimate komt F42 "Zonde" erbij.
   Ze hebben `srv: schijf`, de tekst van F04/F35/F06 en als tweede regel "Level: see the reward line". De bytes zijn
   ongewijzigd (Barta Lv8, Rabarta Lv3, Zonde Lv8), dus de server leest daaruit `line_tech`/`line_level`
   (SpireFlagBridge.cc 4473-4475).
5. **De lijst telt hoogstens 38 regels; op Ultimate valt bij de force "Disk" af.** Gehaald. In
   `rewards-ultimate.json` staat `rung.dropped_pool_full: {"FO": ["F39 Disk"]}`. De FO-pool is 38 lang en Unit blijft.
   `q983.draw.json` FO telt 38 regels, de laatste is Unit.
6. **Een trede geeft een sterker wapen.** Gehaald. `spire_reward_w` telt `rung_weapon_steps[rung & 3]` op bij de W van
   de kamer, met W6 als bovengrens (SpireRewards.cc 369-382). De enige aanroeper, `spire_reward_make_offer`, haalt de
   trede uit `l->quest->meta.quest_number` (SpireFlagBridge.cc 4449, 4480), uit dezelfde bron als het questnummer
   waarmee de draw-tabel geladen wordt (5234). De trede-kamerquests q1940+ krijgen hun trede via
   `spire_meta_ladder_quest_rung`. Ik heb de kit op elke trede naast het wapen gelegd dat de server in kamer 1 biedt.
   Voorbeelden: HUmar Hard Brand naar Buster, Very Hard Buster naar Pallasch, Ultimate Pallasch naar Gladius; RAmar
   Ultimate Railgun naar Raygun; FOmar Hard Cane naar Stick. Bij geen enkele klasse ligt het aanbod onder de kit.
   Het grid kent W2 tot en met W6 voor elke lijn, dus een gecapte W vindt altijd een cel.
7. **`RungWeaponSteps` in `system/spire-rewards.json`, gemaakt door `make_spire_rewards.py`.** Gehaald. Semantisch
   verschilt het gridbestand van a7881eb alleen in `RungWeaponSteps` (`[0, 1, 2, 3]`, uit `sim/ladder.json`
   weapon_steps). `make_spire_rewards.py --check` meldt OK (zie verificatie). De parser weigert een lijst van de
   verkeerde lengte of een waarde boven 4, en de statische test dekt die weigering.
8. **Op Normal verandert niets.** Gehaald. `data/rewards.json`, `rewards.py` en `build.sh` staan niet in de diff, en de
   pin van `rewards.json` is gelijk gebleven. Op de server is trede 0 gelijk aan `rung_weapon_steps[0] = 0`, en de oude
   W was al hoogstens 6, dus `min(W + 0, 6)` geeft dezelfde W. De statische test toetst Normal tegen de oude waarden
   (`spire_reward_w(rewards, 40) == 6`, room 1 W2).
9. **Geen itemverlies, geen vastloper.** Niets in de wijziging raakt de geef-route. Kan de server niets geven (geen grid,
   een fout bij het aanbod), dan krijgt de speler het eigen item van de regel, net als voorheen. De questbouw is groen,
   alle selftest-gates incluis (zie verificatie). Een meting op de Wii is er niet, en die hoort ook niet bij deze review.

## Bevindingen

Geen. Ik vond geen punt van het doel dat niet gehaald is, en niets wat de wijziging stukmaakt.

## Mutanten (alleen als je er draaide, letterlijk)

Geen gedraaid: er is niets gefixt en het doel vraagt niet om een test.

## Verificatie na de fixes (letterlijk)

Er waren geen fixes. Gedraaid op newserv 07f2d6f en quest 0f450e6f, met beide werkmappen schoon.

Server (netstroom: `cat /sys/class/power_supply/A*/online` gaf `1`):

```
[  1%] Built target newserv-Revision-cc
[  2%] Building CXX object CMakeFiles/newserv.dir/src/Revision.cc.o
[  3%] Linking CXX executable newserv
[100%] Built target newserv
make: Leaving directory '/home/m4rck/code/worktrees/newserv/beloningen-b/build'
I 1348190 2026-11-16 21:20:39 - [C-BB] Deleted
I 1348190 2026-11-16 21:20:39 - [Game:7A03] Deleted
I 1348190 2026-11-16 21:20:39 - -- All static tests passed
ok: partner floor 1, leader floor 2 -> skip
ok: partner floor 15, leader floor 14 -> warp
ok: spire-lives-check
```

Make meldt alleen Linking, en dat klopt: de objecten van SpireRewards.cc, SpireFlagBridge.cc en StaticTests.cc zijn
nieuwer dan hun bron (21:09-21:10 tegen 21:08), en `build/newserv help` meldt `newserv-07f2d6f`, zonder plus.

Quest-build `build.sh --ladder` (rc=0, 0 regels met error/fail in het log):

```
--- selftests: 36 ran green, every gate caught every break of its own input (16 s)
--- build: .bin + .dat 241202 of 245000 bytes decompressed (.bin 166416)
--- build-ladder/q981: .bin + .dat 234226 of 245000 bytes decompressed (.bin 159440)
--- build-ladder/q982: .bin + .dat 234270 of 245000 bytes decompressed (.bin 159484)
--- build-ladder/q983: .bin + .dat 233874 of 245000 bytes decompressed (.bin 159088)
--- build-ladder/q984: .bin + .dat 205340 of 245000 bytes decompressed (.bin 140080)
--- build-ladder/q985: .bin + .dat 205360 of 245000 bytes decompressed (.bin 140100)
--- build-ladder/q986: .bin + .dat 204928 of 245000 bytes decompressed (.bin 139668)
--- q981 labels used: engine 239 of 239, rewards 768 of 768, economy 392 of 512, score 90 of 256, coop 256 of 256, path 256 of 256, e08 648 of 768, switch 45 of 256, overflow 600 of 768
q981 hard     run 1: Lv20, V_Difficulty == 1 && SPIRE_RUN1 >= 1, k1 6 rung rows, kit FO 000A00 HU 000101 RA 000601, 11 shop items, 8 pool disks, 11 R12 room disks
q983 ultimate run 1: Lv80, V_Difficulty == 3 && SPIRE_RUN1 >= 3, k1 6 rung rows, kit FO 000A02 HU 000103 RA 000603, 11 shop items, 8 pool disks, 11 R12 room disks
--- ladder: 6 quests, level, condition, rung rows, kit, shop, disks, R12 room disks
b4ba838293633d4cd0ee22607f746bf7  build-ladder/q981-gc-e.bin
eda78172c2ec75736c8f50b27f63c58e  build-ladder/q982-gc-e.bin
fc46b36abbabb07d1768247911fc4e72  build-ladder/q983-gc-e.bin
6fab6f7d4eaf968e6adcfb6779a4e331  build-ladder/q984-gc-e.bin
c952d2a44919bc8ecf1a3c332eb8dd6c  build-ladder/q985-gc-e.bin
f14c45941b095bce4dc7f85340b65c13  build-ladder/q986-gc-e.bin
2c937252d26dfbe13fe63c8c0698dbe1  build-ladder/q981-gc.dat
2c937252d26dfbe13fe63c8c0698dbe1  build-ladder/q982-gc.dat
2c937252d26dfbe13fe63c8c0698dbe1  build-ladder/q983-gc.dat
06e1b9574bd3501b96ca04cb4acb740a  build-ladder/q984-gc.dat
06e1b9574bd3501b96ca04cb4acb740a  build-ladder/q985-gc.dat
06e1b9574bd3501b96ca04cb4acb740a  build-ladder/q986-gc.dat
```

Grid:

```
OK: /home/m4rck/code/worktrees/newserv/beloningen-b/system/spire-rewards.json matches the design grid
```

Losse controles op de trede-bestanden: `check_rewards.py` per trede gaf `- FAIL: 0` (met de oude WARN over de tags
ranged/hit/glass van FO), en `check_pool_order.py` staat hierboven bij punt 3.

## Wat ik niet heb aangeraakt

- Buiten scope volgens het doel: het spell-level op een trede of bij een gekocht startlevel (stap 2). Ook niet: wapens
  boven W6 (op Ultimate geeft elke kamer vanaf de derde hetzelfde wapenlevel), Disk voor de force op Ultimate, mana,
  de bouwscripts, en uitrol of herstart.
- De `bron` van `pool-order.json` zegt nog dat alle volgordes van één questcommit komen, terwijl de trede-secties nu met
  de hand bijgewerkt zijn. Dat is tekst; het raakt geen poort en geen speler.
- De doorgifte van de trede in `spire_reward_make_offer` heb ik op de coderegel getoetst, niet met een test. De
  statische test roept `spire_reward_offer` direct aan, met trede 3.
- De pc-build pint newserv nog op a7881eb (quest-commit 5a532bad). Voor de uitrol moet die pin naar de commit van deze
  wijziging; dat valt onder de uitrol, niet onder deze review.
- Geen geheugenkaart, geen pc, geen lab, en niets gepusht.

## Stopadvies

Stoppen. Elk punt van het doel is gehaald, met bewijs uit de bestanden, de gebouwde draw-tabellen en de drie
verificaties. Ik heb geen bevinding en dus geen fix; een volgende ronde vanuit de invalshoek doel levert naar
verwachting niets op.
