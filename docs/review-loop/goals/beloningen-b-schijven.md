---
slug: beloningen-b-schijven
base: b8da856
---

## Doel
Marck, de speler en eigenaar, over de beloningen in de Spire-run (16 nov 2026):
- "this is what the biggest focus is. a good system."
- Op de vraag of de client een schijf boven Lv1 laat leren: "yes. depending on your mst. mag included."

Deel B, stap 3 van het ontwerp `new-features/roguelike/design/beloningen-per-klasse.md` §4 (quest-repo): de
techniekschijf als beloning, zoals vastgelegd in de beslissingen B-E02-23 (punt 3) en B-E02-30
(`new-features/roguelike/decisions.md`, quest-repo):

- **De schijf (beloningstype Schijf, serversoort 4).** De server kiest bij de trekking een techniek en een level
  dat de run-kopie kan leren: Sega's schijflevel voor de kamer en de tier, hoogstens het maximum van de klasse, voor
  een techniek die de kopie al kent minstens twee levels hoger (één als dat het klassemaximum is), en omlaag (nooit
  tot wat de kopie al kent) tot de kopie de MST kan halen die de client vraagt: MST minstens Base + Step x (level - 1)
  per techniek, op de eigen MST plus gedragen units plus de mag (2 per MIND-level).
- **De mag draagt de eis.** Bij het pakken gaat de MIND die de mag daarvoor nodig heeft in regel B, naast wat het
  beloningswapen vraagt: per stat het hoogste van de twee. Een schijf kost het wapen geen POW of DEX, en een later
  wapen kost de schijf geen MIND; dat blijft zo na een hervatte run.
- **In de quest** is het één nieuwe regel "Disk" aan het eind van de force-pool. Heeft de server niets te geven, dan
  krijgt de speler het eigen item van die regel.

Vaste grenzen van het project die hier gelden: geen itemverlies op een karakter en geen dubbele gift, geen
vastloper, nooit een schijf die de server bij gebruik weigert (boven het klassemaximum), en een opgeslagen run leest
zijn tellers per poolregel nog goed (een pool groeit alleen aan het eind).

De wijziging beslaat twee repos: newserv (deze repo, tak feature/beloningen-b, base b8da856) en de quest-repo in
`/home/m4rck/code/worktrees/pso-roguelike/beloningen-b` (tak feature/beloningen-b, base b1e16776).

## Buiten scope
- De units en het frame met vier slots (gereviewd en uitgerold op 16 nov), en deel A (wapen, nieuwe lijn, mag-voer),
  behalve waar de schijf die code bewerkt.
- Schijven voor hunters en rangers die technieken leren; de oude poolregels met een vaste schijf.
- De treden Hard, Very Hard en Ultimate.
- Of de MST die de server van een GameCube-personage bewaart de uitrusting al bevat (zelfde aanname als bij een
  wapen; Marcks run meet het).
- Uitrol naar de pc of het lab, en herstarten van een newserv.
- De bouwscripts en poorten van de quest-repo die een aparte sessie versnelt (`build.sh`, `pc-build.sh` behalve de
  regel met de newserv-pin, `check_*.py`, `test_*`).
