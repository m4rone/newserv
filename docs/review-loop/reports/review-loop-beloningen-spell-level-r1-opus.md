# Koude review r1 (opus, invalshoek doel): het level van een spell als beloning

## Maatstaf en scope

- Maatstaf: het doel in `docs/review-loop/goals/beloningen-spell-level.md` (Marck, 16 nov 20:50: "op very hard of als ik
  een hoger start level heb dan moet ik hoger starten"), met als ontwerp `force-spells.md` §2 en §6 stuk 1 op tak
  `docs/ontwerp-mechanics` (d0d31ada).
- Beoordeeld: newserv `git diff 3f26e4c a0e2239` (`src/SpireRewards.hh`, `src/SpireRewards.cc`, `src/SpireFlagBridge.cc`,
  `system/spire-rewards.json`, `src/StaticTests.cc`, `.claude/review-loop.json`) en de quest-repo
  `git diff b943a114 61e72cd5` (`quests/spire/make_spire_rewards.py`, plus `decisions.md` en `observaties.md`).
- Gelezen buiten de diff, omdat de wijziging erop leunt: `SpireMag.cc` (staprij, StartFloors, `spire_mag_rule_b`,
  `spire_run_mag_decide`), `system/spire-mag.json` (FO), de pick van een schijf (`SpireFlagBridge.cc` 4720-4762, regel B),
  `Client.cc` `create_battle_overlay` (de kopie op het level van de trede), `Items.cc` (een gebruikte schijf), de
  volgorde baas-examen en beloningstrekking in `make_spire.py`, en hoe `spire-rewards.json` naar de pc gaat
  (`pc-build.sh` 181-184, `apply-on-pc.sh` 249).

## Per punt van het doel: gehaald of niet, met bewijs

1. **Het level volgt de MST van de kopie, twee MST per MIND-level van de mag.** Gehaald.
   `spire_reward_disk_budget` (`SpireRewards.hh:249-252`) is `mst + 2 * min(max(mind_now, mind_plan + tilt), mind_room)`,
   precies `budget(t)` en `mind_ok(t)` van het ontwerp. `spire_reward_disk_level` (`SpireRewards.cc:447-452`) zoekt het
  hoogste level waarvan `Req` binnen `min(budget, mst_cap)` valt, met `class_max` als bovengrens.
   - De bridge vult `mst` (kopie plus gedragen units, zonder mag, zoals vóór de wijziging), `mind_now` (MIND van de
     run-mag in de kopie) en `mind_plan` (`SpireFlagBridge.cc:4489-4497`).
   - `mind_plan` (`SpireFlagBridge.cc:4437-4455`) neemt de stap die de ledger verwacht (`start_step` + verslagen bazen),
     in de kolom van de lean, samen met de StartFloor van het gekochte startlevel (`spire_mag_step_bytes` neemt per stat
     het hoogste van staprij en floor, `SpireMag.cc:247-262`). De data geeft precies de mag-kolom van het ontwerp:
     `Steps [0, 18, 30, 42, 46, ...]`, `floors [(5, 4), (14, 7)]` (uit `system/spire-mag.json`).
   - De verslagen baas staat in de ledger vóór de volgende trekking: een baaskamer heeft geen pauze A en geen trekking.
     Het examen (`exam_reward`, `make_spire.py:3136-3137`) zet de kamer in `ledger.boss_rooms`
     (`SpireMag.cc:390-392`), en pas de kamer erna trekt.
2. **Op een trede en met een gekocht startlevel begint een force vanzelf hoger, zonder eigen stap.** Gehaald. Er is
   geen term per trede in het level. De kopie wordt op het level van de battle rules gebouwd (`Client.cc:707-711`), en
   `rung` raakt alleen de kolom van de aanwezigheid (`SpireRewards.cc:542`). Beide tabellen van het ontwerp staan als
   test in `StaticTests.cc`: 2 klassen x 15 kamers x 4 treden x 4 niveaus = 480 cellen (`expect_eq(480u, cells)`).
   De MST komt daarbij uit `level-table-v3.json`. Een script vergeleek de testtabellen met de markdown-tabellen van
   `force-spells.md` §2: `15 15 True True` (beide tabellen, elke cel gelijk). Ook `room_level` en `room_mind` in de test
   zijn gelijk aan de kolommen level en mag van het ontwerp.
3. **Wat bleef, bleef.** Gehaald (`SpireRewards.cc:453-466`):
   - minstens bekend + 2, en één level als dat het hoogste van de klasse is;
   - nooit boven `class_max`;
   - `extra_levels` van een vloek erbovenop;
   - daarna omlaag tot `Req <= mst_cap` en `disk_mind <= mind_room`, en 0 als het level niet boven bekend uitkomt.

   Bij de pick tilt regel B de mag tot die `disk_mind` (`SpireFlagBridge.cc:4733-4735`). Die lus en die pick zijn niet
   veranderd, dus een gegeven schijf kan de speler leren zoals voorheen. Tests: `spire_reward_disk_level`-blok
   (`StaticTests.cc` 8070-8100) en de end-to-end pick (`StaticTests.cc` 11788-11807).
4. **Het eigen level van een regel is geen ondergrens meer, de eigen techniek blijft.** Gehaald.
   - `line_level` is weg uit de state en uit de bridge.
   - De techniek van de regel gaat zonder aanwezigheidstoets naar `level_of` (`SpireRewards.cc:550-555`), en pas als
     die 0 geeft valt de regel terug op elke aanwezige techniek.
   - Test: Grants Lv9 in kamer 1, waar Sega geen Normal-Grants laat vallen (`StaticTests.cc` 8286-8290).
5. **De tabel per kamer zegt alleen welke technieken mogen komen; op een trede de kolom van die moeilijkheid.**
   Gehaald. `r_it->second[t][column] ? level_of(t) : 0` met `column = min(tier + RungDiskColumns[rung], 3)`
   (`SpireRewards.cc:542`, `558`), letterlijk de formule `aanwezig` van het ontwerp. De test telt per trede de
   kandidaten tegen de kolom (`StaticTests.cc` 8313-8335).
6. **`Techs[].Req`, `TierDiskMind` en `RungDiskColumns` komen uit `make_spire_rewards.py`.** Gehaald.
   - `tech_req` neemt per level het maximum van image en BB.
   - De zeven BB-afwijkingen in `TECH_MST_BB` zijn precies de zeven van `force-spells.md` §4: Barta en Zonde
     omgewisseld, de stap van Gibarta en Gizonde omgewisseld, Rabarta en Razonde omgewisseld, de stap van Deband 28.
   - De parse eist 30 waarden van 1-5000, nooit dalend (`SpireRewards.cc:293-304`), en controleert de twee lijsten
     van vier zoals `RungWeaponSteps`.
   - Tests: Barta Lv1 44 (BB), Lv30 760 (image).
   - `--check` meldt dat het grid klopt (zie verificatie). Het bestand gaat met de newserv-commit naar de pc
     (`pc-build.sh:181-184`), dus de nieuwe binary leest nooit een oud grid zonder `Req`.
7. **Vaste grenzen.** Geen itemverlies en geen vastloper geraakt. De wijziging verandert alleen welk level en welke
   techniek de server kiest. De terugval op het eigen item bij een fout (`*why`, `return std::nullopt`) en de weigering
   boven `class_max` bij de pick (`SpireFlagBridge.cc:4729-4731`) zijn ongewijzigd.

## Bevindingen

Geen. Ik vond geen punt van het doel dat niet gehaald is en niets wat de wijziging stukmaakt. Er is dus ook geen
fix-commit.

Twee dingen die ik zag, maar die geen bevinding tegen het doel zijn:

- **Geen test op de bedrading van `mind_plan` in de bridge.** De end-to-end test draait `spire_reward_mind_plan` wel,
  maar op stap 0, waar het plan 0 is (`StaticTests.cc:11764-11768`). Een fout in die functie zou geen test rood maken;
  alleen de logregel "mag MIND … planned" zou hem tonen. De code klopt bij lezing en de data geeft de tabel van het
  ontwerp (punt 1). Ik heb hem dus niet gebouwd.
- **`mind_now` telt de run-mag in de rugzak, ook als hij niet gedragen wordt** (`spire_reward_run_mag` kijkt niet naar
  `equipped`). De header zegt "the copy wears". Dit maakt geen schijf onbruikbaar die het vroeger wel was: elke schijf
  die MIND vraagt, vraagt al dat de mag gedragen wordt. Een afgelegde, gevoerde mag kan het level alleen hoger
  zetten, tot zijn eigen MIND en binnen `mind_room`.

## Mutanten (alleen als je er draaide, letterlijk)

Geen gedraaid: er was geen eigen fix om te bewijzen, en het doel vraagt geen nieuwe test.

## Verificatie na de fixes (letterlijk)

Er zijn geen fixes, dus dit is de stand a0e2239 (newserv) en 61e72cd5 (quest-repo). Netstroom: `1`.

```
$ cd /home/m4rck/code/worktrees/newserv/beloningen-b && nice -n 10 make -C build -j16 newserv 2>&1 | tail -5 && build/newserv run-static-tests 2>&1 | tail -3 && sh tests/spire-follow-city.test.sh build/newserv 2>&1 | tail -3
[  1%] Built target newserv-Revision-cc
[  2%] Building CXX object CMakeFiles/newserv.dir/src/Revision.cc.o
[  3%] Linking CXX executable newserv
[100%] Built target newserv
make: Leaving directory '/home/m4rck/code/worktrees/newserv/beloningen-b/build'
I 1391102 2026-11-16 21:45:18 - [C-BB] Deleted
I 1391102 2026-11-16 21:45:18 - [Game:7A03] Deleted
I 1391102 2026-11-16 21:45:18 - -- All static tests passed
ok: partner floor 1, leader floor 2 -> skip
ok: partner floor 15, leader floor 14 -> warp
ok: spire-lives-check
```

Alleen `Revision.cc` werd opnieuw gebouwd. Ik wijzigde geen bronbestand, en de build van a0e2239 stond er al.

```
$ cd /home/m4rck/code/worktrees/pso-roguelike/beloningen-b && NEWSERV_DIR=/home/m4rck/code/worktrees/newserv/beloningen-b python3 quests/spire/make_spire_rewards.py --check
OK: /home/m4rck/code/worktrees/newserv/beloningen-b/system/spire-rewards.json matches the design grid
```

## Wat ik niet heb aangeraakt

- Alles buiten scope van het doel: de startspells van de kit, mana, hervatten, de keuze per type force, "Disk" op
  Ultimate, de constanten zelf, de sim van de treden, de questbouw, uitrol en herstart.
- Gedrag van de client dat de wijziging vaker raakt, maar niet invoert. De run draait met `tech_disk_mode 2` en max 0
  (`quests/spire/spire.json:16`, `:28`). Of de GC-client een schijf boven dat maximum leert, is niet gemeten. Vroeger
  gaf de server ook al schijven boven Lv1, en volgens het ontwerp (§5) bewijst alleen Marcks run dat.
- Geen geheugenkaart, geen pc, geen lab en geen draaiende newserv. Er is niets gepusht.

## Stopadvies

Stoppen. Elk punt van het doel is gehaald, met de tabellen van het ontwerp als test (480 cellen, gelijk aan het
ontwerp). Server en grid zijn groen, en deze ronde fixte niets wat een volgende ronde zou moeten nalopen.
