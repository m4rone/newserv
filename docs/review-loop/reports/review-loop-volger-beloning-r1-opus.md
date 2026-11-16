# Review-loop volger-beloning, ronde 1: opus (invalshoek doel)

Reviewer: Opus 5.5, koud: ik heb deze wijziging niet gebouwd. Eerdere review-rapporten heb ik niet gelezen voordat
mijn oordeel vaststond en de fixes gecommit waren.

## Maatstaf en scope

Het doel bestaat uit twee delen.

1. **De beloning van de volger verdwijnt niet meer zonder woord** (B-REGIE-22-48, B-E02-16). Neemt de leider een warp
   terwijl de volger nog niet koos, dan krijgt de volger de eerste gewone beloning uit zijn lijst, of de bonus van
   Overslaan, en de server zegt dat in één regel. Een zeldzame, een vervloekte en een serverwapen worden nooit
   automatisch genomen. De vaste grenzen zijn: geen itemverlies, geen dubbele gift, geen vastloper en geen item maken
   op een client terwijl daar een golf loopt (B-REGIE-14-10).
2. **Eigen herstartscripts herstarten de pc-newserv niet terwijl er iemand speelt.** CLAUDE.md: "never restarted or
   reconfigured while a game is running (check the journal first)".

Gereviewd:

- newserv `git diff 9c8f2a6 HEAD` op `feature/klein-naast-a` (92fa883, ec9a239);
- de quest-repo `git diff 0d14b8db HEAD` op `feature/klein-naast-a` (80c8c387, df8be3be).

Ik las de diff volledig, met de code die hij aanroept en de code die hem aanroept:

- `rewards.py`: give_<id>, pd_ok/pd_taken, skip_bonus, exam_got, auto_plain;
- `make_spire.py`: mirror_phase0, de floor-handler van Sega's teleporter (floor<f>_sega_from_k), on_boss_floor, t_ui,
  prompt_tick, restart_threads;
- `SpireFlagBridge.cc`: spire_bridge_handle_register, spire_bridge_text_context_for, spire_bridge_message_text;
- `newserv-svc.sh` (svc_count_clients) en de spire-gates.

Elke dragende claim kreeg een mutant. Elke mutant is teruggezet met `git checkout --` of uit een kopie van de vaste stand.

Buiten scope: deel A en B van de beloningen zelf, de pijp van de leider zonder te kiezen, uitrol en herstarts, en
`~/pso-tools/quests/apply-yasha-on-pc.sh`.

## Wat er goed staat (getoetst, niet aangenomen)

- **Een zeldzame wordt nooit automatisch genomen.** Mutant M1 (de rare-test uit `auto_plain`) wordt gevangen door T2.
  Dat kwam in ronde 1 alleen via het venster van FO's `cx_r12_merge`; T6 vangt het nu ook rechtstreeks.
- **Een serverwapen wordt nooit automatisch genomen.** M3 (de srv-test weg) wordt gevangen door T2: "reaches the run-mag
  block e08:flow:mag_busy".
- **Pauze A is beslist na een automatische gift.** M9 (`leti r153, 3` weg) wordt gevangen door T3.
- **De routine zelf.** Ze is één rechte lijn zonder yield, met één aanroep, achter de kloktest. De selftest van
  check_auto_take vangt zijn drie mutanten: een sync in de routine, de kloktest weg, de aanroep weg.
- **De newserv-tekst van 'C' 19.** De vier directe tests op `spire_bridge_message_text` zijn groen. Index + 1 geeft
  "Lavis Cannon". 0 geeft de bonus van Overslaan. Zonder tabel, of met 0x7F, komt de algemene regel.
- **e00 telt clients vóór het kopiëren en opnieuw in de ssh-sessie die herstart.** De case "a client between the copy
  and the restart" van mijn nieuwe test is ook op de originele e00 groen: exit 3, gekopieerd, geen herstart.
- **Het oude pakket weigert bij de uitrol op de echte boom.** Ook dat is groen op de originele `apply-on-pc.sh`:
  exit 3, niets veranderd, geen systemctl.
- **lab-pc.** `lab-pc/run.sh` raakt alleen de labkopie; `lab-pc/README.md` is alleen documentatie.
- **De basismeting vóór mijn wijzigingen was volledig groen.**
  - cmake 0 en make 0.
  - De static tests: "All static tests passed".
  - follow-city 0.
  - De quest-build 0: 35 selftest-gates, .bin + .dat 241118 van 245000 bytes. Labels: rewards 768/768, coop 256/256,
    path 256/256, overflow 576/768.
  - De grid: "OK".

## Mutanten (letterlijk)

Ronde 1, op de stand van df8be3be. Het gereedschap is één q920-serverbuild plus check_auto_take `--selftest` en
check_r248. Het script staat in `/tmp/review-loop-klein/mutants.sh`.

```
== M1-rare-auto:
   ERROR ...: T2 auto_take reaches 'window_msg ... "<color 6>Cursed Chest<color 0>: you also\\nge' in e08:rewards:cx_r12_merge
== M2-cursed-auto:
   auto take: one straight stretch, called once from the travel handler, 1 source(s) checked, 0 without the routine; selftest 3 mutations caught
   check_auto_take rc=0
== M3-server-auto:
   ERROR ...: T2 auto_take reaches the run-mag block e08:flow:mag_busy
   check_auto_take rc=1
== M4-any-phase:              check_auto_take rc=0   (alle gates groen)
== M5-no-dialog-wait:         check_auto_take rc=0
== M6-asked-list-kept:        check_auto_take rc=0
== M12-full-pack-told-taken:  check_auto_take rc=0
== M13-stale-RES:             check_auto_take rc=0
== M8-nothing-drawn:          check_auto_take rc=0
== M9-not-decided:
   ERROR ...: T3 auto_take never decides pause A (leti r153, 3)
   check_auto_take rc=1
== M14-boss-floor-skip-gone:  check_auto_take rc=0
```

In newserv haalde mutant N1 text point 19 uit beide voorwaarden van `spire_bridge_text_context_for`. Dan komt er geen
naam meer in de regel. De static tests bleven groen: "All static tests passed".

Ronde 2, na de fixes, op dezelfde build (`/tmp/review-loop-klein/mutants2.sh`):

```
== M2-cursed-auto:
   auto take: one straight stretch, ... selftest 12 mutations caught
   check_auto_take rc=0
== M4-any-phase:
   ERROR ...: T5 the run start (phase 0): the travel handler ends in call, want end
   ERROR ...: T5 in the room: the travel handler ends in call, want end
   ERROR ...: T5 in the fight: the travel handler ends in call, want end
== M5-no-dialog-wait:
   ERROR ...: T5 the list open: the travel handler ends in call, want wait
   ERROR ...: T5 the shop open: the travel handler ends in call, want wait
== M6-asked-list-kept:
   ERROR ...: T5 a list only asked for: auto_take is called with r154 = 0 and r103 = 6, want 0 and 0
== M12-full-pack-told-taken:
   ERROR ...: T3 a give of auto_take that did not fit (r72 = FFFFFFFF) is not sent to the next slot
== M13-stale-RES:
   ERROR ...: T3 the archetype switch_call of auto_take is not right after "leti r72, 0"
== M8-nothing-drawn:
   ERROR ...: T3 the head of auto_take does not return when the room drew nothing (jmpi_eq r181, 0)
== M15-boss-room-test-gone:
   ERROR ...: T5 out of boss room k4: the travel handler ends in call, want end   (ook k8, k11, k15)
== M16-arena-skipped:
   ERROR ...: T5 in the arena of k4 (floor 11) from k3: the travel handler does not call auto_take (end)   (ook k8, k11, k15)
== M17-arena-item:
   ERROR ...: T5 in the arena of k4 (floor 11) from k3: auto_take is called with r154 = 0 and r103 = 0, want 1 and 0   (ook k8, k11, k15)
```

M2 is een equivalente mutant op de huidige data. Alle vier vervloekte entries zijn ook zeldzaam (`z == 'R'`): HU H20,
HU H27, RA R28 en FO F33. De rare-test houdt ze dus al buiten. T2 vangt een vervloekte entry nu wel via de id-lijst
(rares plus vloek uit `data/rewards.json`), maar dat is met deze data niet los te bewijzen.

N1 na de fix (`git checkout -- src/SpireFlagBridge.cc` daarna):

```
static rc=1
E ... GUARD E2E (review r3: a reward weapon and mag food through the bridge; rule B's mag meets the weapon before its equip) FAILED: the server did not get to the auto take's line with the entry's name within 15 seconds
```

## Bevindingen

### B1 (major, doel 1, productiecode): de beloning van de kamer vóór elke baas verdween nog steeds zonder woord

Voor elke baas neemt de leider Sega's teleporter. newserv warpt de volger 2 s na de 6x21 van de leider de bazenarena in,
vóór de reisklok van de leider (B-RB2-46, het commentaar bij `floor<f>_sega_from_k` in `make_spire.py`). De
reis-handler van de volger draait daarna dus op vloer 11, 12, 13 of 14. `on_boss_floor(auto_end)` sloeg daar alles
over. De gegenereerde code van df8be3be:

```
label0CD7@0x0CD7:
  get_player_room                 ... r250, r82-r83
  jmpi_eq                         r82, 0x0000000B, label0CDB      (auto_end)
  ... 0x0C, 0x0D, 0x0E
```

De beloning van k3, k7, k10 en k14 verdween zo zonder regel: precies wat B-REGIE-22-48 wegneemt. De bedoeling van het
commentaar ("a boss arena has no reward list") geldt alleen voor de pauze van de bazenkamer zelf. Die pauze heeft de
examenbeloning en geen lijst.

- **Mutant:** M16 zet de oude sprong terug. Op de vaste stand geeft dat T5 "in the arena of k4 (floor 11) from k3:
  the travel handler does not call auto_take (end)", ook voor k8, k11 en k15. De nieuwe gate op de ongewijzigde
  df8be3be-handler geeft dezelfde 8 regels.
- **Fix (`make_spire.py`, mirror_phase0):**
  - Overslaan gebeurt alleen als `R.k` een bazenkamer is. Dat zijn per build de kamers met `boss`: k4, k8, k11 en k15.
  - In auto_go staat eerst `leti r154, 1` en `on_boss_floor(auto_call)`. Staat de volger al in de arena, dan wordt
    auto_take aangeroepen met r154 = 1. Dat geeft de bonus van Overslaan met de regel van 'C' 19, en geen item naast
    de baas die bij het laden van de vloer verscheen (B-REGIE-14-10).
  - Daarna volgt het oude `leti r154, 0; jmp_eq r210, r211, call; leti r154, 1`, zodat het patroon van T4 blijft staan.
  - Dezelfde stand staat in de doc van `rewards.py`, de rij 'C' 19 van `ROUTEB9-PROTOCOL.md`, `observaties.md` en
    beslissing B-E02-17.
- **Bewijs:** de gegenereerde code na de fix:

```
label0CD7@0x0CD7:
  jmpi_eq                         r150, 0x00000004, label0CDB
  jmpi_eq                         r150, 0x00000008, label0CDB
  jmpi_eq                         r150, 0x0000000B, label0CDB
  jmpi_eq                         r150, 0x0000000F, label0CDB
  ...
label0CD9@0x0CD9:
  leti                            r103, 0x00000000
  leti                            r154, 0x00000001
  get_player_room                 ... r250, r82-r83
  jmpi_eq                         r82, 0x0000000B, label0CDA      (auto_call, r154 = 1)
  ... 0x0C, 0x0D, 0x0E
  leti                            r154, 0x00000000
  jmp_eq                          r210, r211, label0CDA
  leti                            r154, 0x00000001
label0CDA@0x0CDA:
  call                            label0662
```

De bytes gaan van 241118 naar 241158 van de 245000. Er komen geen labels bij.

### B2 (major, doel 2, productiescript): de terugdraai van het oude pakket herstartte de echte newserv zonder clients te tellen

`new-features/roguelike/oplevering/uitrol-pc-2026-10-24/terugdraai.sh` was ongewijzigd en draaide nog
`sudo -n systemctl stop newserv` en daarna `start`, zonder telling. Dat gebeurde ook met `--root DIR`, want de
service is altijd de echte. De `apply-on-pc.sh` van hetzelfde pakket wijst er na een uitrol naar ("done. Back: bash
terugdraai.sh --root …"). LEESMIJ stap 7 noemt hem als route op de pc. Op de echte boom pakte hij bovendien de nieuwste
`spire-backup-*`, en dat is ook een back-up van `quests/spire/apply-on-pc.sh`.

- **Mutant / bewijs vooraf:** een nagemaakte sudo en systemctl op een tijdelijke HOME geeft voor `--root COPY` en voor
  de echte boom allebei:

```
sudo -n systemctl stop newserv
sudo -n systemctl start newserv
systemctl is-active newserv
```

  `test_own_restart_gate.sh` met `PKG_DIR` naar de HEAD-versie: "FAIL: package terugdraai.sh on the real tree: exit 0,
  want 3", "... the tree changed", "... systemctl stop|systemctl start|systemctl is-active|", en "package terugdraai.sh
  --root COPY touched the service".
- **Fix:**
  - Op de echte boom weigert het script met exit 3 en doet het niets. Het verwijst naar `quests/spire/terugdraai.sh`,
    dat clients telt.
  - Het roept nooit meer systemctl aan. Met `--root` zet het alleen de kopie terug en zegt het "start that newserv
    yourself".
  - `.undone`-mappen tellen niet meer mee bij de keuze van de back-up.
  - LEESMIJ en PAKKET hebben bovenaan een regel "Vervangen" gekregen.
- **Bewijs na de fix:** `bash quests/spire/test_own_restart_gate.sh` geeft "own restart scripts: e00 counts clients
  and the journal, the old package never restarts", rc=0.

### B3 (minor, doel 2, productiescript): e00 keek niet in het journal

`quests/e00/apply-on-pc.sh` telde verbonden clients, maar keek niet naar logins ("check the journal first"). De
spire-scripts weigeren al op elke login sinds CLIENTS_SINCE.

- **Mutant / bewijs vooraf:** de nieuwe test op de originele e00:

```
FAIL: e00 a login in the last 60 s: exit 0, want 3 (... |active|done: q910 (battle) and q911 (extermination) installed, server restarted|)
FAIL: e00 journalctl failing: exit 0, want 3
FAIL: e00 clean: the journal is not read over the last 60 s
```

- **Fix:** beide gates, die vóór het kopiëren en die in de herstart-sessie, lezen
  `sudo -n journalctl -u newserv --since -60s`. Een login of "Client connected" geeft exit 3, net als een journalctl
  die faalt. Het filter is dat van de spire-gate.
- **Bewijs:** de test is groen (zie B2). `journalctl --since "-60s"` geeft hier rc=0, dus de syntax leest.

### B4 (minor, gate): de dragende claims van de travel-handler en van auto_take stonden alleen op papier

M4, M5, M6, M12, M13, M8 en M14 overleefden alle gates. Zonder een rode gate zou een latere wijziging dit ongemerkt
terug kunnen zetten:

- de fase-test (nooit bij de start);
- het wachten op een open lijst of winkel;
- het laten vallen van een alleen gevraagde lijst;
- een gift die niet paste en toch "genomen" heet;
- een oude r72 die een dubbele gift geeft;
- niets getrokken.

- **Fix (`check_auto_take.py`):**
  - T5 loopt per geval door mirror_phase0 met vaste registers. De gevallen: fases, open lijst en winkel, gevraagde
    lijst, pauze beslist, de klok verder, uit elke bazenkamer (`mirror_boss_k<k>_next`), en in elke arena
    (`floor<f>_sega_from_k<j>` met j + 1 een baas, met r154 = 1).
  - T2 vangt nu ook een give_<id> van een zeldzame of vervloekte entry uit `data/rewards.json`.
  - T3 vangt nu ook de kop `jmpi_eq r181, 0`, `leti r72, 0` vóór en `jmpi_ne r72, 0xFFFFFFFF` na de archetype-switch.
  - De selftest heeft 12 mutanten voor een build met bazen.
- **Bewijs:** ronde 2 hierboven. De gate is groen op q920, q929, q921 (forest), q922/q923 (Ep2, 4 bazen, 3
  arena-aankomsten), q981-q986 (ladder) en de twee room-quests die `--rooms` bouwde vóór B7. Selftest: 12 per bron.

### B5 (minor, test, newserv): de regel met de naam was alleen met een handgemaakte context getest

De vier directe tests geven `spire_bridge_message_text` zelf een context. Dat 'C' 19 in
`spire_bridge_text_context_for` de tabel en het archetype krijgt, testte niets: N1 bleef groen.

- **Fix (`StaticTests.cc`, e2e-case "review r3: …"):** de case stuurt 0x43021300 en 0x43001300 door
  `spire_bridge_handle_register` en wacht op "You did not choose: Entry 2 was taken for you." en "You did not choose:
  you got the Skip bonus.".
- **Bewijs:** N1 geeft nu "GUARD E2E (...) FAILED: the server did not get to the auto take's line with the entry's name
  within 15 seconds". Op de vaste stand: "All static tests passed".

### B6 (minor, pre-existing, labartefact): q929 was verouderd

`build-c4-server-all/q929-gc-e.bin` en `spire-draw.json` liepen achter op de broncode. De laatste rebuild was
cc2481fe; 431f73e1 en 1e6a6a38 veranderden daarna de beloningen, en df8be3be deed dat nog eens. `pc-build.sh` weigert
zo'n boom ("the build changed tracked files").

- **Bewijs:** `build.sh --c4 server-all` met de HEAD-bronnen gaf ` M .../q929-gc-e.bin`, ` M .../spire-draw.json`.
- **Fix:** opnieuw gebouwd met de vaste bronnen en meegecommit: q929-gc-e.bin f20b266c…, q929-gc.dat ongewijzigd
  2c937252….
- **Niet getoetst:** de build gebruikte de newserv van klein-naast-a als assembler en niet de pin 1e0cb5a. Geeft de pin
  andere bytes, dan meldt pc-build.sh dat.

### B7 (minor, pre-existing, NIET gefixt, buiten de brief): `build.sh --rooms` faalt al op de basis

Op 0d14b8db, met de basisbronnen en de basis-build.sh, faalt de build:

```
base --rooms rc=1
ERROR: build-rooms/k2/spire-gc-e.src.txt: 2 item_create2 r20-r31 sites, want 1
ERROR: build-rooms/k2/spire-gc-e.src.txt: savestate packet or resume missing or misplaced (B-E01-178/179)
```

De tweede plek is `e08:flow:mag_reward_got`, de server-beloning van 431f73e1 en 8509144f. `check_resume.py` verwacht
alleen `resume_items_got`. `pc-build.sh` stopt daardoor bij `bash build.sh --rooms`, dus vóór elke uitrol, ook die van
deze wijziging.

Ik heb het niet gefixt. Het ligt in deel A/B van de beloningen, en dat sluit de brief uit. Het is een scope-delta voor
de orchestrator: **wil je dit bewust uitstellen, of gaat het mee in het werk aan deel A?**

## Verificatie na de fixes (letterlijk)

newserv (`/home/m4rck/code/worktrees/newserv/klein-naast-a`):

```
$ mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc) newserv
cmake rc=0
-- Build files have been written to: /home/m4rck/code/worktrees/newserv/klein-naast-a/build
make rc=0
[100%] Built target newserv
$ build/newserv run-static-tests
static rc=0
I 222050 2026-11-16 10:26:30 - -- All static tests passed
$ sh tests/spire-follow-city.test.sh build/newserv
follow-city rc=0
ok: partner floor 15, leader floor 14 -> warp
ok: spire-lives-check
```

Quest-repo (`/home/m4rck/code/worktrees/pso-roguelike/klein-naast-a`):

```
$ D=$(mktemp -d) && ln -s .../build/newserv .../src .../system "$D"/ && NEWSERV_DIR="$D" bash quests/spire/build.sh
qbuild rc=0
--- build: .bin + .dat 241158 of 245000 bytes decompressed (.bin 166372)
--- q920 labels used: engine 239 of 239, rewards 768 of 768, economy 486 of 512, score 90 of 256, coop 256 of 256, path 256 of 256, e08 710 of 768, switch 45 of 256, overflow 576 of 768
--- selftests: 35 gates caught every break of their own input (54 s)
$ NEWSERV_DIR=/home/m4rck/code/worktrees/newserv/klein-naast-a python3 quests/spire/make_spire_rewards.py --check
OK: /home/m4rck/code/worktrees/newserv/klein-naast-a/system/spire-rewards.json matches the design grid
grid rc=0
$ bash quests/spire/test_own_restart_gate.sh
own restart scripts: e00 counts clients and the journal, the old package never restarts
own-restart rc=0
$ python3 quests/spire/check_auto_take.py quests/spire/build/spire-gc-e.src.txt quests/spire/build-c4-server-all/spire-gc-e.src.txt --selftest
auto take: one straight stretch, called once from the travel handler, 2 source(s) checked, 0 without the routine; selftest 24 mutations caught
auto-take rc=0
```

Commits:

- quest-repo: `fc518c9c60c43c96ad5c89b1c8b0f2d3819a5571`
  (`fix(review): follower's reward before a boss, own restart gates`);
- newserv: `e999562e8ef6453c26d6e329bdda2f516b449acd`
  (`fix(review): test the follower's 'C' 19 line through the bridge`).

Niets is gepusht. De trailer is `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`, de attributie van deze
harnas-sessie. De brief schreef "Claude Opus 5".

## Wat ik niet heb aangeraakt

- **Uitrol en processen.** Niets op de pc of in het lab uitgerold of herstart. De draaiende newserv op de laptop is
  niet aangeraakt. Geen ssh pc: elke ssh en sudo in de tests is nagemaakt, met HOME op een tijdelijke map. Geen
  geheugenkaart. Geen proces op naam gestopt.
- **`~/pso-tools/quests/apply-yasha-on-pc.sh`:** buiten scope.
- **Deel A en B van de beloningen.** Daarom is B7 niet gefixt.
- **De pijp van de leider zonder te kiezen.** Staat al in `observaties.md`.
- **De 20 s-grens.** Houdt de volger zijn lijst langer open, dan volgt hij zonder beslissing. Staat al in
  `observaties.md`; ongewijzigd.
- **Clientgedrag, niet gemeten.** Of de tickerregel van 'C' 19 het laden van de vloer overleeft, of dat een volgende
  regel hem wegdrukt, kan alleen een meting op de Wii zeggen. Ook de meseta van de bonus vlak naast een verschenen baas
  volgt alleen het ontwerp: de r154 = 1-route gold al als veilig terwijl de kamer van de leider loopt. Op de client is
  dat niet gemeten.
- **Ep2-baas k27** heeft geen Sega-aankomstlabel. De vloertest dekt hem in de code, maar T5 heeft er geen eigen geval
  voor.
- **`build-rooms`:** alleen de twee quests die vóór B7 gebouwd werden zijn door de nieuwe gate gegaan. De
  build-variantmappen die ik maakte heb ik weer verwijderd.

## Stopadvies

Nog één korte koude ronde, alleen op B1, daarna stoppen. B1 is de enige wijziging aan spelgedrag. Ik heb die fix zelf
gebouwd, dus een reviewer die hem niet bouwde moet hem lezen. B2-B5 zijn bewezen met rode-dan-groene tests.

Los van de loop blokkeert B7 (pre-existing) `pc-build.sh`. Er komt dus geen uitrol van deze wijziging voordat de
room-build weer groen is.
