# Review-loop naad, ronde 1, opus (invalshoek: doel)

## Maatstaf en scope

Maatstaf: het doel `docs/review-loop/goals/naad.md`. Elke mechaniek bouwt in eigen bestanden en meldt zich zelf aan,
zonder gedragswijziging, volgens de zeven punten en met de zes bewijzen. Gelezen:

- newserv `git diff c786164 a80984e` (tak feature/beloningen-b; de questrepo op 86a8b107);
- de questrepo vanaf adaac25a. Daar zijn de scriptwijzigingen per commit gelezen: rewards.py, make_spire_rewards.py,
  data/grid, pc-build.sh, apply-on-pc.sh, apply-target.sh, terugdraai*.sh, check_deploy_sync.py, check_spire_drops.py
  en de twee testscripts.

Niet beoordeeld, buiten scope: nieuw mechaniekgedrag, de ontwerpkeuzes, de vorm van de verhuisde tests, de verhuisde
code van soort 1-5 zelf, balans en constanten, en de uitrol op pc of lab.

## Per punt van het doel: gehaald of niet, met bewijs

1. **Gehaald, na B1.** CMake globt `src/Spire*.cc` (`CMakeLists.txt:52`, `file(GLOB SPIRE_SOURCES CONFIGURE_DEPENDS
   ${CMAKE_SOURCE_DIR}/src/Spire*.cc)`). `StaticTests.cc:41` roept `spire_mech_run_tests()` één keer aan. De tests
   melden zich aan als attack, defence, force, mags, mechanics en seam. De verhuizing is letterlijk: het aantal
   toetsende regels over alle testbestanden is 4839 vóór en na de fix. Twee parsertests van soort 4 en 5 stonden
   nog in het gedeelde bestand; zie B1.
2. **Gehaald.** `SpireMechanics.cc/.hh` bevat de soortentabel, H1-H11 als aanmeldlijsten en de schakelaarlezer.
   `SpireTestsMechanics.cc:898-966` toetst de lezer:
   - zonder bestand staat alles aan;
   - een ontbrekende sleutel staat aan;
   - drie soorten onleesbaar bestand laten de laatste goede stand staan;
   - een weggehaald bestand zet alles weer aan;
   - het bestand wordt hoogstens één keer per seconde bekeken.
3. **Gehaald.** Soort 1-5 staan in SpireRewardWeapon, MagFood, Disk en Unit.
   - `spire_reward_kind_known` kent 1-8. Soort 6-8 zijn niet aangemeld en geven "reward kind N is not registered".
     De pick valt dan terug op het eigen item van de entry (`SpireTestsMechanics.cc:876-878`).
   - Per soort zijn pick en result gelijk aan c786164, inclusief de volgorde van de rnd-aanroepen en de equip_seq
     alleen bij wapens.
   - De 40 weggehaalde kernregels die niet letterlijk terugkomen zijn twee soorten wijziging. Ten eerste een
     weggevallen `static`. Ten tweede een `if (kind == ...)` die nu via de tabel naar de soort gaat. Beide zijn
     bedoeld.
4. **Gehaald.**
   - De run-mag-sleutel staat letterlijk in `SpireMagState.cc` op H10. Die is zonder schakelaar, zodat de sleutel
     nooit verdwijnt.
   - H11 staat in `SpireMeta.cc`.
   - `carried` bewaart de sleutels van een uitgeschakelde aanmelder.
5. **Gehaald.**
   - rewards.py kent focus 6, mana 7 en pantser 8, met focus als zeldzame serverlijn.
   - make_spire_rewards.py leest `data/grid/*.json` en schrijft ingesprongen.
   - `data/schakelaars.json` is `{}`.
6. **Gehaald.**
   - De uitrol neemt `system/spire-*.json`, de `spire-*/`-mappen van de pin en het schakelaarbestand.
   - Namen met spire-bridge, draw, heat, rooms en backup worden geweigerd.
   - `terugdraai-target.sh` slaat `system/spire-bridge*` en `system/spire-backup*` over (`not_ours`). Geen pad raakt
     iets in spire-bridge dat het zelf niet neerzette.
   - `test_apply_rollout_order.sh`: 44 gevallen ok.
7. **Gehaald.** `.claude/review-loop.json` leidt de paden af uit `git rev-parse --show-toplevel` en de naam van de
   werkplek.

De zes bewijzen:

- **Aanbod-snapshot.** De statische tests slagen tegen `tests/spire-seam/offers.fingerprint`. Kind1-5, edges en pick
  staan zoals eerst gemeten. Shared en total zijn opnieuw gemeten op de oude code, zonder de Source-tekst. Die
  hermeting op c786164 heb ik niet zelf herhaald; dat is de resterende onzekerheid.
- **Savestate.** De zes fixtures in SpireTestsSeam gaan heen en terug onveranderd, binnen "All static tests passed".
- **Grid.** Als JSON gelijk aan c786164, op twee beschrijvende sleutels na:
  `top-level keys differing: ['Source', '_what']`. `--check` geeft OK.
- **Questbouw.** Byte-gelijk.
  - Alle 24 `.bin` en `.dat` en nog 59 andere bestanden (83 van 89) zijn gelijk aan `build-naad/md5-voor-ladder.txt`.
  - De zes `opcodes-used.md` verschillen alleen in de regel `Bron:`, het pad van de tijdelijke map. De inhoud van
    QuestScript.cc is gelijk aan die van de pin: md5 3988a6ec in beide.
  - Met het pin-pad in die regel zijn alle zes gelijk aan de nulmeting.
  - q929: de bijgehouden artefacten zijn ongewijzigd (`git status` toont niets).
- **Geen schakelaarbestand.** Zonder bestand geeft elke soort het aanbod van vóór, op de toets in
  `SpireTestsMechanics.cc:899-908`. Het geleverde bestand is `{}`.
- **Bestaande tests.** Alle tests zijn groen; zie de verificatie hieronder.

## Bevindingen

**B1 (klein, raakt het doel, gefixt in newserv 4b8d732).** Twee laadweigeringen van de eigen gridsectie van een soort
stonden nog in het gedeelde `StaticTests.cc`:

- een Units-item dat geen unit is (soort 5, `SpireRewardUnit.cc:43`);
- een schijflevel boven 30 (soort 4, `SpireRewardDisk.cc:96`).

De naad laat elke soort zijn eigen sectie lezen. Het bouwrapport N2 noemt deze twee zelf: "zodra elke soort zijn eigen
sectie leest, hoort hij bij verdediging" (twijfelgevallen 2 en 3). Een mechaniek die zijn sectie wijzigt, moest dus
nog het gedeelde bestand aanpassen, en dat wil de naad juist voorkomen. Beide blokken zijn letterlijk verhuisd naar
`SpireTestsDefence.cc` en `SpireTestsForce.cc`, samen met de include van `SpireTestSupport.hh`. Bewijs dat er niets
wegviel: 4839 toetsende regels vóór en na.

Opmerkingen, geen defect:

- De pin van pc-build.sh (eca75dd) loopt achter op HEAD. Opnieuw pinnen hoort na de review, vóór de pc-build.
- H8 vuurt alleen bij een niet-lege 'W'-tekst. Een latere aanmelder op H8 moet dat weten.
- De snapshot dekt het live schijfpad niet (de schijfstand uit de run-kopie). De end-to-end-tests in StaticTests.cc
  dekken het wel, en die zijn groen.

## Mutanten (alleen als je er draaide, letterlijk)

Geen gedraaid. B1 is een letterlijke testverhuizing. De gelijke telling en de groene run tonen dat de tests nog
draaien.

## Verificatie na de fixes (letterlijk)

Netstroom: `cat /sys/class/power_supply/A*/online` gaf `1`. Alles is gedraaid op 4b8d732.

```
[  4%] Building CXX object CMakeFiles/newserv.dir/src/SpireTestsForce.cc.o
[  4%] Building CXX object CMakeFiles/newserv.dir/src/StaticTests.cc.o
[  5%] Linking CXX executable newserv
[100%] Built target newserv
make: Leaving directory '/home/m4rck/code/worktrees/newserv/beloningen-b/build'
I 1719302 2026-11-17 00:35:12 - [C-BC] Deleted
I 1719302 2026-11-17 00:35:12 - [Game:7A03] Deleted
I 1719302 2026-11-17 00:35:12 - -- All static tests passed
ok: empty Characters refused
ok: spire-lives-check
```

```
OK: /home/m4rck/code/worktrees/newserv/beloningen-b/system/spire-rewards.json matches the grid sources
exit=0
```

```
rollout order test: 44 cases, all ok
test_build_reuse: 17 cases, every changed map is built again; a change of only the switch file keeps the build key
```

`build.sh --ladder` gaf `exit=0` met als laatste regels:

```
c952d2a44919bc8ecf1a3c332eb8dd6c  build-ladder/q985-gc-e.bin
f14c45941b095bce4dc7f85340b65c13  build-ladder/q986-gc-e.bin
2c937252d26dfbe13fe63c8c0698dbe1  build-ladder/q981-gc.dat
2c937252d26dfbe13fe63c8c0698dbe1  build-ladder/q982-gc.dat
2c937252d26dfbe13fe63c8c0698dbe1  build-ladder/q983-gc.dat
06e1b9574bd3501b96ca04cb4acb740a  build-ladder/q984-gc.dat
06e1b9574bd3501b96ca04cb4acb740a  build-ladder/q985-gc.dat
06e1b9574bd3501b96ca04cb4acb740a  build-ladder/q986-gc.dat
```

Vergelijking met `build-naad/md5-voor-ladder.txt` (89 regels):

```
build-ladder/q981/opcodes-used.md: FAILED
...
build-ladder/q986/opcodes-used.md: FAILED
md5sum: WARNING: 6 computed checksums did NOT match
```

Met het pin-pad in de regel `Bron:`:

```
q981 want=5bc733819f5061a6b584b5531c5f4f54 got=5bc733819f5061a6b584b5531c5f4f54 equal
q982 want=2f527a27cc55d811d1b7ef0a36aa95db got=2f527a27cc55d811d1b7ef0a36aa95db equal
q983 want=5e046150380ca9a9f4ae5f5b01287101 got=5e046150380ca9a9f4ae5f5b01287101 equal
q984 want=ffe421bce3d9b024acf872b1f9afafdd got=ffe421bce3d9b024acf872b1f9afafdd equal
q985 want=ffe421bce3d9b024acf872b1f9afafdd got=ffe421bce3d9b024acf872b1f9afafdd equal
q986 want=4b2b4ec2dce25fe41731b4dd8a0d8ad5 got=4b2b4ec2dce25fe41731b4dd8a0d8ad5 equal
```

## Wat ik niet heb aangeraakt

- De questrepo. Daar was geen bevinding. Ook niet de niet-gecommitte bestanden van een ander: `overzicht.html`,
  `E02-rewards/status.json` en `quests/spire/build-naad/`.
- De verhuisde code van soort 1-5 en de ontwerpkeuzes.
- De pin in pc-build.sh.
- Geen pc-build en geen uitrol. Niets op de pc of in het lab, en niets aan de draaiende newserv op de laptop.
- Geen push.

## Stopadvies

Stoppen. Alle zeven punten en alle zes bewijzen zijn gehaald. De enige bevinding was klein en is gefixt. Wat
overblijft is de vaste route: de pin van pc-build.sh naar de HEAD na de review, daarna de pc-build en
`apply-on-pc.sh` bij nul spelers, met `terugdraai.sh` klaar.
