---
slug: naad-testregel
base: de69fad
---

## Doel
De naad (`docs/review-loop/goals/naad.md`, nagekeken in ronde 1) legt de code zo dat acht mechanieken elk in eigen
bestanden bouwen en zich aanmelden op een soorttabel en op de haken H1 tot H11 (`src/SpireMechanics.hh`). De eerste
bouwers die erop bouwden vonden vier plekken waar een mechaniek die zich aanmeldt toch een bestand van een ander moet
wijzigen of het gedrag van een ander verschuift. Dat is precies wat de naad moest weghalen. Deze wijziging legt die vier.
**Zolang niemand zich aanmeldt verandert er niets:** op de code van nu (alleen de run-mag staat op H10) is elk aanbod,
elke opgeslagen sleutel en elke bestaande test gelijk.

1. **De toetsen tellen geen aanmeldingen meer.** `src/SpireTestsMechanics.cc` eiste dat op elke haak behalve H10
   niemand staat, dat er precies vijf soorten zijn, dat geen soort een eerste route heeft en dat de soorten 6 tot 8
   niet zijn aangemeld. De eerste echte aanmelding van een mechaniek maakte de statische tests daardoor rood in een
   bestand dat niet van hem is. De toetsen vergelijken nu met de stand aan het begin van het testbestand (na de test
   staat er weer wat er stond) en toetsen verder de invarianten: de run-mag staat op H10, er is geen dubbele
   aanmelding, en de eigen testsoort en testhaken gedragen zich zoals de tabel belooft.
2. **Tijdens de statische tests zwijgen de echte aanmeldingen van anderen.** Een mechaniek die standaard aan staat
   (een ontbrekende schakelaar is aan) verschoof anders de uitkomst van de tests van andere eigenaren: een gewicht op
   H3 geeft een andere unit, een eerste route op soort 5 een ander aanbod, H2 een andere kamer. De regel: zolang
   `run-static-tests` draait, bereiken de haken H1 tot H11 en de eerste routes alleen aanmelders zonder schakelaar (de
   run-mag in de savestate) en aanmelders waarvan het draaiende testbestand de sleutel heeft aangezet met een zetter
   die alleen in de tests bestaat; de soorten zelf blijven aan. Een aanmelding die een test zelf doet, werkt in die
   test zoals nu. In productie geldt de zwijgstand nooit: alleen het draaien van de statische tests zet hem, en dat is
   getoetst.
3. **De kansen blijven op de kamer van de quest.** H2 (`reward_room`) geeft de kamer en de trede waarop een aanbod
   rekent. De kamer uit H2 bepaalt wat het aanbod inhoudt (het wapen, de schijf), niet de kans op een hoger niveau:
   `spire_reward_odds` krijgt de kamer van de quest en de trede uit H2. Het commentaar bij H2 zegt dat. Zonder
   aanmelder op H2 zijn beide kamers dezelfde.
4. **De vingerafdruk blijft groen** in elke commit (`-- Spire seam: the reward offers are as before the seam`), net als
   de savestate-fixtures; de bestanden onder `tests/spire-seam/` wijzigen niet.

Het ontwerp waar punt 3 uit komt: `new-features/roguelike/design/mechanics/leveling.md` op tak `docs/ontwerp-mechanics`
van de quest-repo (werkplek `/home/m4rck/code/worktrees/pso-roguelike/ontwerp-mechanics`), "de kansen blijven op k".

Vaste grenzen van het project die hier gelden: geen itemverlies op een karakter en geen vastloper. Een zwijgstand die
in productie aan staat zou elke mechaniek stil uitzetten: dat is een fout in deze wijziging.

De wijziging zit alleen in newserv (deze repo), base de69fad: `src/SpireMechanics.hh`, `src/SpireMechanics.cc`,
`src/SpireTestsMechanics.cc`, `src/SpireFlagBridge.cc` (de ene aanroep van `spire_reward_odds`), `src/StaticTests.cc`
of `src/Main.cc` (alleen waar het draaien van de statische tests de zwijgstand zet), en waar nodig één regel in de
testbestanden `src/SpireTests*.cc`.

## Buiten scope
- De rest van de naad: die is in ronde 1 nagekeken.
- Wat een mechaniek op een haak doet, en de tests van de mechanieken zelf: die bouwen de acht bouwers.
- De keuzes van het ontwerp: welke haken er zijn, wat H2 teruggeeft, dat een ontbrekende schakelaar aan is.
- De quest-repo, de uitrolscripts, uitrol naar de pc of het lab en een pc-build.
