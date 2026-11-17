# Koude review naad-testregel, ronde 1, opus (invalshoek: doel)

## Maatstaf en scope

Maatstaf: het doel in `docs/review-loop/goals/naad-testregel.md`, punten 1 tot 4. Ik heb `git diff de69fad HEAD`
beoordeeld (commits 362e88f, 7d95e2a en cddc50a), en de geraakte bestanden helemaal gelezen: `src/SpireMechanics.hh`,
`src/SpireMechanics.cc`, `src/SpireTestsMechanics.cc`, de aanroep in `src/SpireFlagBridge.cc` (`spire_reward_make_offer`),
`src/StaticTests.cc` (`run_static_tests`) en `src/Main.cc` (`run-static-tests`). Ik heb ook de code gelezen die deze
bestanden aanroept of door hen wordt aangeroepen: `spire_reward_offer` en `spire_rewards_parse` (`SpireRewards.cc`), pick en
result in de bridge, de registraties in productie (`SpireMagState.cc`, `SpireReward*.cc`) en de vingerafdruk in
`SpireTestsSeam.cc`. Het ontwerp waar punt 3 uit komt: `leveling.md` op `docs/ontwerp-mechanics` ("de kansen houden k",
"De zeldzaamheid blijft op kamer en trede").

Mijn sterkste bewijs is één gerichte proefrun. Ik heb tijdelijk een echte mechaniek toegevoegd die zich aanmeldt vanuit
een statische initialiser in een eigen bestand (achteraan in `src/SpireRewardUnit.cc`, daarna hersteld met
`git checkout --`). Die mechaniek staat standaard aan met een schakelaar, en meldt zich aan op H1, H2 (kamer +3, trede 0),
H3 (gewicht 7 voor oneven kandidaten), H5, H8, H10 (sleutel `ProbeKey`), H11 (sleutel `ProbeMeta`) en als eerste route
vóór de soorten 1 tot 5. Een eigen testbestand `zz.probe` toonde dat de proef buiten de zwijgstand werkt en in de
zwijgstand zwijgt. Hiermee meet ik de situatie die het doel beschrijft ("de eerste echte aanmelding"). De tests in het
bestand zelf spelen die situatie alleen na.

## Per punt van het doel: gehaald of niet, met bewijs

1. **De toetsen tellen geen aanmeldingen meer: gehaald na B1.** De tellingen zijn weg: "alleen mags op H10",
   `5u == spire_mech_kinds().size()` (twee keer), "geen eerste route" en "6 tot 8 niet aangemeld". Daarvoor in de plaats
   staan `SeamSnapshot` (aan het begin en het eind van `mechanics` en `mechanics.after`) en `expect_seam_invariants`
   (geen conflicten, geen dubbele naam per haak of soort, elke soort bekend, `mags` precies één keer op H10). Op één plek
   bleef het testbestand rood bij een echte aanmelding: het nieuwe zwijgstand-blok vergeleek buiten de zwijgstand de hele
   uitkomst van H3 (B1). Na de fix is de volledige statische run groen met de proef erbij (zie hieronder).
2. **Tijdens de statische tests zwijgen de aanmeldingen van anderen: gehaald.** `hook_on` en `spire_mech_kind(…, true)`
   vragen `silenced()`. Een aanmelding zonder schakelaar, een aanmelding met `by_test` (gedaan terwijl de zwijgstand aan
   stond) en een sleutel uit `spire_mech_test_only` gaan erdoor. De soorten zelf blijven aan. In productie staat de
   zwijgstand nooit aan: `tests_silence` is een `bool` met constante initialisatie op `false`. Alleen
   `run_static_tests()` (StaticTests.cc:41) zet hem op `true`, en `run_static_tests` heeft maar één aanroeper, de actie
   `run-static-tests` (Main.cc:269-273). `spire_mech_test_only` gooit buiten de zwijgstand. Getoetst door
   `expect(!silence_at_start)` (gelezen in een statische initialiser) en `expect(spire_mech_tests_silence())`. In de
   proefrun werd de proef in totaal 9 keer aangeroepen (`-- REVIEW PROBE calls in total: 9`). Dat zijn alleen de 3
   aanroepen buiten de zwijgstand in `zz.probe` en de 6 in de `OutsideSilence`-blokken van `mechanics`. Tijdens alle
   andere tests, ook de vingerafdruk en heel StaticTests.cc, werd hij niet aangeroepen.
3. **De kansen blijven op de kamer van de quest: gehaald.** SpireFlagBridge.cc:4367 roept nu
   `spire_reward_odds(table, spire_reward_room_index(k), trede, …)` aan. De kamer van H2 (`room_index`) gaat alleen naar
   `spire_reward_offer` (het aanbod zelf), de trede van H2 gaat naar de kansen. Vóór de naad (039b8d0) kregen de kansen
   `room_index = spire_reward_room_index(k)`. Zonder aanmelder op H2 is de waarde dus dezelfde. Het commentaar bij H2
   (SpireMechanics.hh:238-240) en bij de aanroep zegt dit. De test via de bridge (SpireTestsMechanics.cc:502-607) houdt
   kamer en kansen uit elkaar: kamer 6 naar 2 geeft Legendary met het aanbod van kamer 2, kamer 3 naar 9 geeft geen
   Legendary met het aanbod van kamer 9, en trede 0 geeft geen Legendary in kamer 6. Met de oude aanroep op `room_index`
   zou de eerste regel "lower room 2" geven in plaats van "legendary room 2" (kamer 2 < LegendaryRoom 5).
4. **De vingerafdruk en de fixtures blijven groen: gehaald.** `git diff --stat de69fad HEAD -- tests/` is leeg. De test
   `-- Spire seam: the reward offers are as before the seam` slaagt in de eindverificatie, en ook in de proefrun met een
   mechaniek op H2, H3 en de eerste routes vóór 1 tot 5. Ik heb niet elke tussencommit apart gebouwd. Dat elke commit
   groen is, leid ik af uit wat hij wijzigt: 362e88f raakt alleen de tests plus één leesfunctie (`spire_mech_kind_names`),
   7d95e2a verandert niets zolang niemand met een schakelaar zich aanmeldt (de run-mag heeft geen schakelaar), en
   cddc50a geeft dezelfde waarde zonder aanmelder op H2.

## Bevindingen

**B1 (minor, raakt het doel, alleen een testfix): het nieuwe zwijgstand-blok gaat rood bij de eerste echte
mechaniek op H3.**
In SpireTestsMechanics.cc:1080 (op HEAD vóór de fix) stond, binnen `OutsideSilence`:
`expect_eq(std::string("LLLL 2 4 4"), weighed());`. Buiten de zwijgstand wordt, zoals op de server, elke echte mechaniek
aangeroepen. Een gewicht dat niet 1 is (de hunter-, ranger- en force-types staan op H3, `SPIRE_MECH_ORDER_*_TYPES`)
verandert de getrokken index en de som. Daarmee wordt het testbestand van de naad rood door een aanmelding die niet van
hem is, en dat is precies wat punt 1 moest weghalen.
Bewijs, de proefrun met de mechaniek hierboven:
```
E 1768514 2026-11-17 01:20:24 - run-static-tests: failure at /home/m4rck/code/worktrees/newserv/beloningen-b/src/SpireTestsMechanics.cc:1080: std::string("LLLL 2 4 4") != weighed()
```
Fix (194a1fe): buiten de zwijgstand vergelijkt het blok alleen de eigen aanroepen van de testaanmelder:
```
      weighed();
      expect_eq(std::string("LLLL"), hook_calls);
```
`died()` ("EL") en `run_state_read()` (alleen `TestL`) lezen al alleen wat van de test zelf is. De toetsen binnen de
zwijgstand (`" 2 4 0"` en de rest) zijn ongewijzigd: daar zwijgt een echte mechaniek. Na de fix, mét de proef:
```
I 1768885 2026-11-17 01:22:10 - -- All static tests passed
```
De toetsen aan H3 buiten de zwijgstand die deze test eerder deed, staan al in `expect_hook` (in de zwijgstand, met de
eigen aanmelders van de test).

## Mutanten (alleen als je er draaide, letterlijk)

Geen mutanten. De proef is geen breuk van de code onder review. Het is een tijdelijke aanmelding in een bestand dat de
wijziging niet raakt, en die heb ik hersteld:
```
$ git checkout -- src/SpireRewardUnit.cc && cmp src/SpireRewardUnit.cc /tmp/review-loop/SpireRewardUnit.cc.orig && echo restored
restored
```
Wat de proef liet zien (`zz.probe`, eerst buiten en dan in de zwijgstand):
```
-- REVIEW PROBE H2 moved the room: true (want true)
-- REVIEW PROBE route before kind 5 asked: true (want true)
-- REVIEW PROBE route before kind 1 gave the offer: true (want true)
-- REVIEW PROBE H10 wrote its key: true (want true)
-- REVIEW PROBE H2 moved the room: false (want false)
-- REVIEW PROBE route before kind 5 asked: false (want false)
-- REVIEW PROBE route before kind 1 gave the offer: false (want false)
-- REVIEW PROBE H10 wrote its key: false (want false)
-- REVIEW PROBE calls in total: 9
```

## Verificatie na de fixes (letterlijk)

Laptop aan de lader (`cat /sys/class/power_supply/A*/online` gaf `1`). Gebouwd op 194a1fe, zonder de proef
(SpireRewardUnit.cc wordt opnieuw gecompileerd en SpireTestsMechanics.cc met de fix al in de bouw daarvoor):
```
[  2%] Building CXX object CMakeFiles/newserv.dir/src/Revision.cc.o
[  2%] Building CXX object CMakeFiles/newserv.dir/src/SpireRewardUnit.cc.o
[  3%] Linking CXX executable newserv
[100%] Built target newserv
make: Leaving directory '/home/m4rck/code/worktrees/newserv/beloningen-b/build'
I 1769832 2026-11-17 01:24:13 - [C-BD] Deleted
I 1769832 2026-11-17 01:24:13 - [Game:7A03] Deleted
I 1769832 2026-11-17 01:24:13 - -- All static tests passed
ok: empty Characters refused
ok: spire-lives-check
```
Grid:
```
OK: /home/m4rck/code/worktrees/newserv/beloningen-b/system/spire-rewards.json matches the grid sources
```
De quest-repo is niet geraakt, dus de uitrolscripts en de questbouw heb ik niet gedraaid. `git status --short` is leeg
na de fix-commit. `git diff --stat de69fad HEAD -- tests/` is leeg.

## Wat ik niet heb aangeraakt

- De quest-repo, de uitrolscripts, de pc, het lab en de draaiende newserv op de laptop.
- De ontwerpkeuzes: dat de soorten zelf aan blijven in de zwijgstand, dat een aanmelder zonder schakelaar altijd wordt
  aangeroepen, en wat H2 teruggeeft. Ik zag dat een eerste route met een eigen `parse` in de zwijgstand nog steeds
  geparsed wordt (`spire_mech_kinds()` geeft ook een route die zwijgt). Dat is hetzelfde gedrag als bij een schakelaar die
  uit staat ("whatever its switch says"), en de vingerafdruk dekt alleen de soorten 0 tot 5. Daarom is dit geen
  bevinding.
- Ik heb niet elke tussencommit apart gebouwd (zie punt 4).
- Ik heb geen eerdere reviewverslagen gelezen.

## Stopadvies

Stoppen. De vier punten zijn gehaald. Het ene gat (B1) zat in de nieuwe test zelf, is met een testfix gedicht, en is
gemeten met een echte aanmelding op alle haken waar een mechaniek de tests van anderen kan verschuiven. Productiecode is
in deze ronde niet gewijzigd. Een volgende ronde zou dezelfde code opnieuw lezen zonder nieuw risico.
