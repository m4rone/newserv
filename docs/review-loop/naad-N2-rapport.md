# Naad N2: rapport

Fase N2 van de naad (`docs/review-loop/goals/naad.md` punt 1): bouw en tests zonder gedeelde lijst.
Basis `2cfb90c`, tak `feature/beloningen-b`, de code staat in `060e346`. Gemeten op 2026-11-16 op de laptop.

## Dekkingsclaim

Deze fase dekt: dat de bouw een nieuw `src/Spire*.cc` vanzelf meeneemt (getoond met de zes nieuwe bestanden van deze
fase, zonder dat `CMakeLists.txt` er een noemt), en dat elke verhuisde test letterlijk en precies een keer draait
(regel voor regel vergeleken met `2cfb90c`, de som van de toetsende regels gelijk, een verhuisde test faalt als zijn
verwachte waarde verandert).

Deze fase dekt NIET: of de indeling per eigenaar voor elke test de juiste is (de twijfelgevallen staan hieronder), en
niets van de soorten, haken of schakelaars (volgende fase). Ook niet gedekt: een run van de oude binary naast de
nieuwe om de logregels van `run-static-tests` regel voor regel te vergelijken; vergeleken is de broncode, niet de
uitvoer van voor en na.

## Wat er staat

- `CMakeLists.txt`: de zeven vaste regels `src/Spire*.cc` zijn een `file(GLOB SPIRE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/Spire*.cc)`.
- `src/SpireMechanics.hh` en `.cc` (nieuw): alleen de aanmelding van tests. `spire_mech_register_tests(naam, functie)`,
  `spire_mech_run_tests()` en `spire_mech_test_names()`. De lijst is een static binnen een functie
  (`test_registry()`), een `std::map` op naam: de volgorde van draaien is de volgorde van de namen. Per testbestand
  een regel op stderr: `-- Spire test file: <naam>`.
- `src/StaticTests.cc`: een aanroep `spire_mech_run_tests();` als eerste regel van `run_static_tests()`, op de plek
  van de twee regels van de wacht.
- `src/SpireTestsAttack.cc` (naam `attack`), `SpireTestsDefence.cc` (`defence`), `SpireTestsForce.cc` (`force`),
  `SpireTestsMags.cc` (`mags`): de verhuisde tests, elk een functie en een aanmelding.
- `src/SpireTestsSeam.cc`: een include en een aanmeldregel (`seam`); verder ongewijzigd.
- `src/SpireTestSupport.hh` (nieuw): `spire_test_first_room_of_w(rewards)`. Dat was de lokale map
  `first_room_of_w` in het rewards-blok; de test die bleef (elke W van 2 tot 6 heeft een kamer) en de verhuisde
  test per cel in `SpireTestsAttack.cc` lezen hem allebei. De vier regels staan nu een keer, in deze header.

## Beslissingen (zelf genomen, binnen de opdracht)

- **De wacht draait als laatste van de aangemelde bestanden, niet meer als allereerste.** De volgorde is op naam
  (attack, defence, force, mags, seam), zoals gevraagd. De hele groep draait wel als eerste in
  `run_static_tests()`, en de vier bestanden ervoor schrijven geen bestand en wisselen niet van map (geteld:
  `save_file`, `current_path`, `remove_all`, `temp_directory_path` komen er nul keer in voor). De wacht ziet de
  roosterbestanden dus nog zoals ze in de repo staan, en meldt zich in dezelfde seconde.
- **De verhuisde tests draaien nu aan het begin, niet meer op hun oude plek tussen de andere.** Geen test las iets
  dat een eerdere test klaarzette: alles is groen in de nieuwe volgorde.
- **Een naam die twee keer wordt aangemeld, of een lege lijst, laat `spire_mech_run_tests()` gooien.** Zo draait geen
  test dubbel en valt het op als de aanmelding als geheel wegvalt. Er is bewust geen lijst van verwachte namen: dat
  zou de gedeelde lijst terugbrengen.
- **De opzetregels staan per testbestand.** De verhuisde stukken lazen drie lokale variabelen van het blok waar ze
  uit komen: `rewards` (het rooster), `mags` (de magtabel) en `ipt` (de itemtabel). Elk testbestand zet de regels
  die het nodig heeft bovenaan zijn functie, met dezelfde tekst. `rewards` blijft ook in `StaticTests.cc` staan (vier
  kopieen van een regel); `mags` en `ipt` verdwenen daar en staan elk in twee testbestanden. Het zijn geen
  hulpfuncties of lambda's, dus ze staan niet in de header; ze zijn in de uitvoer hieronder met `copy` en met `=`
  aangewezen.
- **Het kopje van het run-mag-blok verhuisde mee** (`-- Spire run mag: step, bytes, ...`). Het kopje van het
  rewards-blok bleef in `StaticTests.cc`, bij de tests die daar bleven; de verhuisde stukken staan in het log onder
  de regel van hun testbestand.
- **De verhuisde tekst is niet opnieuw opgemaakt.** `clang-format --dry-run` geeft 14 meldingen in de nieuwe
  testbestanden, alle in verhuisde regels die door het inspringen anders afbreken. Letterlijk gaat voor
  (`StaticTests.cc` van `2cfb90c` geeft er zelf 216).

## Twijfelgevallen in de indeling

Regelnummers zijn van `2cfb90c:src/StaticTests.cc`.

Gebleven in `StaticTests.cc`, terwijl er iets voor een eigenaar te zeggen is:

1. 7814-7834: het wapen dat een trede geeft (een aanbod van soort 1) en de weigering van een rooster zonder
   `RungWeaponSteps`. Een blok dat de gedeelde W per trede toetst via een wapenaanbod, met een parsertest erin.
2. 8030-8039: een item onder `Units` dat geen unit is, wordt bij het laden geweigerd. Parser; zodra elke soort zijn
   eigen sectie leest, hoort hij bij verdediging.
3. 8410-8418: een schijflevel boven 30 wordt bij het laden geweigerd. Parser; later van de force.
4. 8440-8443: `spire_reward_kind_known` (1 tot 5 bekend, 0, 6 en -1 niet). Gedeeld; de volgende fase verbreedt hem.
5. 8503-8512: het ATA-model (`spire_ata_dex_needed`). Gedeeld met `SpireDrops`; raakt vooral het wapen.
6. 8558-8569: `spire_reward_equip_gate`, het aantrekken van het beloningswapen wacht op de mag van regel B. Tussen
   aanval en mags in; de poort zelf zit in de gedeelde aantrek-code.
7. De blokken over de savestate (6881-7418) lezen en schrijven ook de run-mag in de savestate; ze toetsen de
   savestate als geheel en bleven.
8. Het blok "Spire guard, played end to end" (10331-13487) roept `spire_reward_offer` aan voor wapen en nieuwe lijn
   en rekent de MIND van een schijf na, midden in een gedeeld spel van 3157 regels. Niet te splitsen; gebleven.
9. De blokken over drops (4581-5383): `SpireDrops` is volgens het ontwerp van aanval, maar het zijn geen tests van
   soort 1 of 2, en de opdracht noemt voor `SpireTestsAttack.cc` alleen die twee. Gebleven.
10. De blokken bundel 9, kwaliteit-2, kwaliteit-3, practice en `$kit` noemen de mag, units en schijven via de brug.
    Ze zijn niet regel voor regel gelezen: geteld is waar de woorden vallen, en geen van die blokken heeft een van
    de vier als enige onderwerp. Gebleven.

Verhuisd, terwijl er ook iets voor een andere plek te zeggen is:

11. 7836-7929 naar aanval: de lus over elke cel toetst het wapen (soort 1), de nieuwe lijn (soort 2) en, in dezelfde
    lus, regel B van de mag per cel. Een lus is niet letterlijk te splitsen; hij leest daarom ook de magtabel.
12. 8344-8351 naar force: `spire_reward_need_with_disk`, de behoefte van regel B met een schijf in de run. Kan ook bij
    mags.
13. 8352-8373 naar mags: `spire_mag_fit_200` met de behoefte van een schijf. Kan ook bij de force.
14. 8374-8409 naar aanval: de eigen lijnen van een force (Cane, Rod, Wand) bij soort 1 en 2. De soort besliste, niet
    de klasse.
15. 8011-8028 naar verdediging: `spire_equip_find_kit_frame` (het kit-frame dat 'C' 13 aantrekt). Het is het frame
    met vier slots, maar geen functie van soort 5.
16. Het run-mag-blok 5930-6879 ging als geheel naar mags. Daarin staan ook gevallen die de sleutels van de run-mag in
    de savestate toetsen (`spire_savestate_to_json` en `_from_json`) en een paar teksten
    (`spire_boss_burst_text_due`, `spire_kit_use_text_due`, `spire_ticker_line`).

## Bewijs 1: geen test weg

Toetsende regels: een regel met `expect(`, `expect_eq(`, `expect_ne(`, `expect_raises(`, een van de lokale
`expect_...`-lambda's (`expect_real`, `expect_game_step`, `expect_enemy_and_object_kept`) of `throw`.

```
$ git show 2cfb90c:src/StaticTests.cc | grep -cE "\bexpect[_a-z]*\(|\bthrow\b"
4653
$ grep -cE "\bexpect[_a-z]*\(|\bthrow\b" src/StaticTests.cc src/SpireTestsAttack.cc src/SpireTestsDefence.cc src/SpireTestsForce.cc src/SpireTestsMags.cc src/SpireTestSupport.hh
src/SpireTestsDefence.cc:34
src/SpireTestsAttack.cc:35
src/SpireTestsForce.cc:87
src/SpireTestSupport.hh:0
src/SpireTestsMags.cc:443
src/StaticTests.cc:4054
$ (the same, summed)
4653
$ git show 2cfb90c:src/SpireTestsSeam.cc | grep -cE ... ; grep -cE ... src/SpireTestsSeam.cc   (the guard of N1, apart)
17
17
$ git show 2cfb90c:src/StaticTests.cc | grep -oE "\bexpect[_a-z]*\(|\bthrow\b" | sort | uniq -c
   1892 expect(
      3 expect_enemy_and_object_kept(
   2640 expect_eq(
     10 expect_game_step(
      2 expect_ne(
     14 expect_raises(
     52 expect_real(
     40 throw
$ cat <the six files now> | grep -oE ... | sort | uniq -c
   1892 expect(
      3 expect_enemy_and_object_kept(
   2640 expect_eq(
     10 expect_game_step(
      2 expect_ne(
     14 expect_raises(
     52 expect_real(
     40 throw
```

Voor 4653, na 4653 (4054 + 35 + 34 + 87 + 443 + 0). De wacht apart: 17 voor, 17 na.

## Bewijs 2: letterlijk verhuisd

Het script staat onderaan dit rapport (het stond in de scratchmap, niet in de repo). Het leest de basis uit git en
de werkboom, en controleert zelf wat de verhuislijst beweert: (1) de basis min de genoemde regels is regel voor regel
gelijk aan de `StaticTests.cc` van nu min de vier toegevoegde regels; (2) elk verhuisd stuk staat aaneengesloten in
zijn bestand, gelijk op de inspringing vooraan na; (3) los van de lijst: elke niet-lege regel die verdween staat
vaak genoeg in de nieuwe bestanden; (4) welke regels in de nieuwe bestanden niet verhuisd zijn (`=` betekent: dezelfde
tekst staat ook nu in `StaticTests.cc`).

Wat niet uit `StaticTests.cc` komt, is in elk testbestand: de includes, vier regels commentaar, de naamruimte, de
functie, de aanmelding, en de opzetregel `auto rewards = ...` (in aanval ook de aanroep van de helper).

```
$ python3 verify.py /home/m4rck/code/worktrees/newserv/beloningen-b; echo verify_exit=$?
ok   range 5930-6879 overlaps no other range
ok   range 7741-7741 overlaps no other range
ok   range 7792-7795 overlaps no other range
ok   range 7836-7929 overlaps no other range
ok   range 7931-7939 overlaps no other range
ok   range 7940-7942 overlaps no other range
ok   range 7944-7944 overlaps no other range
ok   range 7945-8028 overlaps no other range
ok   range 8041-8351 overlaps no other range
ok   range 8352-8373 overlaps no other range
ok   range 8374-8409 overlaps no other range
ok   range 8420-8439 overlaps no other range
ok   range 8444-8457 overlaps no other range
ok   range 8458-8476 overlaps no other range
ok   range 8514-8556 overlaps no other range
ok   the 5 dropped lines outside the ranges are blank lines: [6880, 7835, 8029, 8040, 8557]
lines in StaticTests.cc now that the base does not have (4):
   + #include "SpireMechanics.hh"
   + #include "SpireTestSupport.hh"
   +   spire_mech_run_tests(); // SpireMechanics.cc: every registered Spire test file, once, in the order of their names
   +     auto first_room_of_w = spire_test_first_room_of_w(rewards); // SpireTestSupport.hh
ok   each of them stands once
ok   base (17467 lines) minus the 1618 named lines == working StaticTests.cc (15853 lines) minus those 4 added lines, line for line
lines of the base that were replaced, not moved:
   - 39:   void spire_seam_run_tests(); // SpireTestsSeam.cc: the seam's guard, first, on the files as they are in the repo
   - 40:   spire_seam_run_tests();
ok   move  5930- 6879 ( 950 lines, -0 indent) -> src/SpireTestsMags.cc:35-984  the run mag: step, bytes, merge, refusals, idempotence (whole block, with its label line)
ok   move  8041- 8351 ( 311 lines, -4 indent) -> src/SpireTestsForce.cc:39-349  kind 4: disk MST, budget, level, the two tables, offers, a line's own disk, the curse, the need with a disk
ok   move  7836- 7929 (  94 lines, -2 indent) -> src/SpireTestsAttack.cc:39-132  kind 1 and 2: every cell of the grid, rule B's need per cell, the new line
ok   move  7945- 8028 (  84 lines, -4 indent) -> src/SpireTestsDefence.cc:43-126  kind 5: unit offers, the worn frame and its four slots, the kit frame
ok   move  8514- 8556 (  43 lines, -2 indent) -> src/SpireTestsMags.cc:1042-1084  rule B in the ledger
ok   move  8374- 8409 (  36 lines, -4 indent) -> src/SpireTestsAttack.cc:134-169  kind 1 and 2: a force's own lines
ok   move  8352- 8373 (  22 lines, -4 indent) -> src/SpireTestsMags.cc:999-1020  rule B: the mag fits 200 with a disk's need
ok   move  8420- 8439 (  20 lines, -2 indent) -> src/SpireTestsDefence.cc:128-147  kind 5: the unit bonus, the free unit slot
ok   move  8458- 8476 (  19 lines, -2 indent) -> src/SpireTestsMags.cc:1022-1040  kind 3: mag food goes on the stat of the lean, through the ledger
ok   move  8444- 8457 (  14 lines, -2 indent) -> src/SpireTestsAttack.cc:171-184  kind 1: the fallback line without a known weapon
ok   move  7931- 7939 (   9 lines, -2 indent) -> src/SpireTestsMags.cc:989-997  kind 3: the mag food levels by tier
ok   move  7792- 7795 (   4 lines, -2 indent) -> src/SpireTestSupport.hh:16-19  helper: first room of every W
ok   move  7940- 7942 (   3 lines, -2 indent) -> src/SpireTestsDefence.cc:39-41  kind 5: the grid has units
ok   move  7741- 7741 (   1 lines, -2 indent) -> src/SpireTestsMags.cc:987-987  setup: the mag table
ok   move  7944- 7944 (   1 lines, -4 indent) -> src/SpireTestsDefence.cc:42-42  setup: the item parameter table
ok   copy  7741- 7741 (   1 lines, -2 indent) -> src/SpireTestsAttack.cc:36-36  setup: the mag table (second copy of a moved line)
ok   copy  7944- 7944 (   1 lines, -4 indent) -> src/SpireTestsForce.cc:37-37  setup: the item parameter table (second copy of a moved line)
stripped non-blank lines gone from StaticTests.cc: 1258 distinct, 1578 in all; not found often enough in the new files: 2
   gone 1x, in the new files 0x: void spire_seam_run_tests(); // SpireTestsSeam.cc: the seam's guard, first, on the files as they are in the repo
   gone 1x, in the new files 0x: spire_seam_run_tests();
ok   the only ones are the replaced lines above
src/SpireTestsAttack.cc: 189 lines, 145 moved, not moved (blank lines aside):
      1 = #include <phosg/Filesystem.hh>
      2   #include <phosg/JSON.hh>
      3   #include <phosg/Strings.hh>
      4 = #include <phosg/UnitTest.hh>
      6   #include <stdint.h>
      8 = #include <algorithm>
      9   #include <array>
     10 = #include <format>
     11   #include <functional>
     12   #include <initializer_list>
     13   #include <map>
     14   #include <memory>
     15   #include <optional>
     16   #include <set>
     17   #include <stdexcept>
     18   #include <string>
     19   #include <utility>
     20   #include <vector>
     22 = #include "SpireMag.hh"
     23 = #include "SpireMechanics.hh"
     24 = #include "SpireRewards.hh"
     25 = #include "SpireTestSupport.hh"
     27   // The tests of reward kind 1 (a weapon in the line held) and kind 2 (a weapon in a new line).
     28   // Moved literally out of run_static_tests() in StaticTests.cc (the seam, docs/review-loop/goals/naad.md point 1): the
     29   // text of every test below is what stood there, the indentation and this function around it aside. The owner of this
     30   // file is the attack mechanic (pso-roguelike new-features/roguelike/design/mechanics/integratie.md par. 5).
     32   namespace {
     34   void spire_tests_attack() {
     35 =   auto rewards = spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE));
     37     auto first_room_of_w = spire_test_first_room_of_w(rewards);
    185   }
    187   [[maybe_unused]] const bool registered = spire_mech_register_tests("attack", spire_tests_attack);
    189   } // namespace
src/SpireTestsDefence.cc: 152 lines, 108 moved, not moved (blank lines aside):
      1 = #include <phosg/Filesystem.hh>
      2   #include <phosg/JSON.hh>
      3   #include <phosg/Strings.hh>
      4 = #include <phosg/UnitTest.hh>
      6   #include <stdint.h>
      8 = #include <algorithm>
      9   #include <array>
     10 = #include <format>
     11   #include <functional>
     12   #include <initializer_list>
     13   #include <map>
     14   #include <memory>
     15   #include <optional>
     16   #include <set>
     17   #include <stdexcept>
     18   #include <string>
     19   #include <utility>
     20   #include <vector>
     22   #include "ItemParameterTable.hh"
     23   #include "PlayerInventory.hh"
     24 = #include "SpireEquipItem.hh"
     25 = #include "SpireFlagBridge.hh"
     26 = #include "SpireMechanics.hh"
     27 = #include "SpireRewards.hh"
     29   // The tests of reward kind 5 (a unit) and of the frame with four slots it goes into.
     30   // Moved literally out of run_static_tests() in StaticTests.cc (the seam, docs/review-loop/goals/naad.md point 1): the
     31   // text of every test below is what stood there, the indentation and this function around it aside. The owner of this
     32   // file is the defence mechanic (pso-roguelike new-features/roguelike/design/mechanics/integratie.md par. 5).
     34   namespace {
     36   void spire_tests_defence() {
     37 =   auto rewards = spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE));
    148   }
    150   [[maybe_unused]] const bool registered = spire_mech_register_tests("defence", spire_tests_defence);
    152   } // namespace
src/SpireTestsForce.cc: 354 lines, 312 moved, not moved (blank lines aside):
      1 = #include <phosg/Filesystem.hh>
      2   #include <phosg/JSON.hh>
      3   #include <phosg/Strings.hh>
      4 = #include <phosg/UnitTest.hh>
      6   #include <stdint.h>
      8 = #include <algorithm>
      9   #include <array>
     10 = #include <format>
     11   #include <functional>
     12   #include <initializer_list>
     13   #include <map>
     14   #include <memory>
     15   #include <optional>
     16   #include <set>
     17   #include <stdexcept>
     18   #include <string>
     19   #include <utility>
     20   #include <vector>
     22   #include "ItemParameterTable.hh"
     23   #include "LevelTable.hh"
     24 = #include "SpireMag.hh"
     25 = #include "SpireMechanics.hh"
     26 = #include "SpireRewards.hh"
     28   // The tests of reward kind 4 (a technique disk) and of the level the disk teaches.
     29   // Moved literally out of run_static_tests() in StaticTests.cc (the seam, docs/review-loop/goals/naad.md point 1): the
     30   // text of every test below is what stood there, the indentation and this function around it aside. The owner of this
     31   // file is the force mechanic (pso-roguelike new-features/roguelike/design/mechanics/integratie.md par. 5).
     33   namespace {
     35   void spire_tests_force() {
     36 =   auto rewards = spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE));
    350   }
    352   [[maybe_unused]] const bool registered = spire_mech_register_tests("force", spire_tests_force);
    354   } // namespace
src/SpireTestsMags.cc: 1089 lines, 1044 moved, not moved (blank lines aside):
      1 = #include <phosg/Filesystem.hh>
      2   #include <phosg/JSON.hh>
      3   #include <phosg/Strings.hh>
      4 = #include <phosg/UnitTest.hh>
      6   #include <stdint.h>
      8 = #include <algorithm>
      9   #include <array>
     10 = #include <format>
     11   #include <functional>
     12   #include <initializer_list>
     13   #include <map>
     14   #include <memory>
     15   #include <optional>
     16   #include <set>
     17   #include <stdexcept>
     18   #include <string>
     19   #include <utility>
     20   #include <vector>
     22 = #include "SpireFlagBridge.hh"
     23 = #include "SpireMag.hh"
     24 = #include "SpireMechanics.hh"
     25 = #include "SpireRewards.hh"
     27   // The tests of the run mag (SpireMag.*, rule B, the ledger) and of reward kind 3 (mag food).
     28   // Moved literally out of run_static_tests() in StaticTests.cc (the seam, docs/review-loop/goals/naad.md point 1): the
     29   // text of every test below is what stood there, the indentation and this function around it aside. The owner of this
     30   // file is the mags mechanic (pso-roguelike new-features/roguelike/design/mechanics/integratie.md par. 5).
     32   namespace {
     34   void spire_tests_mags() {
    986 =   auto rewards = spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE));
   1085   }
   1087   [[maybe_unused]] const bool registered = spire_mech_register_tests("mags", spire_tests_mags);
   1089   } // namespace
src/SpireTestSupport.hh: 21 lines, 4 moved, not moved (blank lines aside):
      1   #pragma once
      3   #include <stdint.h>
      5   #include <map>
      7 = #include "SpireRewards.hh"
      9   // What a test that stayed in run_static_tests() (StaticTests.cc) and a test that moved to its owner's file
     10   // (src/SpireTests*.cc) both need. One definition here, never a copy per file.
     12   // The first room of every W the grid gives (rewards.room_w is room index -> W). It stood as a local of the rewards
     13   // block in run_static_tests(); the grid test that stayed there and the per-cell weapon test in SpireTestsAttack.cc
     14   // both read it.
     15   inline std::map<uint8_t, uint32_t> spire_test_first_room_of_w(const SpireRewardTable& rewards) {
     20     return first_room_of_w;
     21   }
asserting lines (a line with expect...( or throw) / occurrences
   base StaticTests.cc               4653 /  4653
   now  StaticTests.cc               4054 /  4054
   now  src/SpireTestsAttack.cc         35 /    35
   now  src/SpireTestsDefence.cc        34 /    34
   now  src/SpireTestsForce.cc          87 /    87
   now  src/SpireTestsMags.cc          443 /   443
   now  src/SpireTestSupport.hh          0 /     0
   now  sum                          4653 /  4653
ok   the sum now equals the base
   the seam guard, counted apart: base 17 / 17, now 17 / 17
VERDICT: all checks ok
verify_exit=0
```

## Bewijs 3: de bouw neemt een nieuw `src/Spire*.cc` vanzelf mee

`CMakeLists.txt` noemt geen enkel Spire-bestand meer, en de zes nieuwe bronnen van deze fase zijn gebouwd. Bij de
eerste `make` na de wijziging configureerde make zelf opnieuw (uit dat log, 22:25: `-- Configuring done (0.0s)`,
daarna `Building CXX object CMakeFiles/newserv.dir/src/SpireMechanics.cc.o` en de vier testbestanden; dat log is
door de volgende bouw overschreven, de regels hieronder zijn daarna opnieuw gemeten).

```
$ grep -n "Spire" CMakeLists.txt
50:# Every Spire source, by its name: a new src/Spire*.cc builds without a line here. CONFIGURE_DEPENDS makes the build
52:file(GLOB SPIRE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/Spire*.cc)
$ ls build/CMakeFiles/newserv.dir/src/ | grep "^Spire"
SpireDraw.cc.o
SpireDraw.cc.o.d
SpireDrops.cc.o
SpireDrops.cc.o.d
SpireFlagBridge.cc.o
SpireFlagBridge.cc.o.d
SpireMag.cc.o
SpireMag.cc.o.d
SpireMechanics.cc.o
SpireMechanics.cc.o.d
SpireMeta.cc.o
SpireMeta.cc.o.d
SpireRewards.cc.o
SpireRewards.cc.o.d
SpireTestsAttack.cc.o
SpireTestsAttack.cc.o.d
SpireTestsDefence.cc.o
SpireTestsDefence.cc.o.d
SpireTestsForce.cc.o
SpireTestsForce.cc.o.d
SpireTestsMags.cc.o
SpireTestsMags.cc.o.d
SpireTestsSeam.cc.o
SpireTestsSeam.cc.o.d
$ grep -c "Spire" build/CMakeFiles/VerifyGlobs.cmake; grep -n "CONFIGURE_DEPENDS\|GLOB" build/CMakeFiles/VerifyGlobs.cmake | head -4
13
5:file(GLOB NEW_GLOB LIST_DIRECTORIES true "/home/m4rck/code/worktrees/newserv/beloningen-b/src/Spire*.cc")
6:set(OLD_GLOB
20:if(NOT "${NEW_GLOB}" STREQUAL "${OLD_GLOB}")
21:  message("-- GLOB mismatch!")
```

## Bewijs 4: alles draait

De laatste regels van de bouw (exit 0, nul waarschuwingen in het log):

```
[  5%] Building CXX object CMakeFiles/newserv.dir/src/SpireTestsMags.cc.o
[  5%] Building CXX object CMakeFiles/newserv.dir/src/SpireTestsAttack.cc.o
[  6%] Linking CXX executable newserv
[100%] Built target newserv
make: Leaving directory '/home/m4rck/code/worktrees/newserv/beloningen-b/build'
```

`build/newserv run-static-tests` (exit 0), de regels van de aangemelde testbestanden en de staart:

```
I 1505646 2026-11-16 22:30:22 - -- Spire test file: attack
I 1505646 2026-11-16 22:30:22 -    rewards: 1680 cells checked, 0 need mismatches, 90720 rule B mags checked
I 1505646 2026-11-16 22:30:22 - -- Spire test file: defence
I 1505646 2026-11-16 22:30:22 -    rewards: 25 units, 128 unit offers checked
I 1505646 2026-11-16 22:30:22 - -- Spire test file: force
I 1505646 2026-11-16 22:30:22 -    rewards: 7124 disk offers checked for 8 classes that learn techniques
I 1505646 2026-11-16 22:30:22 - -- Spire test file: mags
I 1505646 2026-11-16 22:30:22 - -- Spire run mag: step, bytes, merge, refusals, idempotence (F6, route B)
I 1505646 2026-11-16 22:30:22 - -- Spire test file: seam
I 1505646 2026-11-16 22:30:22 - -- Spire seam: the reward offers are as before the seam
I 1505646 2026-11-16 22:30:23 - -- Spire seam: a savestate with the run mag loads and is written back the same
...
I 1505646 2026-11-16 22:31:29 - [Game:7A03] Deleted
I 1505646 2026-11-16 22:31:29 - -- All static tests passed
exit=0
```

`sh tests/spire-follow-city.test.sh build/newserv`:

```
ok: partner floor 15, leader floor 14 -> warp
ok: spire-lives-check
exit=0
```

De vingerafdruk van N1 is groen: de wacht (`seam`) draaide en gooide niet, en geen bestand onder `tests/spire-seam/`
is gewijzigd.

## Bewijs 5: een verhuisde test bijt nog

In `src/SpireTestsForce.cc:42` de verwachte waarde `40u` in `41u` veranderd, gebouwd, gedraaid (exit 1):

```
I 1505445 2026-11-16 22:30:05 - -- Spire test file: defence
I 1505445 2026-11-16 22:30:05 -    rewards: 25 units, 128 unit offers checked
I 1505445 2026-11-16 22:30:05 - -- Spire test file: force
E 1505445 2026-11-16 22:30:05 - run-static-tests: failure at /home/m4rck/code/worktrees/newserv/beloningen-b/src/SpireTestsForce.cc:42: 41u != spire_reward_disk_mst(rewards.techs[0], 1)
```

Teruggezet uit een kopie (`cmp` gelijk), herbouwd; daarna de groene run van bewijs 4.

## Gezien, niet gebouwd

- Kwaliteit: het log van `run-static-tests` heeft 175 regels met `E` of `W` (tests die een weigering uitlokken). Ze
  zijn niet naast een run van `2cfb90c` gelegd. Wat het kost: een tweede werkplek met een volle bouw.
- Toekomstbestendigheid: de glob bouwt elk `src/Spire*.cc` dat in de map staat, ook een half geschreven bestand van
  een andere bouwer in een gedeelde werkplek of een achtergebleven kopie met zo'n naam. Waar: `CMakeLists.txt:52`.
- Toekomstbestendigheid: niemand somt de verwachte testbestanden op. Een testbestand dat uit de boom verdwijnt, neemt
  zijn tests zonder melding mee; alleen een lege lijst gooit. Dat is de keuze van het ontwerp (geen gedeelde lijst);
  de telling van bewijs 1 is de wacht bij een review.
- Codekwaliteit: de 14 meldingen van `clang-format` in de verhuisde tekst (zie Beslissingen).

## Bijlage: het script en de verhuislijst

`manifest.json`:

```json
{
  "base": "2cfb90c",
  "moves": [
    {"file": "src/SpireTestsMags.cc",    "from": 5930, "to": 6879, "dedent": 0, "what": "the run mag: step, bytes, merge, refusals, idempotence (whole block, with its label line)"},
    {"file": "src/SpireTestsMags.cc",    "from": 7741, "to": 7741, "dedent": 2, "what": "setup: the mag table"},
    {"file": "src/SpireTestSupport.hh",  "from": 7792, "to": 7795, "dedent": 2, "what": "helper: first room of every W"},
    {"file": "src/SpireTestsAttack.cc",  "from": 7836, "to": 7929, "dedent": 2, "what": "kind 1 and 2: every cell of the grid, rule B's need per cell, the new line"},
    {"file": "src/SpireTestsMags.cc",    "from": 7931, "to": 7939, "dedent": 2, "what": "kind 3: the mag food levels by tier"},
    {"file": "src/SpireTestsDefence.cc", "from": 7940, "to": 7942, "dedent": 2, "what": "kind 5: the grid has units"},
    {"file": "src/SpireTestsDefence.cc", "from": 7944, "to": 7944, "dedent": 4, "what": "setup: the item parameter table"},
    {"file": "src/SpireTestsDefence.cc", "from": 7945, "to": 8028, "dedent": 4, "what": "kind 5: unit offers, the worn frame and its four slots, the kit frame"},
    {"file": "src/SpireTestsForce.cc",   "from": 8041, "to": 8351, "dedent": 4, "what": "kind 4: disk MST, budget, level, the two tables, offers, a line's own disk, the curse, the need with a disk"},
    {"file": "src/SpireTestsMags.cc",    "from": 8352, "to": 8373, "dedent": 4, "what": "rule B: the mag fits 200 with a disk's need"},
    {"file": "src/SpireTestsAttack.cc",  "from": 8374, "to": 8409, "dedent": 4, "what": "kind 1 and 2: a force's own lines"},
    {"file": "src/SpireTestsDefence.cc", "from": 8420, "to": 8439, "dedent": 2, "what": "kind 5: the unit bonus, the free unit slot"},
    {"file": "src/SpireTestsAttack.cc",  "from": 8444, "to": 8457, "dedent": 2, "what": "kind 1: the fallback line without a known weapon"},
    {"file": "src/SpireTestsMags.cc",    "from": 8458, "to": 8476, "dedent": 2, "what": "kind 3: mag food goes on the stat of the lean, through the ledger"},
    {"file": "src/SpireTestsMags.cc",    "from": 8514, "to": 8556, "dedent": 2, "what": "rule B in the ledger"}
  ],
  "copies": [
    {"file": "src/SpireTestsAttack.cc", "from": 7741, "to": 7741, "dedent": 2, "what": "setup: the mag table (second copy of a moved line)"},
    {"file": "src/SpireTestsForce.cc",  "from": 7944, "to": 7944, "dedent": 4, "what": "setup: the item parameter table (second copy of a moved line)"}
  ],
  "dropped_blank": [6880, 7835, 8029, 8040, 8557],
  "replaced": [
    {"from": 39, "to": 40, "what": "the two seam lines, replaced by the one call of spire_mech_run_tests()"}
  ]
}
```

`verify.py` (naast `manifest.json` zetten, aanroepen met het pad van de werkplek):

```python
#!/usr/bin/env python3
# Proof for the seam N2: every line that left StaticTests.cc stands in a new file, and what in the new files is new.
# Reads the base from git and the working tree; trusts manifest.json for nothing it does not check itself.
import json, subprocess, sys, os, re
from collections import Counter
repo, S = sys.argv[1], os.path.dirname(os.path.abspath(__file__))
man = json.load(open(os.path.join(S, 'manifest.json')))
base = subprocess.check_output(['git', '-C', repo, 'show', man['base'] + ':src/StaticTests.cc'], text=True).split('\n')[:-1]
rd = lambda p: open(os.path.join(repo, p)).read().split('\n')[:-1]
work = rd('src/StaticTests.cc')
dests = ['src/SpireTestsAttack.cc', 'src/SpireTestsDefence.cc', 'src/SpireTestsForce.cc', 'src/SpireTestsMags.cc', 'src/SpireTestSupport.hh']
dest = {d: rd(d) for d in dests}
ok = True
def check(cond, text):
    global ok
    ok &= bool(cond)
    print(('ok   ' if cond else 'FAIL ') + text)

# 1. base minus the lines the manifest names == working tree minus its added lines
gone = set()
for m in man['moves']:
    r = set(range(m['from'], m['to'] + 1))
    check(not (gone & r), 'range %d-%d overlaps no other range' % (m['from'], m['to']))
    gone |= r
moved = set(gone)
blank = set(man['dropped_blank'])
check(all(base[n - 1].strip() == '' for n in blank), 'the %d dropped lines outside the ranges are blank lines: %s' % (len(blank), sorted(blank)))
repl = set()
for r in man['replaced']:
    repl |= set(range(r['from'], r['to'] + 1))
gone |= blank | repl
added = [l for l in work if l not in set(base)]
print('lines in StaticTests.cc now that the base does not have (%d):' % len(added))
for l in added:
    print('   + ' + l)
check(all(work.count(l) == 1 for l in added), 'each of them stands once')
rest_base = [l for n, l in enumerate(base, 1) if n not in gone]
rest_work = [l for l in work if l not in set(added)]
check(rest_base == rest_work, 'base (%d lines) minus the %d named lines == working StaticTests.cc (%d lines) minus those %d added lines, line for line'
      % (len(base), len(gone), len(work), len(added)))
print('lines of the base that were replaced, not moved:')
for n in sorted(repl):
    print('   - %d: %s' % (n, base[n - 1]))

# 2. every moved range stands contiguously in its file, equal but for the leading indentation
covered = {d: set() for d in dests}
def place(m, kind):
    lines = []
    for n in range(m['from'], m['to'] + 1):
        l = base[n - 1]
        lines.append('' if l.strip() == '' else l[m['dedent']:])
        if l.strip():
            assert l[:m['dedent']].strip() == '', n
    d = dest[m['file']]
    hits = [i for i in range(len(d) - len(lines) + 1) if d[i:i + len(lines)] == lines and not (covered[m['file']] & set(range(i, i + len(lines))))]
    check(len(hits) >= 1, '%s %5d-%5d (%4d lines, -%d indent) -> %s:%s  %s' % (kind, m['from'], m['to'], len(lines), m['dedent'], m['file'],
          ('%d-%d' % (hits[0] + 1, hits[0] + len(lines))) if hits else '?', m['what']))
    if hits:
        covered[m['file']] |= set(range(hits[0], hits[0] + len(lines)))
for m in sorted(man['moves'], key=lambda m: -(m['to'] - m['from'])):
    place(m, 'move')
for m in man['copies']:
    place(m, 'copy')

# 3. independent of the manifest: every non-blank line gone from StaticTests.cc, stripped, stands in a new file at least as often
cb, cw, cd = Counter(l.strip() for l in base), Counter(l.strip() for l in work), Counter()
for d in dests:
    cd.update(l.strip() for l in dest[d])
missing = {l: (cb[l] - cw[l], cd[l]) for l in cb if l and cb[l] - cw[l] > cd[l]}
print('stripped non-blank lines gone from StaticTests.cc: %d distinct, %d in all; not found often enough in the new files: %d'
      % (sum(1 for l in cb if l and cb[l] > cw[l]), sum(cb[l] - cw[l] for l in cb if l and cb[l] > cw[l]), len(missing)))
for l, (g, h) in missing.items():
    print('   gone %dx, in the new files %dx: %s' % (g, h, l))
check(set(missing) <= set(base[n - 1].strip() for n in repl), 'the only ones are the replaced lines above')

# 4. what in the new files does not come from StaticTests.cc
workset = set(l.strip() for l in work)
for d in dests:
    print('%s: %d lines, %d moved, not moved (blank lines aside):' % (d, len(dest[d]), len(covered[d])))
    for i, l in enumerate(dest[d]):
        if i not in covered[d] and l.strip():
            print('   %4d %s %s' % (i + 1, '=' if l.strip() in workset and l.strip() not in ('}', '{') else ' ', l))

# 5. the count of asserting lines
pat = re.compile(r'\bexpect[_a-z]*\(|\bthrow\b')
def count(lines):
    return sum(1 for l in lines if pat.search(l)), sum(len(pat.findall(l)) for l in lines)
b = count(base)
w = count(work)
print('asserting lines (a line with expect...( or throw) / occurrences')
print('   base StaticTests.cc              %5d / %5d' % b)
print('   now  StaticTests.cc              %5d / %5d' % w)
tl, to = w
for d in dests:
    c = count(dest[d])
    tl += c[0]; to += c[1]
    print('   now  %-28s %5d / %5d' % (d, c[0], c[1]))
print('   now  sum                         %5d / %5d' % (tl, to))
check((tl, to) == b, 'the sum now equals the base')
seam_b = subprocess.check_output(['git', '-C', repo, 'show', man['base'] + ':src/SpireTestsSeam.cc'], text=True).split('\n')[:-1]
print('   the seam guard, counted apart: base %d / %d, now %d / %d' % (count(seam_b) + count(rd('src/SpireTestsSeam.cc'))))
print('VERDICT: ' + ('all checks ok' if ok else 'A CHECK FAILED'))
sys.exit(0 if ok else 1)
```
