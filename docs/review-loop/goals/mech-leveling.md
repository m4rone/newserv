---
slug: mech-leveling
base: 92869f9
---

## Doel
Marck, de speler en eigenaar (16 nov 2026, 20:50-20:56): "eigenlijk op elke mechanic moet een agent goed over
nadenken. de leveling, de mags aanval verdediging is echt te beperkt".

Zijn beloningen beginnen op het level waarop hij echt staat. Heeft hij een startlevel gekocht, dan biedt de eerste
kamer in het bos al een wapen van de grotten of de mijnen in plaats van een bosbeloning. Hoe zeldzaam een beloning is,
blijft zoals nu. Ontwerp: `new-features/roguelike/design/mechanics/leveling.md` § 2 ("Waar dit ontwerp afwijkt" punt 2,
en de ruggengraat per kamer) en § 7.

Wat de wijziging doet (haak H2: bij elk aanbod de kamer en de trede waarop het aanbod rekent):
- Bij elk aanbod rekent de server `L_sleutel = L_nu − R`. L_nu is het level van de run-kopie op dat moment, gezet door
  de level-up-melding 6x30; of die bij elke level-up komt, ook bij meerdere in één kill, is niet gemeten. R is het
  level van de moeilijkheid: Normal 0, Hard 19, Very Hard 39, Ultimate 79.
- `k_eff = max(k, de laatste kamer j met N(j) ≤ L_sleutel)`, met k de kamer van de quest en N(j) het level dat een
  speler op de gewone curve heeft als de beloning van kamer j wordt getrokken, dus aan het eind van kamer j op Normal,
  uit `spire-leveling.json`: 4, 6, 8, 10, 11, 12, 13, 15, 16, 17, 19, 20, 21, 22, 24 voor kamer 1 tot en met 15. Het
  ontwerp (§ 2) noemt de levels bij binnenkomst (1, 4, 6, ...); daarmee zou elke speler, ook zonder startlevel, een
  kamer opschuiven, en dat is niet de bedoeling (B-ONTWERP-18).
- Het wapen rekent op k_eff: `RoomW(k_eff)` plus de bestaande stap van de moeilijkheid (`RungWeaponSteps`), hoogstens
  W6. De schijf leest zijn rij op k_eff. De moeilijkheid telt zo één keer: via haar eigen stap, niet nog eens via R.
- De kans op Normal, Rare, Epic en Legendary blijft rekenen op de kamer k van de quest en op de trede; leveling
  verandert de trede niet.
- k_eff is nooit kleiner dan k. Haalt L_sleutel N(k + 1) niet, dan is k_eff gelijk aan k en is het aanbod gelijk aan
  dat van vóór dit stuk. Vanaf L_sleutel 22 is de ladder op: kamer 15 en W6.
- Met z'n tweeën rekent elk aanbod op de kopie van de speler die het krijgt.

Uitzetten (integratie kop 7): met `leveling` uit, of de uitknop in `spire-leveling.json` uit, is H2 niet aangemeld en
rekent elk aanbod op kamer k en de trede van de quest, zoals vóór dit stuk. De mechaniek werkt alleen als zowel de
eigen uitknop als de schakelaar aan staan, dus zonder tabel staat hij ook uit.

Grens van het project die dit stuk raakt: een beloning kun je gebruiken op het moment dat je hem krijgt, ook een wapen
van een latere kamer (regel B van de mag geldt voor elk beloningswapen, `mags.md` § 2).

De wijziging zit in twee repos, beide op tak `feature/mech-leveling`:
- newserv `/home/m4rck/code/worktrees/newserv/mech-leveling`: `src/SpireLevel.cc/.hh` (H2), `system/spire-leveling.json`
  en `src/SpireTestsLevel.cc`. De rest van de rij van leveling in integratie kop 5 (H1 in `SpireLevel.*`,
  `SpireMeta.cc/.hh`, `system/spire-shop.json`) hoort bij latere stukken.
- quest `/home/m4rck/code/worktrees/pso-roguelike/mech-leveling`: alleen het logboek
  `new-features/roguelike/bouw/leveling/`. De quest- en simbestanden uit de rij (`data/grid/groei.json`, `sim/`,
  `data/economy*.json`, `spire-expeditions.json`) horen bij latere stukken.

## Buiten scope
- De latere stukken van leveling: de voorsprong houden met EXP-rijen per client (H1 en de EXP-sidecar
  `system/spire-exp/`), het startlevel per run kiezen in de winkel, de treden 3 tot en met 8, de losse kamers k13-k15
  en de elite-EXP.
- De vijandrijen van kamer 9-15 en de treden voor forces (`sim/`, `data/economy*.json`): na de avondrun met de
  logregel van verdediging (B-ONTWERP-4).
- Het level van de run-mag: dat zet leveling niet (B-ONTWERP-8).
- Regels in gedeelde bestanden en in de quest: die legt de bundel uit `bouw/leveling/voor-de-bundel.md`.
- De keuze van getallen en balans als zodanig.
- Uitrol naar de pc, herstarten van een newserv en de volle pc-build.
- Wat alleen een run op de Wii bewijst, zoals voor de latere stukken de EXP per client en een start boven level 80.
