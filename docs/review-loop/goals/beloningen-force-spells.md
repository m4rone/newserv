---
slug: beloningen-force-spells
base: be9befa
---

## Doel
Marck, de speler en eigenaar (16 nov 2026), over de beloningen van een force:
- "de kracht van een force zit in de spells en de mst."
- "ja de rewards moet gewoon een spell zijn die een force kan gebruiken."

En zijn eis van 14 nov: je kunt elke beloning gebruiken op het moment dat je hem krijgt.

In de lijst van de force stonden 18 regels met een vaste schijf ("Foie Lv3", "Resta Lv5", "Rafoie Lv3" en verder:
F01 tot F15 en F35 tot F37 in `quests/spire/data/rewards.json`). De quest maakte die schijf zelf en keek niet of de
speler hem kon leren of al kende. Dit is bouwstuk 1 van `new-features/roguelike/design/beloningen-per-type.md` §4
(quest-repo); de beslissingen staan in `new-features/roguelike/decisions.md` als B-E02-51 tot B-E02-53.

Wat de wijziging doet:

- **De 18 regels geven een spell die je kunt leren.** Elke regel is nu een beloning van de server (soort schijf), net
  als de bestaande regel "Disk". De server houdt de techniek van de regel aan en kiest het level: het level van Sega
  voor de kamer en de getrokken trede, minstens het level dat de regel vroeger gaf, en minstens twee boven wat de
  kopie al kent (één als dat het hoogste van de klasse is). Kan de kopie de MST daarvoor niet halen, ook niet met de
  mag van regel B, dan gaat het level omlaag tot het wel kan, nooit tot wat de kopie al kent.
- **Kan de kopie in die techniek niet meer groeien, dan een andere spell.** De server kiest dan een techniek van de
  trede, zoals de regel "Disk" doet. Heeft hij niets te geven (geen tabel, geen kopie, niets leerbaar), dan krijgt de
  speler de oude vaste schijf van de regel.
- **De naam belooft geen level meer.** De regels heten naar hun techniek ("Foie", "Barta"); het level en de trede
  staan in de beloningsregel van de server ("RARE Foie Lv9").
- **Twee van de 18 regels zijn zeldzaam (F03 en F11).** Het aanbod van een zeldzame regel is nooit lager dan de trede
  Rare. Een getrokken vloek op zo'n regel betaalt zoals bij een schijf van de quest: Cursed Chest en Glass geven twee
  levels extra, binnen het hoogste van de klasse en wat de kopie aan MST haalt. De quest stuurt de vloek van de keuze
  mee in het verzoek (bits 6 en 7 naast het regelnummer), zodat ook een aanbod dat de server pas bij de keuze maakt
  de levels krijgt. De beloningsregel zet achter een aanbod van de server geen "(rare)" meer: de trede staat er al
  voor.
- **Het examen bij een baas geeft geen serverbeloning.** Het examen geeft zijn zeldzame item in een kamerthread,
  zonder wachten op de server. De twee zeldzame schijfregels vallen daar dus af: een force krijgt bij het examen
  een van de drie Merges. Hun plaats in de tabel blijft bestaan, zodat een opgeslagen run met een wachtend
  exameditem dezelfde tabel leest; op die twee plaatsen staat de Foie Merge.

Vaste grenzen van het project die hier gelden: geen itemverlies op een karakter en geen dubbele gift, geen
vastloper, en een opgeslagen run leest zijn tellers nog goed (de volgorde en de bytes van elke regel in de lijst zijn
ongewijzigd). Voor hunters en rangers, en voor elke regel die geen schijf van zichzelf noemt, verandert niets.

De wijziging zit in twee repos, beide op tak `feature/beloningen-b`:
- newserv (deze repo), base be9befa: `src/SpireRewards.hh`, `src/SpireRewards.cc`, `src/SpireFlagBridge.cc`,
  `src/SpireDraw.hh`, `src/SpireDraw.cc` en de tests in `src/StaticTests.cc`.
- de quest-repo (`/home/m4rck/code/worktrees/pso-roguelike/beloningen-b`), base 1e7fb649:
  `quests/spire/data/rewards.json`, `quests/spire/rewards.py` en de poort `quests/spire/check_ux_pass.py`.

## Buiten scope
- Mana als beloning (Difluid en Trifluid, bouwstuk 2), units per type, spells voor hunters en rangers, androids.
- Schijven die de quest op andere plekken zelf maakt: Sea of Fire, de extra van het examen, de kamerschijf bij een
  Merge met Cursed Chest, en de winkel.
- De treden Hard, Very Hard en Ultimate (daar draait nog het oude beloningssysteem).
- De automatische beloning van een volger: die neemt nooit een serverbeloning (bestaand gedrag).
- Een schijf terwijl regel B weigert of er geen run-mag is (bestaand gedrag).
- Uitrol naar de pc of het lab, en herstarten van een newserv.
- De bouwscripts van de quest-repo (`build.sh`, `pc-build.sh`) en de poorten die deze wijziging niet raakt.
