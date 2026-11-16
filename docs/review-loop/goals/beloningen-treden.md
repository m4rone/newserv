---
slug: beloningen-treden
base: a7881eb
---

## Doel
Marck, de speler en eigenaar (16 nov 2026, 20:50): "bedenk dat force spells 30 levels hebben. op very hard of als ik
een hoger start level heb dan meot ik hoger starten."

De run heeft vier treden: Normal, Hard, Very Hard en Ultimate (de laatste drie zijn de questnummers q981 tot q986 en
de kamerquests plus 1000 per trede). Op Normal draait het nieuwe beloningssysteem: een regel in de lijst vraagt de
server om het item (een wapen uit je lijn, een nieuwe lijn, mag-voer, een unit, een spell). Op Hard, Very Hard en
Ultimate draaide nog het oude systeem: de drie bestanden `quests/spire/data/rewards-hard.json`, `-vhard.json` en
`-ultimate.json` (quest-repo) waren niet opnieuw gemaakt sinds de serverregels er zijn. Een force kreeg daar dus geen
enkele spell van de server, niemand een wapen uit zijn lijn, en het kit-frame had er geen slots. Dit is stap 1 van de
overdracht `new-features/roguelike/handovers/2026-11-16-force-spells.md` (quest-repo); de beslissing staat in
`new-features/roguelike/decisions.md` als B-E02-57.

Wat de wijziging doet:

- **De drie trede-bestanden zijn opnieuw gemaakt** met `sim/curve_spire.py rewards`. Elke serverregel van Normal
  staat nu ook op de treden, en het kit-frame heeft er vier slots.
- **Een opgeslagen run leest zijn tellers nog goed.** Elke regel die er al stond houdt zijn plaats in de lijst; de
  nieuwe regels (Unit, Disk) staan achteraan. In de vaste volgorde (`quests/spire/data/pool-order.json`) zijn per
  trede alleen de regels bijgewerkt die Normal op hun plek veranderde: de frames met vier slots en de regel die
  Mag food werd.
- **De schijven die een trede zelf toevoegt** (Barta Lv8, Rabarta Lv3, op Ultimate ook Zonde Lv8) zijn nu ook een
  regel van de server. Ze nemen naam en tekst van de gewone regel van hun techniek en beloven geen level meer.
- **Een trede geeft een sterker wapen dan Normal.** De server telt bij het wapenlevel van de kamer de stappen van de
  trede op (Hard 1, Very Hard 2, Ultimate 3, uit `sim/ladder.json`), tot het hoogste level van het grid. Een trede
  biedt zo nooit een wapen onder zijn eigen kit. De stappen staan in `system/spire-rewards.json` als
  `RungWeaponSteps`, gemaakt door `quests/spire/make_spire_rewards.py`.
- **De lijst van een klasse telt hoogstens 38 regels** (de tellers van de quest). De force heeft er op Ultimate 39;
  daar valt de laatste nieuwe regel ("Disk") af.

Vaste grenzen van het project die hier gelden: geen itemverlies op een karakter, geen vastloper, een opgeslagen run
leest zijn tellers nog goed, en op Normal verandert niets.

De wijziging zit in twee repos, beide op tak `feature/beloningen-b`:
- newserv (deze repo), base a7881eb: `src/SpireRewards.hh`, `src/SpireRewards.cc`, `src/SpireFlagBridge.cc`,
  `system/spire-rewards.json` en de tests in `src/StaticTests.cc`.
- de quest-repo (`/home/m4rck/code/worktrees/pso-roguelike/beloningen-b`), base 595f26e0: `sim/curve_spire.py`,
  `quests/spire/make_spire_rewards.py`, de drie trede-bestanden en `pool-order.json` in `quests/spire/data/`.

## Buiten scope
- Het level van een spell op een trede en bij een gekocht startlevel (stap 2: de server geeft op een trede nog het
  level van Normal, met het eigen level van de regel als ondergrens).
- Mana als beloning, het verschil tussen de vier forces, en de beloningen van hunters, rangers en androids.
- Wapens boven het hoogste level van het grid: op Ultimate geeft elke kamer vanaf de derde hetzelfde wapenlevel.
- De regel "Disk" voor een force op Ultimate (de lijst is daar vol).
- Hoe een trede zijn vijanden, levels en winkel doorrekent (`sim/plan-spire-*.json`, de economy-bestanden).
- Uitrol naar de pc of het lab, en herstarten van een newserv.
- De bouwscripts van de quest-repo (`build.sh`, `pc-build.sh`) en de poorten die deze wijziging niet raakt.
