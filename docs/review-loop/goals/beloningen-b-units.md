---
slug: beloningen-b-units
base: 8e77957
---

## Doel
Marck, de speler en eigenaar, over de beloningen in de Spire-run (16 nov 2026):
- "this is what the biggest focus is. a good system."
- "yes. so frame with 4 slots and items in it count toward the ability to equip weapons."

Deel B van het ontwerp `new-features/roguelike/design/beloningen-per-klasse.md` (quest-repo), de units en het frame met
vier slots, zoals vastgelegd in de beslissingen B-E02-23 (punt 1, 2 en 4), B-E02-25 en B-E02-26
(`new-features/roguelike/decisions.md`, quest-repo):

- **Units (beloningstype Kracht).** De server kiest de unit bij de trekking, per klassefamilie en tier, uit de
  unit-lijsten van het ontwerp-JSON: hunters kracht en snelheid, rangers richten, kracht en snelheid, forces mind, TP,
  TP-herstel en techniek. Na het pakken trekt de server de unit aan in het eerste vrije slot van het gedragen frame;
  zonder vrij slot of zonder frame blijft hij in het pak en leest de speler waarom. Gedragen units tellen mee voor de
  ATP- en MST-eis van een wapen. In de quest is het één nieuwe regel "Unit" aan het eind van elke pool; heeft de server
  niets te geven, dan krijgt de speler het eigen item van die regel.
- **Frame met vier slots.** Het kit-frame wordt met alle bytes gemaakt (slotbyte 4) en door de server aangetrokken bij
  het verzoek voor het kitwapen, alleen op een client met de aantrek-functie; anders blijft het de oude weg. De frames
  in de beloningspools en in de winkel hebben ook vier slots.

Vaste grenzen van het project die hier gelden: geen itemverlies op een karakter en geen dubbele gift, geen vastloper,
geen item maken op een client terwijl daar een golf loopt, en een opgeslagen run leest zijn tellers per poolregel nog
goed (een pool groeit alleen aan het eind). Het echte personage is na de run terug zonder iets van de run.

De wijziging beslaat twee repos: newserv (deze repo, tak feature/beloningen-b, base 8e77957) en de quest-repo in
`/home/m4rck/code/worktrees/pso-roguelike/beloningen-b` (tak feature/beloningen-b, base 458ee903).

## Buiten scope
- Schijven (serversoort 4) en de MST-eis van een schijf.
- Deel A (wapen, nieuwe lijn, mag-voer, regel B), behalve waar deel B het raakt.
- De treden Hard, Very Hard en Ultimate: hun beloningsbestanden blijven zoals ze zijn (B-E02-25 punt 3).
- Units voor ATA in de eis van een wapen.
- Uitrol naar de pc of het lab, en herstarten van een newserv.
- De bouwscripts en poorten van de quest-repo die een aparte sessie versnelt (`build.sh`, `pc-build.sh` behalve de
  regel met de newserv-pin, `check_*.py`, `test_*`).
