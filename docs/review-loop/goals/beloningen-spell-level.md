---
slug: beloningen-spell-level
base: 3f26e4c
---

## Doel
Marck, de speler en eigenaar (16 nov 2026, 20:50): "bedenk dat force spells 30 levels hebben. op very hard of als ik
een hoger start level heb dan meot ik hoger starten."

Een spell als beloning (de regels van de force die een schijf geven, en de regel "Disk") kreeg zijn level uit een
tabel per kamer en niveau van de beloning. Die tabel kent de moeilijkheid van de run niet en het gekochte startlevel
niet: op Very Hard kreeg een force in de eerste kamer dezelfde Foie als op Normal. Het ontwerp voor de nieuwe regel is
`new-features/roguelike/design/mechanics/force-spells.md` §2 en §6 (stuk 1) op tak `docs/ontwerp-mechanics` van de
quest-repo (werkplek `/home/m4rck/code/worktrees/pso-roguelike/ontwerp-mechanics`); de beslissing staat in
`new-features/roguelike/decisions.md` van de quest-repo als B-E02-60.

Wat de wijziging doet:

- **Het level volgt de geestkracht (MST) van de run-kopie.** De server neemt de MST van de kopie met de gedragen
  units, telt er twee MST per MIND-level van de mag bij, en geeft het hoogste level waarvan de eis daarbinnen valt.
  Voor de mag telt wat hij nu draagt, of wat hij op dit punt van de run volgens zijn tabel hoort te hebben plus een
  toeslag per niveau van de beloning (Normal 0, Rare 15, Epic 40, Legendary 80 MIND-levels) als dat meer is; nooit
  meer dan het wapen van de run in de mag overlaat.
- **Op een hogere trede en met een gekocht startlevel begint een force vanzelf hoger**, omdat de kopie daar op een
  hoger level is gebouwd en dus meer MST heeft. Er komt geen aparte stap per moeilijkheid bij. De twee volledige
  tabellen van het ontwerp (Foie voor een FOnewearl en een FOmar, per kamer, trede en niveau) zijn de maat.
- **Wat bleef:** minstens twee levels boven wat de kopie al kent (één als dat het hoogste van de klasse is), nooit
  boven het hoogste van de klasse, de twee extra levels van een vloek, en daarna omlaag tot de kopie de eis met zijn
  mag kan halen. Een schijf die de speler krijgt kan hij dus leren.
- **Het eigen level van een regel is geen ondergrens meer.** Een regel met een eigen techniek ("Foie", "Shifta")
  houdt die techniek zolang de kopie erin kan groeien, ook in een kamer waar Sega die techniek niet laat vallen.
- **De tabel per kamer zegt alleen nog welke technieken er mogen komen** voor een regel zonder eigen techniek. Op een
  trede leest de server daarvoor de kolom van Sega's eigen schijven van die moeilijkheid.
- **De MST-eis per level** staat per techniek in `system/spire-rewards.json` (`Techs[].Req`, 30 waarden): de
  strengste van het client-image en Blue Burst, omdat niet gemeten is welke de Wii houdt. Samen met `TierDiskMind` en
  `RungDiskColumns` komt hij uit `quests/spire/make_spire_rewards.py`.

Vaste grenzen van het project die hier gelden: geen itemverlies op een karakter, geen vastloper, en een beloning kun
je gebruiken op het moment dat je hem krijgt.

De wijziging zit in twee repos, beide op tak `feature/beloningen-b`:
- newserv (deze repo), base 3f26e4c: `src/SpireRewards.hh`, `src/SpireRewards.cc`, `src/SpireFlagBridge.cc`,
  `system/spire-rewards.json` en de tests in `src/StaticTests.cc`.
- de quest-repo (`/home/m4rck/code/worktrees/pso-roguelike/beloningen-b`), base b943a114:
  `quests/spire/make_spire_rewards.py`. De quest zelf verandert niet.

## Buiten scope
- De startspells van de kit op het startlevel, mana als beloning, en de geleerde spells bij hervatten (stuk 2 tot 4
  van het ontwerp).
- Welke spell een force van welk type vaker krijgt (`force-types.md`), en de regel "Disk" op Ultimate.
- De keuze van de constanten zelf (de toeslag per niveau, de strengste van twee eisen): die zijn van het ontwerp.
- Hoe zwaar de vijanden op een trede zijn nu een force daar hogere spells krijgt (`sim/plan-spire-*.json`).
- Het wapen, de unit en het mag-voer als beloning, en regel B van de mag.
- Uitrol naar de pc of het lab, en herstarten van een newserv.
- De bouwscripts van de quest-repo (`build.sh`, `pc-build.sh`) en de questbouw.
