---
slug: beloningen-a
base: 1e0cb5a
---

## Doel
Marck, de speler en eigenaar, over de beloningen in de Spire-run (14 nov 2026):
- "de wapen type upgrade is cool, dus als je die kiest krijg je gewoon van die lijn. tot aan legendaries is ook wel vet."
- "moet je het wel kunnen equippen natuurlijk, nogal nutteloos als het niet kan."

Deel A van het ontwerp `new-features/roguelike/design/beloningen-per-klasse.md` (quest-repo, tak feature/beloningen) met het grid `beloningen-per-klasse.json` ernaast:
- De beloningstypen Wapen (volgt de lijn van het wapen dat je vasthoudt; de kit van de Quartermaster kiest de lijn), Nieuwe lijn en Mag-voer.
- Wapenlevel W1-W6 per kamer en de tiers Normal, Rare, Epic en Legendary met de kansen uit `design/kracht-voorbij-normaal.md` §4. Legendary gaat open na de eerste De Rol Le.
- Elke beloning is te equippen op het moment dat je hem krijgt. Mag-regel B uit §3 is de brug daarheen: de mag groeit met het wapen, komt nooit boven 200 en zakt nooit.
- Het ATA-model is één configsleutel. De serverdrops lezen dezelfde sleutel.
- Geen itemverlies: na een run geeft de itembewaker het echte karakter exact zijn inventaris en mag terug.

De wijziging beslaat twee repos: newserv (deze repo, tak feature/beloningen, base 1e0cb5a) en de quest-repo in `/home/m4rck/code/worktrees/pso-roguelike/beloningen-a` (tak feature/beloningen, base 7677a86c).

## Buiten scope
- Deel B: Schijf (tech disks), Kracht (units, TP, superkrachten) en het frame met 4 slots.
- Uitrol naar de pc of het lab, en herstarten van een newserv.
- Extra werk voor Episode 2 of per moeilijkheid.
- De fase 0-fixes op spike/fase0 (mag-regel, Jellen, partnermelding, vloekregel).
