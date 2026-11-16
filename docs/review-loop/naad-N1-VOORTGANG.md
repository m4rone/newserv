# Naad N1: voortgang

Start 2026-11-16, kop 87c68fe, tak feature/beloningen-b.

- [bezig] voorbereiding: doel, ontwerp §9 en de te dekken code lezen
- [af] src/SpireTestsSeam.cc geschreven (momentopname, vingerafdruk, savestate heen en terug), aanroep in StaticTests.cc, regel in CMakeLists.txt; nog niet gebouwd
- [af] gebouwd; momentopname 356447 regels in 8 secties (kind1 112320, kind2 112320, kind3 5760, kind4 103680, kind5 17280, edges 2200, shared 571, pick 2316), vingerafdruk in tests/spire-seam/offers.fingerprint
- [af] zes savestate-fixtures in tests/spire-seam/ geschreven door de code van 87c68fe; heen en terug exact, ook tests/spire-savestate-before-all-exams.json
- [af] run-static-tests groen (All static tests passed, exit 0)
- [af] eerste commit 78cfbb9 (de wacht en zijn fixtures)
- [af] stap 5, de wacht bijt: `+ 1` op de mag-voer-waarde in `spire_reward_offer` gaf exit 1 met "the reward offers differ from before the seam", sectie kind3 (zelfde 5760 regels, andere hash); teruggezet, `git diff --quiet -- src/SpireRewards.cc` gaf 0
- [af] na het terugzetten herbouwd: run-static-tests exit 0 (All static tests passed), spire-follow-city exit 0 (ok: spire-lives-check)

## Beslissingen (zelf genomen, binnen de opdracht)

- Twee regels in `StaticTests.cc` in plaats van een: de declaratie in het blok en de aanroep. Er is geen header in de schrijfset.
- De wacht draait als eerste in `run_static_tests()`: de roosterbestanden zijn dan nog zoals in de repo (latere tests herschrijven `system/spire-rewards.json` tijdelijk) en een afwijking meldt zich na twee seconden.
- Drie vaste stromen per rij: twee zoals de server zelf trekt (0..n-1), een die niet tot n wordt teruggebracht, zodat de `% size` achter elke trekking vastligt. De trekkingen zelf (n en waarde, op volgorde) staan in de regel.
- Drie secties naast het gevraagde raster: `edges` (kamerindex 0, 16 en 40, trede boven 3, klasse buiten het rooster, rooster zonder Disks en Units), `shared` (kamerindex, W, trede, kansen, namen) en `pick` (wat een wapen en een schijf van de mag vragen, de eenheidsbonus en het vrije slot, het antwoordwoord, de poort van het aantrekken).
- `spire_reward_kind_known` staat bewust niet in de momentopname: de naad verbreedt hem tot 8.
- De savestate-test eist ook dat de struct van elke toestand nog dezelfde tekst geeft, en vergelijkt de gelezen ledger veld voor veld: de schrijver laat een sleutel op zijn standaardwaarde weg, dus de tekst alleen ziet een verloren sleutel niet.

## Gezien, niet gebouwd

- De volgorde van de sleutels in een savestate volgt uit de invoegvolgorde in de phosg-dict (een unordered_map) en uit zijn herindeling bij groei. De verhuizing naar H10 moet "RunMag" dus op hetzelfde moment in dezelfde dict zetten, anders verandert de tekst. Dezelfde tekst hangt ook aan de libstdc++ van de bouwmachine; op de laptop klopt hij, op de pc is hij niet gemeten.
- De foutteksten van `spire_reward_offer` staan in de momentopname (rijen met THROW). Een verhuizing die een fouttekst herschrijft, laat de vingerafdruk falen.
- De momentopname is 84 MB. Schrijf hem met `SPIRE_SEAM_DUMP` naar `build/`, niet naar `/tmp`.
- Dat de savestate-test bijt is niet met een ingreep getoond; alleen de vingerafdruk, zoals gevraagd.
