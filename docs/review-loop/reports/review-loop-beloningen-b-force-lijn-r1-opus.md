# Review-loop beloningen-b-force-lijn, ronde 1 (opus, invalshoek doel)

## Maatstaf en scope

Maatstaf is het doel in `docs/review-loop/goals/beloningen-b-force-lijn.md`. Ik heb `git diff eec3326 HEAD` gelezen
(958f399, 1f0c8fc, 8125261). Daarnaast heb ik de code gelezen die de wijziging aanroept of die haar aanroept:

- `spire_reward_offer` en `spire_reward_line_of` in `src/SpireRewards.cc`;
- `spire_reward_need_with_disk` in `src/SpireRewards.hh`;
- `spire_mag_fit_200` en `spire_mag_rule_b` in `src/SpireMag.cc`;
- in `src/SpireFlagBridge.cc`: de trekking van een schijf (`mind_room`, :4463), de pick (eis van wapen en schijf,
  :4661-4679) en de RESULT (ledger `Need` en `DiskMind`, :4934-4938), plus het laden van de ledger (:990-997);
- de nieuwe statische tests en de e2e-test van de schijf (StaticTests.cc :11440-11585);
- het grid `system/spire-rewards.json`, en in de quest-repo B-E02-33 en de twee nieuwe regels in
  `design/beloningen-per-klasse.md`.

Bestaande code die al werkte en die de wijziging niet verandert, heb ik niet opnieuw getoetst.

## Per punt van het doel: gehaald of niet, met bewijs

1. **Nieuwe lijn geeft een force alleen een MST-lijn.** Gehaald.
   - De code: `own_line` (SpireRewards.cc:457-463) laat voor archetype FO alleen een lijn toe waarvan het kitwapen MST
     vraagt. NEW_LINE filtert daar op (:537).
   - Het grid, nagelopen met een python-telling over `system/spire-rewards.json`: bij alle vier de forces vragen de
     kitwapens van Cane, Rod en Wand MST 50, 80 en 114. Elk wapen van die drie lijnen (alle W, alle tiers) vraagt MST.
     Geen enkel wapen van Saber, Slicer, Handgun of Mechgun vraagt MST. Bij hunters en rangers vraagt geen lijn MST.
   - De test: `seen == {Cane, Rod, Wand}` voor elke force, met en zonder een saber in de hand.
2. **Zonder wapen van een bekende lijn volgt een force Cane.** Gehaald.
   - `line_order` staat Saber vooraan en daarna alfabetisch (SpireRewards.cc:232), dus de eerste MST-lijn is Cane.
   - De test `bare.line == "Cane"` geldt voor FO, de oude test bij :8222 is aangepast.
   - Voor hunters en rangers geeft `own_line` altijd true. Daardoor blijven de terugval (Saber) en de lijst van andere
     lijnen gelijk; de test telt dat elke andere lijn nog aangeboden wordt.
3. **Wie zelf een vechtwapen draagt, houdt die lijn.** Gehaald.
   - `spire_reward_line_of` is niet gewijzigd. De test: een saber in de hand geeft bij elke klasse, ook bij een force,
     een wapenbeloning op de Saber-lijn.
   - Nieuwe lijn geeft zo'n force wel een MST-lijn.
4. **Het beloningswapen is te dragen, ook na een schijf.** Gehaald.
   - `spire_reward_need_with_disk` laat POW en DEX van het wapen staan. De MIND van de schijven krijgt hoogstens
     `200 - POW - DEX`, de eigen MIND van het wapen blijft staan.
   - In `spire_mag_fit_200` levert stap 4 de MIND die de mag boven de eis had in, voordat stap 5 een gevraagde stat
     onder zijn eis brengt. De nieuwe test (POW 100 naar 160, MIND 50, eis {160, 0, 40}) geeft POW 16000 en MIND 4000
     op level 200. Met alleen de oude stappen zou stap 5 POW eerst korten, tot 150, en dan faalt de test (uitgerekend, niet gedraaid).
   - De trekking rekent met dezelfde ruimte: `mind_room = 200 - (need.pow + need.dex)` (SpireFlagBridge.cc:4463-4464).
     Een nieuwe schijf past dus. Bij de pick van een schijf gaat `max(DiskMind, nieuwe schijf)` door dezelfde grens,
     zodat ook een schijf van eerder in de run het wapen niet meer onder zijn eis duwt.
5. **Voor hunters, rangers en een force op een MST-lijn geeft regel B dezelfde mag.** Gehaald.
   - Geen van de 115 wapens in het grid vraagt meer dan één stat (python-telling). Een hunter of ranger zonder schijf
     en een force op een MST-lijn (alleen MIND) hebben dus maar één gevraagde stat.
   - Op één stat geven stap 4 (naar de eis) en daarna stap 5 (naar min(nu, eis)) samen hetzelfde als de oude stap 4
     (direct naar min(nu, eis)): `cut_to` kort alleen zolang het level boven 200 is.
   - In `need_with_disk` is de ruimte 200 bij POW en DEX 0, en zonder schijf (DiskMind 0) is de uitkomst die van het
     wapen. Dus ook daar verandert niets.
   - Kanttekening: dit klopt omdat elk wapen één stat vraagt. Zou het grid ooit een wapen krijgen dat POW en DEX
     vraagt, dan verandert stap 4 ook de mag van een hunter. Dat gebeurt alleen waar de oude snede het wapen onder
     zijn eis liet: ik heb uitgeschreven dat bij een draagbare oude uitkomst de nieuwe gelijk is.
6. **Vaste grenzen.** Gehaald.
   - Geen itemstroom gewijzigd.
   - Geen nieuwe throw: `own_line` gebruikt `find`, en `lines.at` krijgt alleen sleutels uit `line_order`. Elke lijn van
     elke klasse heeft W2-W6 (nagelopen).
   - Het formaat van de savestate is gelijk (`Need`, `DiskMind`).
   - De schijven zijn nog niet uitgerold (B-E02-32), dus geen opgeslagen run draagt een oude eis boven 200.

## Bevindingen

**B1 — drie commentaren beschreven nog de oude regel** (minor, raakt punt 4).

Bewijs:

- `src/SpireMag.hh:168-170`: "kept apart so a later reward weapon, which replaces need, does not drop it".
- `src/SpireFlagBridge.cc:4661-4662`: "per stat the highest of what the weapon asks and what the run's disks ask ...
  a later weapon keeps the disks' MIND".
- `src/SpireFlagBridge.cc:4935`: "the run's disks keep their MIND past a later weapon".

Sinds 1f0c8fc is dat onwaar. Wie deze tekst volgt, zet de fout terug die het doel oplost.

Fix in a8e811a: de drie commentaren zeggen nu dat POW en DEX van het wapen blijven staan en dat de MIND van de schijven
krijgt wat er van 200 over is. Er is geen gedrag gewijzigd.

Geen andere bevindingen.

Twee observaties, geen bevinding:

- **Een schijf die nog niet geleerd is, verliest zijn MIND bij een zwaar beloningswapen.** Dat is de keuze van B-E02-33
  ("de schijven zijn dan al geleerd").
  - Normaal leert de speler de schijf eerst: hij kiest één beloning per kamer (ontwerp par. 1), en de mag voor de schijf
    komt in pauze B van dezelfde kamer.
  - Bewaart hij de schijf in zijn pak en pakt hij in de volgende kamer een zwaar vechtwapen, dan kan hij de schijf pas
    leren als de eis van het wapen weer daalt.
- **Cosmetisch, in het journaal.** De journaalregel van een pick schrijft "held Cane" bij een force die geen wapen van
  een bekende lijn draagt (`from_line` is dan Cane). Vroeger stond daar Saber, dat hij ook niet droeg.

## Mutanten (alleen als je er draaide, letterlijk)

Geen. Mijn fix raakt alleen commentaar, en geen test kan een commentaar vangen.

## Verificatie na de fixes (letterlijk)

Alle drie op stand a8e811a.

Server op de pc (rsync, `make`, statische tests, volgtest), exit 0:

```
[ 36%] Building CXX object CMakeFiles/newserv.dir/src/SpireRewards.cc.o
[ 37%] Building CXX object CMakeFiles/newserv.dir/src/StaticTests.cc.o
[ 38%] Linking CXX executable newserv
[100%] Built target newserv
make: Leaving directory '/home/m4rck/code/newserv-review-b/build'
I 859260 2026-11-16 19:10:42 - [C-BB] Deleted
I 859260 2026-11-16 19:10:42 - [Game:7A03] Deleted
I 859260 2026-11-16 19:10:42 - -- All static tests passed
ok: partner floor 1, leader floor 2 -> skip
ok: partner floor 15, leader floor 14 -> warp
ok: spire-lives-check
exit=0
```

Questbouw op de laptop, exit 0. De slotregels:

```
--- q920 labels used: engine 239 of 239, rewards 768 of 768, economy 486 of 512, score 90 of 256, coop 256 of 256, path 256 of 256, e08 716 of 768, switch 45 of 256, overflow 596 of 768
--- build: .bin + .dat 242922 of 245000 bytes decompressed (.bin 168136)
--- q920: .bin 168136 of 178176 bytes decompressed, heat patch 359 fields, H 0..6 -> build/q920.heat.json
--- selftests: 35 gates caught every break of their own input (91 s)
```

Een grep op fail, error en warn in het hele log (210 regels) gaf geen treffer, op de regel "OK: no end clock" na.

Grid, exit 0:

```
OK: /home/m4rck/code/worktrees/newserv/beloningen-b/system/spire-rewards.json matches the design grid
```

## Wat ik niet heb aangeraakt

- **Op de pc:** de dienst newserv, zijn boom `~/pso-tools/newserv` en elk ander pad behalve `~/code/newserv-review-b`.
  Niets uitgerold of herstart.
- **Op de laptop:** de draaiende newserv.
- **De quest-repo:** geen wijziging. De tekst van B-E02-33 en het ontwerp klopt met de code.
- **Verder:** geen geheugenkaart, geen push, geen dependency of lockfile. Eerdere review-verslagen heb ik niet gelezen.

## Stopadvies

Stoppen. Elk punt van het doel is gehaald, met code, test en data als bewijs. De enige bevinding was commentaar en is
gefixt. Een tweede ronde op dit doel zou hetzelfde opnieuw bewijzen.
