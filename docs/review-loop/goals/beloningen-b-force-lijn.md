---
slug: beloningen-b-force-lijn
base: eec3326
---

## Doel
Marck, de speler en eigenaar (16 nov 2026), toen bleek dat een force na een schijf een zwaar vechtwapen kon krijgen
dat hij niet kan dragen:
- "gebruikt een force ooit een vechtwapen?"
- "de kracht van een force zit in de spells en de mst. daar is hopelijk op de juiste manier over nagedacht"

En zijn harde eis van 14 nov: je kunt elke beloning equippen op het moment dat je hem krijgt.

Wat de wijziging doet (beslissing B-E02-33 in `new-features/roguelike/decisions.md`, quest-repo):

- **Geen vechtwapen opdringen aan een force.** De beloning Nieuwe lijn geeft een force (archetype FO) alleen een lijn
  waarvan de wapens MST vragen (Cane, Rod, Wand), nooit Saber, Slicer, Handgun of Mechgun. Draagt een force geen wapen
  van een bekende lijn, dan volgt de wapenbeloning zijn eerste MST-lijn (Cane), niet Saber. Voor hunters en rangers
  verandert niets.
- **Wie zelf een vechtwapen draagt, houdt de regel van 14 nov.** Draagt een force een saber, slicer, handgun of
  mechgun, dan volgt de wapenbeloning die lijn nog.
- **Het beloningswapen is altijd te dragen, ook na een schijf.** Vragen het wapen (POW en DEX) en de schijven van de
  run (MIND) samen meer dan de 200 levels van de mag, dan blijft de eis van het wapen heel en krijgt de MIND van de
  schijven wat er over is. De mag levert daarvoor MIND in die hij al had, voordat een gevraagde stat onder zijn eis
  komt. Bij de trekking paste een schijf al naast het wapen; dat blijft zo, ook voor de schijven van eerder in de run.

Vaste grenzen van het project die hier gelden: geen itemverlies op een karakter en geen dubbele gift, geen
vastloper, en een opgeslagen run leest zijn tellers nog goed. Voor hunters en rangers, en voor een force op een
MST-lijn, geeft regel B dezelfde mag als vóór deze wijziging.

De wijziging zit in newserv (deze repo, tak feature/beloningen-b, base eec3326); in de quest-repo
(`/home/m4rck/code/worktrees/pso-roguelike/beloningen-b`) alleen de tekst van het ontwerp en de beslissing.

## Buiten scope
- De keuze van techniek en level van een schijf (gereviewd in `beloningen-b-schijven`).
- Een schijf of wapen terwijl regel B weigert of er geen run-mag is (bestaand gedrag, voor een wapen al zo).
- Schijven voor hunters en rangers die technieken leren; de treden Hard, Very Hard en Ultimate.
- Uitrol naar de pc of het lab, en herstarten van een newserv.
- De bouwscripts en poorten van de quest-repo die een aparte sessie versnelt (`build.sh`, `pc-build.sh` behalve de
  regel met de newserv-pin, `check_*.py`, `test_*`).
