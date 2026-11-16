---
slug: naad
base: c786164
---

## Doel
Marck, de speler en eigenaar (16 nov 2026, 20:50): "laten we opschalen. en de rewards voor alle andere types aan een
aparte agent geven. niet te veel deployen/tijdverspillen mogen best 10 dingen tegelijk gaan."

Acht mechanieken (force, leveling, mags, aanval, verdediging, hunter-types, ranger-types, superkrachten) krijgen elk
een eigen bouwer. Vóór deze wijziging schreven ze allemaal in dezelfde functies: één aanbodfunctie met een tak per
soort beloning, één testfunctie van zeventienduizend regels, één vaste lijst in `CMakeLists.txt`, één rooster van één
regel. De naad legt de code zo dat elke mechaniek in eigen bestanden bouwt en zich aanmeldt, zonder dat iemand de kern
hoeft te wijzigen. Het ontwerp is `new-features/roguelike/design/mechanics/integratie.md` op tak `docs/ontwerp-mechanics`
van de quest-repo (werkplek `/home/m4rck/code/worktrees/pso-roguelike/ontwerp-mechanics`): §3 (de naden, de soorttabel
en de haken H1 tot H11), §5 (wie welk bestand schrijft), §7 (de schakelaars), §9 (de naad in zeven stappen) en de
lijst "Afwijkende namen". De regelnummers daar zijn gemeten op een oudere commit; de functienamen gelden.

**De naad verandert geen gedrag.** Wat een speler aangeboden krijgt, wat hij kiest en krijgt, wat de server opslaat
en wat de quest doet, is na de naad gelijk aan ervoor. Dat is het ene dat deze wijziging moet halen.

Wat de wijziging doet:

1. **Bouw en tests zonder gedeelde lijst.** `CMakeLists.txt` neemt `src/Spire*.cc` met een glob. Een testbestand meldt
   zich aan, en `StaticTests.cc` roept alle aangemelde tests op één plek aan. De bestaande tests van de soorten 1 tot 5
   en van de run-mag staan letterlijk in het testbestand van hun eigenaar (`SpireTestsAttack.cc`, `SpireTestsMags.cc`,
   `SpireTestsForce.cc`, `SpireTestsDefence.cc`): geen test verdwijnt en geen test wordt zwakker.
2. **`SpireMechanics.cc/.hh`:** de tabel van soorten (een soort meldt zich aan met vier functies: eigen sectie lezen,
   aanbod, keuze, resultaat), de elf haken H1 tot H11 als aanmeldlijsten met één aanroep per haak in de kern, en de
   lezer van de schakelaars (`system/spire-schakelaars.json`, opnieuw gelezen zodra het bestand verandert). Zonder
   aanmelding op een haak gebeurt wat er vóór de naad gebeurde. Ontbreekt het schakelaarbestand of een sleutel, dan
   staat alles aan; een onleesbaar bestand laat de laatste goede stand staan en meldt dat in het journaal.
3. **De soorten 1 tot 5 staan letterlijk in hun eigen bestand** (`SpireRewardWeapon.cc`, `SpireRewardMagFood.cc`,
   `SpireRewardDisk.cc`, `SpireRewardUnit.cc`). `SpireRewards.cc` houdt het gedeelde deel en de verdeling. De soorten
   6 (focus), 7 (mana) en 8 (pantser) bestaan als nummer en zijn bekend, maar niemand meldt ze aan: een regel van zo'n
   soort geeft het eigen item van de regel, net als een soort die uit staat.
4. **De run-mag in de savestate** staat letterlijk in `SpireMagState.cc` en meldt zich aan op H10, met dezelfde
   sleutels op dezelfde plek. H11 (meta per personage) ligt in `SpireMeta.cc`.
5. **In de quest-repo:** `rewards.py` kent de soorten focus 6, mana 7 en pantser 8 en laat focus toe als zeldzame
   serverregel. `make_spire_rewards.py` leest zijn bronnen per eigenaar uit `quests/spire/data/grid/*.json` en schrijft
   het rooster ingesprongen. `quests/spire/data/schakelaars.json` begint leeg.
6. **Uitrol:** `pc-build.sh` en `apply-on-pc.sh` nemen `system/spire-*.json` van de vastgepinde newserv-commit en de
   schakelaars mee, en `terugdraai.sh` zet ze terug. `test_apply_rollout_order.sh` dekt dat met zijn fixture.
7. **`.claude/review-loop.json`** haalt zijn paden uit de eigen werkplek, zodat een bouwer in een andere werkplek
   dezelfde config kan gebruiken.

Het bewijs dat het gedrag gelijk blijft, alle zes:

- een momentopname van alle aanbiedingen met een vaste seed (12 klassen, 15 kamers, 4 niveaus, 4 treden, soorten 1
  tot 5) is na de naad gelijk aan die van vóór de naad. De vingerafdruk van vóór is vastgelegd in de eerste commit van
  de naad, op de oude code, en elke commit daarna is eraan gelijk;
- een savestate van vóór de naad, met de sleutels van de run-mag gevuld, laadt en wordt gelijk teruggeschreven;
- het nieuwe rooster is als JSON gelijk aan het oude;
- de questbouw is byte voor byte gelijk en het labartefact q929 verandert niet;
- zonder schakelaarbestand verandert er niets;
- alle bestaande tests zijn groen.

Vaste grenzen van het project die hier gelden: geen itemverlies op een karakter, geen vastloper, en de pc-newserv
wordt alleen via `apply-on-pc.sh` bij nul spelers vervangen. De map `system/spire-bridge` op de pc draagt de spullen
van elk personage tijdens een run: een uitrol of een terugdraai die daar iets overschrijft of weghaalt dat hij niet
zelf heeft neergezet, is een fout in deze wijziging.

De wijziging zit in twee repos, beide op tak `feature/beloningen-b`:
- newserv (deze repo), base c786164: `CMakeLists.txt`, `src/StaticTests.cc`, `src/SpireMechanics.*`,
  `src/SpireRewards.*`, de nieuwe `src/SpireReward*.cc`, `src/SpireMagState.cc`, `src/SpireTests*.cc`,
  `src/SpireFlagBridge.*`, `src/SpireMeta.cc`, `src/ReceiveSubcommands.cc`, `src/ReceiveCommands.cc`,
  `system/spire-rewards.json` (alleen de opmaak) en `.claude/review-loop.json`.
- de quest-repo (`/home/m4rck/code/worktrees/pso-roguelike/beloningen-b`), base adaac25a: `quests/spire/rewards.py`,
  `quests/spire/make_spire_rewards.py`, `quests/spire/data/grid/`, `quests/spire/data/schakelaars.json`,
  `quests/spire/pc-build.sh`, `quests/spire/apply-on-pc.sh`, `quests/spire/terugdraai.sh` met hun hulpscripts, en
  `quests/spire/test_apply_rollout_order.sh`.

## Buiten scope
- Elk nieuw gedrag van een mechaniek: wat de soorten 6 tot 8 geven, wat een mechaniek op een haak doet, en de
  werking van een schakelaar voor een mechaniek die nog niet gebouwd is. De naad legt alleen de plek.
- De keuzes van het ontwerp zelf: welke soorten en haken er zijn, hoe ze heten, wie welk bestand bezit.
- De vorm van de bestaande tests die verhuisd zijn: ze zijn letterlijk verplaatst, niet herschreven.
- De code van de soorten 1 tot 5 zelf, voor zover die letterlijk is verplaatst: wat er vóór de naad al stond en
  werkte, is gegeven.
- De balans, de teksten en de constanten van het rooster.
- Uitrol naar de pc of het lab, herstarten van een newserv, en een volle pc-build.
