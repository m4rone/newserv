# Naad N4: rapport

Fase N4 van de naad (`docs/review-loop/goals/naad.md` punt 2, de haken, en punt 4). Basis `655f1cd`, tak
`feature/beloningen-b`, drie commits tot en met `43f7c7c` (dit rapport en één testregel komen in een vierde).
Gemeten op 2026-11-16 op de laptop.

## Dekkingsclaim

Deze fase dekt: dat elke haak H1-H11 bestaat, zijn aanmelders in vaste volgorde aanroept, een aanmelder met zijn
schakelaar uit overslaat, en zonder aanmelder niets verandert aan wat de tests zien; dat de savestate met run-mag
gelijk heen en terug gaat (de zes fixtures van N1 en `tests/spire-savestate-before-all-exams.json`); dat de
aanbiedingen gelijk zijn (de vingerafdruk van N1), ook met een aanmelder op H3 die overal gewicht 1 geeft.

Deze fase dekt NIET: dat de aanroepplekken op een live client op het bedoelde moment vuren. De plekken in
`ReceiveSubcommands.cc`, `ReceiveCommands.cc` en de handlers 'W', 'O', 'I', 98, 'V' en hervatten zijn gelezen, niet
gespeeld: geen test roept die handlers met een client aan. De tests roepen de aanroepfunctie van een haak zelf aan
(H1, H2, H4-H9 en de vier momenten van H10), en gaan alleen bij H3 (via `spire_reward_offer`), H10 schrijven en lezen
(via `spire_savestate_to_json`/`from_json`) en H11 (via `spire_meta_save_file`/`spire_meta_load_file`) door de kern
heen. Ook niet gedekt: wat een mechaniek later op een haak doet, de schijf (soort 4) met een aanmelder op H3 (alleen
zonder aanmelder, via de vingerafdruk), de sleutelvolgorde van de savestate-tekst op de pc (N1: hangt aan de
libstdc++ van de bouwmachine), en alles op de pc of de Wii.

## Wat er staat

- `src/SpireMechanics.hh/.cc`: de elf haken. Eén register (`hook_registry()`, een static binnen een functie) met per
  haak een lijst die op (volgnummer, naam) gesorteerd blijft. Per haak een aanmeldfunctie
  (`spire_mech_register_<haak>`) en een aanroepfunctie (`spire_mech_<haak>`). Een aanmelding is
  `SpireMechHook<FnT>{naam, schakelaarsleutel, volgnummer, functie, default_off}`. De volgnummers staan als
  `SPIRE_MECH_ORDER_*` op één plek: mags 10, leveling 20, hunter-types 30, ranger-types 40, force 50, verdediging 60,
  superkrachten 70. Die ene rangorde geeft elke rij van de tabel in §3 van het ontwerp.
- `src/SpireMagState.cc` (nieuw): het schrijven en lezen van `"RunMag"`, aangemeld op H10 als `mags`.
- `src/SpireFlagBridge.hh` en `src/SpireMeta.hh`: elk één veld `sections` (in `SpireSavestate` en `SpireMetaFile`),
  waarin een aanmelder van H10 of H11 zijn eigen sleutel draagt.
- De aanroepregels in de kern (bewijs 3) en in drie soortbestanden (H3).
- `src/SpireTestsMechanics.cc`: `spire_tests_hooks()`.

## De haken: aanroepplek en wat de aanmelder meekrijgt

| # | aanroepfunctie | bestand, functie | meegegeven |
|---|---|---|---|
| H1 | `spire_mech_quest_bin` | `ReceiveCommands.cc`, `set_lobby_quest`, om de .bin heen ná heat en startlevel | client, spel, de .bin (gecomprimeerd); antwoordt de .bin die de client krijgt |
| H2 | `spire_mech_reward_room` | `SpireFlagBridge.cc`, `spire_reward_make_offer`, na trede en kamerindex | client, spel, serial, de getrokken regel, kamer k, elite; kamerindex en trede om te wijzigen |
| H3 | `spire_mech_type_weight_pick` | `SpireRewardWeapon.cc` `spire_reward_weapon_offer` (andere lijn), `SpireRewardDisk.cc` `spire_reward_disk_offer`, `SpireRewardUnit.cc` `spire_reward_unit_offer` | per kandidaat: het aanbod (`SpireRewardOfferInput`, met in `live` client en spel), soort kandidaat, lijnnaam of techniek+level of unitcode; antwoordt een gewicht |
| H4 | `spire_mech_player_hp` | `ReceiveSubcommands.cc`, `on_change_hp`, vóór het doorsturen | client, spel, subcommando (alleen 4B en 4C bereiken een aanmelder) |
| H5 | `spire_mech_player_died` | `ReceiveSubcommands.cc`, `on_player_died`, vóór de synchro-aftrek en het doorsturen | client, spel |
| H6 | `spire_mech_tech_done` | `ReceiveSubcommands.cc`, `on_cast_technique_finished`, na het doorsturen | client, spel, techniek, level |
| H7 | `spire_mech_enemy_killed` | `ReceiveSubcommands.cc`, `on_entity_drop_item_request`, na het verzoenen met de kaart | client, spel, het verzoende dropverzoek (`DropReconcileResult`), vloer; een kist bereikt geen aanmelder |
| H8 | `spire_mech_wave_start` | `SpireFlagBridge.cc`, `spire_bridge_handle_register`, de tak die de 'W'-tekst naar iedereen stuurt | schrijver, spel, serial, quest, a, b |
| H9 | `spire_mech_run_end` | `SpireFlagBridge.cc`: `spire_base_return` ('O' OK), `spire_run_again` ('I' OK, na het vergeten van de vorige run), `spire_guard_end_run` (98, eerste regel) | client, spel, welke van de drie |
| H10 | `spire_mech_run_state_capture/_write/_read/_resume` | `SpireFlagBridge.cc`: `spire_savestate_write` (na `ss.run_mag`), `spire_savestate_to_json`, `spire_savestate_from_json`, `spire_resume_answer` (na `spire_run_mag_adopt`) | capture en resume: client, spel, de savestate; write en read: de savestate en de JSON-dict |
| H11 | `spire_mech_char_meta_read/_write` | `SpireMeta.cc`: `spire_meta_load_file` (na de eigen sleutels), `spire_meta_save` (vóór de tekst) | de JSON en het `SpireMetaFile` |

Zonder aanmelder die aangeroepen wordt: H1 geeft dezelfde pointer terug; H2 laat kamerindex en trede staan; H3 doet
`rnd(n) % n`, de ene trekking van vóór de naad, en vraagt geen kandidaat; H4-H9 doen niets; H10 en H11 raken de dict
niet aan (op H10 staat `mags` altijd aangemeld).

## Beslissingen (zelf genomen, binnen de opdracht)

1. **De run-mag meldt zich op H10 aan zonder schakelaar** (lege sleutel = geen schakelaar, altijd aangeroepen). De
   run-mag zelf heeft in de kern geen schakelaar; zou `mags` uit de sleutel `"RunMag"` uit de savestate halen, dan
   geeft een hervatting de mag niet terug (`spire_savestate_items` houdt alleen de mag van de ledger). §7 van het
   ontwerp: geen schakelaar voor de itemveiligheid. Een test legt het vast (`{"mags": false}` verandert de tekst niet).
2. **H10 heeft vier momenten**, niet twee: 'V' (capture), tekst schrijven, tekst lezen, hervatten. "Savestate
   schrijven en hervatten" zijn de twee momenten met een client; de twee tekstmomenten zijn waar de sleutel van de
   run-mag stond. Zonder capture en resume kan force ("Techs") of superkrachten ("Powers") zijn stand niet uit de
   client halen of terugzetten zonder de kern te wijzigen.
3. **`sections` in `SpireSavestate` en `SpireMetaFile`** (`std::map<std::string, std::shared_ptr<const void>>`, de
   vorm van `SpireRewardTable::sections` uit N3). Bij de meta is dat nodig: elke vlagschrijf leest het bestand eerst
   en schrijft het struct terug, dus een sleutel die het struct niet draagt, is bij de volgende schrijf weg.
   `SpireMeta.hh` stond voorwaardelijk in de schrijfset ("tenzij een declaratie het vraagt"); dit is die declaratie.
   Het veld heeft `= {}`: `StaticTests.cc` vult `SpireMetaFile` op vier plekken positioneel en `-Werror` weigert
   anders een veld zonder beginwaarde.
4. **Eén rangorde voor alle haken** (`SPIRE_MECH_ORDER_*`) in plaats van een nummer per haak en aanmelder: de tabel
   van §3 is met één rij getallen te geven, en een bouwer hoeft niets te kiezen.
5. **Een aanmelder die gooit** op H1-H9 of bij capture/resume van H10 wordt gelogd en telt voor dat moment als niet
   aangemeld (H2 zet kamerindex en trede terug). De handlers 'O', 'I' en 98 beloven "never throws", en een throw in een
   handler van `ReceiveSubcommands.cc` kost de client zijn verbinding. Bij H10 schrijven/lezen en H11 gaat een throw
   door: dat is het lezen of schrijven van een bestand, en zo werkte het blok van de run-mag al (een `"RunMag"` zonder
   `RunId` weigert de savestate).
6. **H3 als keuzefunctie**, niet als los gewicht: `spire_mech_type_weight_pick(n, rnd, kandidaat)`. Gewichten van
   meer aanmelders vermenigvuldigen; allemaal 1 geeft dezelfde trekking `rnd(n)` en dezelfde kandidaat; allemaal 0
   valt terug op de gewone trekking. Het aantal spelers en wat al in de keuze staat leest de aanmelder zelf uit
   `in->live` (client, spel, `spire_reward_offers_for`); de kern rekent ze niet voor.
7. **H4 filtert op 4B en 4C in de aanroepfunctie**: de handler `on_change_hp` bedient ook 6x4A (volledig afgeweerd) en
   6x2F; de ene aanroepregel staat in het bestaande blok voor 4A-4C en geeft het subcommando mee. Het nummer is dat
   van de client (GC: 4B/4C); een oudere clientversie met andere nummers bereikt geen aanmelder.
8. **H7 filtert de kist in de aanroepfunctie**, zodat er in `ReceiveSubcommands.cc` één regel staat. De aanroep staat
   vóór de keuze van de dropmodus, dus ook een spel zonder serverdrops meldt de vijand.
9. **H8 staat in de tak die de 'W'-tekst rondstuurt**, niet eerder in de handler: dat is de plek waar de kern de 'W'
   al als 'W' behandelt (de tekst `Wave a/b` is nooit leeg).
10. **H9 bij 'O' en 'I' alleen als het antwoord OK is**: een geweigerde 'O' of 'I' beëindigt geen run. Gaat de client
    bij 'O' naar het basisspel (`BASE_GAME`), dan volgt zijn 98 en vuurt `GAME_EXIT`.
11. **De haken filteren niet op "is dit een Spire-run"**: H4-H7 staan in handlers van elk spel en de 98 komt uit elk
    spel. De aanmelder vraagt het zelf (`spire_game_is_spire`); het staat in de kop van de haken.
12. **Eén commit voor alle aanroepregels** (`039b8d0`) in plaats van één per bestand: elke commit moest gemeten groen
    zijn, en één cyclus dekt ze samen.
13. **De beslissingen staan hier en niet in `decisions.md`**: dat bestand staat in de quest-repo, die deze fase niet
    mocht aanraken.

## Bewust niet gedaan

- **`ss.run_mag = *led` bij de 'V' en `spire_run_mag_adopt` bij het hervatten** bleven in `SpireFlagBridge.cc`. De
  opdracht noemt het schrijven en lezen van de sleutels; de ledger-opslag (`spire_run_mag_store`) is static in de
  brug en wordt door de rest van de run-mag gebruikt.
- **Het veld `run_mag` in `SpireSavestate`** bleef een eigen veld (niet naar `sections`): `SpireTestsSeam.cc` en
  `SpireTestsMags.cc` lezen het.
- **De trekking van de special van een wapen** (`SpireRewardWeapon.cc`, `cell.specials[...]`) kreeg geen H3: het
  ontwerp noemt spell, unit en lijn.
- **Niemand meldt zich aan** op H1-H9 en H11.
- Geen uitrol, geen pc-build, geen questbouw, niets in de quest-repo, niet gepusht.

## Bewijs 1: vingerafdruk en savestate-fixtures groen in elke commit

Per commit gebouwd en gedraaid vóór het committen (`build/n4-cycle.sh`: `make -C build -j16 newserv`,
`build/newserv run-static-tests`, `sh tests/spire-follow-city.test.sh build/newserv`). Elke regel is de letterlijke
inhoud van het exit-bestand:

```
78d4850 (a, de haken)          build 0 static 0 city 0
039b8d0 (b, de aanroepregels)  build 0 static 0 city 0
43f7c7c (c, SpireMagState.cc)  build 0 static 0 city 0
eindstand (dit rapport)        build 0 static 0 city 0
```

Uit het log van de eindstand (`build/n4-eind-static.log`, 59312 regels):

```
-- Spire test file: attack
-- Spire test file: defence
-- Spire test file: force
-- Spire test file: mags
-- Spire test file: mechanics
W ... Spire hook H2 reward_room: stuk: stuk failed (a registrant of the test that fails); not called for this moment
W ... Spire hook H5 player_died: stuk: stuk failed (a registrant of the test that fails); not called for this moment
-- Spire test file: seam
-- Spire seam: the reward offers are as before the seam
-- Spire seam: a savestate with the run mag loads and is written back the same
...
-- All static tests passed
```

De twee W-regels horen bij de tests (een aanmelder die expres gooit). `tail -3` van de city-test:

```
ok: partner floor 1, leader floor 2 -> skip
ok: partner floor 15, leader floor 14 -> warp
ok: spire-lives-check
```

Staart van de build: `[100%] Built target newserv`, 0 regels met `warning: ` in het buildlog. De 153 regels met "skip"
in het testlog zijn 152 logregels van de item creator ("skipping special generation") en één logregel van de brug, geen
overgeslagen tests. De eerste cyclus van (a) faalde op de build (`missing initializer for member
SpireMetaFile::sections` in `StaticTests.cc`), zie beslissing 3; de eerste cyclus van (b) faalde op de eigen H7-test
(de kistfilter kwam erbij zonder dat de test een vijand meegaf). Beide hersteld vóór de commit.

`tests/` en `src/SpireTestsSeam.cc` zijn niet gewijzigd: `git diff --stat 655f1cd -- tests/ src/SpireTestsSeam.cc`
geeft geen uitvoer.

## Bewijs 2: de run-mag-sleutels zijn letterlijk verhuisd

Uitvoer van het script onderaan (`moved.py <werkplek> 039b8d0`, de stand vóór de verhuizing):

```
lines gone from SpireFlagBridge.cc (non-empty, with repeats): 65
of those in SpireMagState.cc: 63
gone and NOT in SpireMagState.cc: 2
  MISSING x1: spire_mech_run_state_write(ss, json); // the seam's H10
  MISSING x1: spire_mech_run_state_read(json, ret); // the seam's H10
lines new in SpireFlagBridge.cc: 2
  NEW x1: spire_mech_run_state_write(ss, json); // the seam's H10: the run mag's "RunMag" (SpireMagState.cc), then the others
  NEW x1: spire_mech_run_state_read(json, ret); // the seam's H10: the run mag's "RunMag" (SpireMagState.cc), then the others
block write: 31 lines of the base, contiguous in SpireMagState.cc at line index 16 (-1 = not found)
block read: 32 lines of the base, contiguous in SpireMagState.cc at line index 49 (-1 = not found)
lines of SpireMagState.cc that do not come from SpireFlagBridge.cc: 22
  OWN: #include "SpireFlagBridge.hh"
  OWN: #include <algorithm>
  OWN: #include <phosg/JSON.hh>
  OWN: #include "Loggers.hh"
  OWN: #include "SpireMag.hh"
  OWN: #include "SpireMechanics.hh"
  OWN: // The run mag in the savestate (the seam, docs/review-loop/goals/naad.md point 4): the key "RunMag" with the run mag's
  OWN: // ledger, written and read at the seam's hook H10 run_state (SpireMechanics.hh). Both blocks stood in
  OWN: // SpireFlagBridge.cc (spire_savestate_to_json and spire_savestate_from_json) and are moved here as they were; the
  OWN: // hook is called where they stood, so the key is put in the same dict at the same moment and the savestate's text is
  OWN: // the one of before.
  OWN: //
  OWN: // The registration carries no switch: the run mag itself has none in the core, and a savestate written without its
  OWN: // ledger gives no run mag back at the resume (spire_savestate_items keeps only the ledger's mag).
  OWN: namespace {
  OWN: void spire_mag_state_write(const SpireSavestate& ss, phosg::JSON& json) {
  OWN: }
  OWN: }
  OWN: void spire_mag_state_read(const phosg::JSON& json, SpireSavestate& ret) {
  OWN: [[maybe_unused]] const bool registered = spire_mech_register_run_state(
  OWN: {"mags", "", SPIRE_MECH_ORDER_MAGS, {nullptr, spire_mag_state_write, spire_mag_state_read, nullptr}});
  OWN: } // namespace
```

De twee "MISSING"-regels zijn de twee aanroepregels van H10 die bleven staan en een langer commentaar kregen (de twee
"NEW"-regels). 63 regels verhuisd: het schrijfblok (31) en het leesblok (32), elk aaneengesloten en in volgorde. De
22 eigen regels van `SpireMagState.cc`: zes includes, acht regels commentaar, de namespace (open en dicht), de twee
functiekoppen met hun sluithaak, en de aanmelding (twee regels).

De haak wordt aangeroepen op de plek waar het blok stond (schrijven: als laatste vóór `json.serialize`; lezen: als
laatste vóór `ret.exists = true`), dus `"RunMag"` komt op hetzelfde moment in dezelfde dict.

## Bewijs 3: de aanroepregels in de kern

`git diff -U0 655f1cd -- src/ReceiveSubcommands.cc src/ReceiveCommands.cc src/SpireMeta.cc`:

```
diff --git c/src/ReceiveCommands.cc w/src/ReceiveCommands.cc
index 81be1d8..d8c8edd 100644
--- c/src/ReceiveCommands.cc
+++ w/src/ReceiveCommands.cc
@@ -29,0 +30 @@
+#include "SpireMechanics.hh"
@@ -2541 +2542 @@ void set_lobby_quest(std::shared_ptr<Lobby> l, std::shared_ptr<const Quest> q, b
-        spire_start_level_quest_bin(lc, l, spire_heat_quest_bin(lc, l, vq->bin_contents)));
+        spire_mech_quest_bin(lc, l, spire_start_level_quest_bin(lc, l, spire_heat_quest_bin(lc, l, vq->bin_contents))));
diff --git c/src/ReceiveSubcommands.cc w/src/ReceiveSubcommands.cc
index 7eac413..3cd56fc 100644
--- c/src/ReceiveSubcommands.cc
+++ w/src/ReceiveSubcommands.cc
@@ -24,0 +25 @@
+#include "SpireMechanics.hh"
@@ -1736,0 +1738 @@ static void on_player_died(std::shared_ptr<Client> c, SubcommandMessage& msg) {
+  spire_mech_player_died(c, l); // the seam's H5 (SpireMechanics.hh)
@@ -1821,0 +1824 @@ static asio::awaitable<void> on_change_hp(std::shared_ptr<Client> c, SubcommandM
+    spire_mech_player_hp(c, l, cmd.subcommand); // the seam's H4 (SpireMechanics.hh)
@@ -1836,0 +1840 @@ static void on_cast_technique_finished(std::shared_ptr<Client> c, SubcommandMess
+    spire_mech_tech_done(c, l, cmd.technique_number, cmd.level); // the seam's H6 (SpireMechanics.hh)
@@ -3043,0 +3048 @@ static void on_entity_drop_item_request(std::shared_ptr<Client> c, SubcommandMes
+  spire_mech_enemy_killed(c, l, rec, cmd.floor); // the seam's H7 (SpireMechanics.hh)
diff --git c/src/SpireMeta.cc w/src/SpireMeta.cc
index a4a1dd0..3c37e98 100644
--- c/src/SpireMeta.cc
+++ w/src/SpireMeta.cc
@@ -12,0 +13 @@
+#include "SpireMechanics.hh"
@@ -107,0 +109 @@ SpireMetaFile spire_meta_load_file(const std::string& filename) {
+  spire_mech_char_meta_read(json, ret); // the seam's H11
@@ -164,0 +167 @@ static void spire_meta_save(const std::string& filename, uint32_t serial_number,
+  spire_mech_char_meta_write(file, json); // the seam's H11
```

In `SpireFlagBridge.cc` (alle toegevoegde regels met `spire_mech_`, met de functie waarin ze staan):

```
spire_savestate_write        +    spire_mech_run_state_capture(c, l, ss); // the seam's H10
spire_savestate_to_json      +  spire_mech_run_state_write(ss, json); // the seam's H10: the run mag's "RunMag" (SpireMagState.cc), then the others
spire_savestate_from_json    +  spire_mech_run_state_read(json, ret); // the seam's H10: the run mag's "RunMag" (SpireMagState.cc), then the others
spire_resume_answer          +    spire_mech_run_state_resume(c, l, cr->savestate); // the seam's H10
spire_reward_make_offer      +  spire_mech_reward_room({c, l, serial_number, entry, k, elite}, room_index, trede); // the seam's H2
spire_bridge_handle_register +          spire_mech_wave_start(c, l, serial_number, quest_number, (value >> 8) & 0xFF, value & 0xFF); // the seam's H8
spire_guard_end_run          +  spire_mech_run_end(c, l, SpireMechRunEnd::GAME_EXIT); // the seam's H9
spire_base_return            +    spire_mech_run_end(c, l, SpireMechRunEnd::BASE_RETURN); // the seam's H9
spire_run_again              +    spire_mech_run_end(c, l, SpireMechRunEnd::RUN_AGAIN); // the seam's H9
```

In de soortbestanden (H3), elk de ene trekking vervangen:

```
SpireRewardDisk.cc    -  const auto& [tech, level] = can[rnd(can.size()) % can.size()];
                      +  const auto& [tech, level] = can[spire_mech_type_weight_pick(can.size(), rnd, [&](size_t z) { // the seam's H3
                      +    return SpireMechTypeWeight{&in, SpireMechCandidate::TECH, nullptr, can[z].first, can[z].second};
                      +  })];
SpireRewardUnit.cc    -  uint32_t code = codes[rnd(codes.size()) % codes.size()];
                      +  uint32_t code = codes[spire_mech_type_weight_pick(codes.size(), rnd, [&](size_t z) { // the seam's H3
                      +    return SpireMechTypeWeight{&in, SpireMechCandidate::UNIT, nullptr, codes[z], 0};
                      +  })];
SpireRewardWeapon.cc  -      ret.line = others[rnd(others.size()) % others.size()];
                      +      ret.line = others[spire_mech_type_weight_pick(others.size(), rnd, [&](size_t z) { // the seam's H3
                      +        return SpireMechTypeWeight{&in, SpireMechCandidate::LINE, &others[z], 0, 0};
                      +      })];
```

Bestanden buiten de schrijfset in `git diff 655f1cd --name-only`: geen (14 bestanden, alle in de lijst; `SpireMeta.hh`
via de voorwaarde, beslissing 3).

## Bewijs 4: een haaktest bijt

In `each_hook` (`SpireMechanics.cc`) de schakelaartoets vervangen door `if (false)`, gebouwd en gedraaid:

```
build 0 static 1 city 0
E ... run-static-tests: failure at .../src/SpireTestsMechanics.cc:255: e_l != called()
```

Regel 255 is `expect_eq(e_l, called()); // a registrant whose switch is off is not called`, bij de eerste haak (H1).
Teruggezet: `git diff --quiet -- src/SpireMechanics.cc` gaf 0, en de eindcyclus daarna is groen (bewijs 1).

Niet met een ingreep getoond: dat de savestate-fixtures bijten als `SpireMagState.cc` zich niet aanmeldt. Wel staat
er een test die eist dat `mags` op H10 staat en verder niemand op enige haak.

## Wat de tests per haak toetsen

Per haak drie aanmelders van de test: `aa` (nummer 20, schakelaar `test.haak.aan`) eerst aangemeld, dan `zz` (nummer
10, geen schakelaar), dan `mm` (nummer 15, schakelaar `test.haak.uit`). Verwacht: zonder aanmelder de oude uitkomst;
met `test.haak.uit` uit de aanroepen zz, aa; aan: zz, mm, aa (op nummer, niet op naam en niet op aanmeldvolgorde);
beide sleutels uit: alleen zz; een naam twee keer op een haak wordt genoemd en de eerste blijft; na de test is de haak
zoals ervoor (een `HookGuard` haalt de aanmelding weg bij het verlaten van het blok, ook na een gefaalde verwachting).
Daarnaast: H1 nullptr laat de .bin; H2 en H5 een aanmelder die gooit; H3 de gewichten (3 van 6, vermenigvuldigen,
alles 0) en via het echte aanbod van unit en andere lijn (12 klassen x 4 niveaus: elke trekking en elk aanbod gelijk
met een aanmelder die 1 geeft; één unit met gewicht wordt het aanbod); H4 dat 4A en 2F niemand bereiken; H7 dat een
kist niemand bereikt; H10 en H11 een eigen sleutel heen en terug door de kern.

## Gezien, niet gebouwd

- performance · een aanmelder met een schakelaar kost per aanroep twee bestandsvragen (`spire_mech_enabled` kijkt of
  het schakelaarbestand veranderd is) · `SpireMechanics.cc` `switches()` · op H4 is dat per treffer; een tijdvenster
  van een seconde zou het wegnemen
- security · H4-H7 en de 98 van H9 vuren in elk spel, ook buiten een Spire-run · `SpireMechanics.hh`, kop van de haken
  · een aanmelder die de vraag vergeet, handelt in een gewoon spel; een filter in de aanroepfunctie zou dat dichten
  maar vraagt een keuze over wat "Spire-spel" is bij een 98 zonder spel
- datakwaliteit · de meta per personage wordt bij elke vlagschrijf gelezen en teruggeschreven; een sleutel die geen
  aanmelder draagt (bijvoorbeeld met zijn schakelaar uit) verdwijnt dan uit het bestand · `SpireMeta.cc`
  `spire_meta_save` · een mechaniek die uit gaat, verliest zo zijn meta bij de eerstvolgende schrijf; onbekende
  sleutels ongelezen doorgeven zou dat voorkomen
- datakwaliteit · hetzelfde voor de savestate: een aanmelder op H10 met zijn schakelaar uit schrijft zijn sleutel niet
  bij de volgende 'V' · `SpireFlagBridge.cc` `spire_savestate_to_json` · raakt alleen een latere mechaniek, niet de
  run-mag (geen schakelaar)
- codekwaliteit · H2 begrenst de trede en kamerindex van een aanmelder niet · `SpireFlagBridge.cc`
  `spire_reward_make_offer` · niet gemeten wat een waarde buiten het rooster doet

## Het script (stond in de scratchmap, niet in de repo)

```python
#!/usr/bin/env python3
# Shows that every line that left src/SpireFlagBridge.cc since <base> stands, leading whitespace aside, in
# src/SpireMagState.cc, and lists the lines of SpireMagState.cc that do not come from there.
import subprocess, sys, collections
repo, base = sys.argv[1], sys.argv[2]
def lines(text):
    return [l.strip() for l in text.splitlines() if l.strip()]
old = subprocess.run(['git', '-C', repo, 'show', f'{base}:src/SpireFlagBridge.cc'], capture_output=True, text=True, check=True).stdout
new = open(f'{repo}/src/SpireFlagBridge.cc').read()
dest = open(f'{repo}/src/SpireMagState.cc').read()
gone = collections.Counter(lines(old)) - collections.Counter(lines(new))
came = collections.Counter(lines(new)) - collections.Counter(lines(old))
have = collections.Counter(lines(dest))
moved = sum(min(n, have[l]) for l, n in gone.items())
missing = {l: n - have[l] for l, n in gone.items() if have[l] < n}
print(f'lines gone from SpireFlagBridge.cc (non-empty, with repeats): {sum(gone.values())}')
print(f'of those in SpireMagState.cc: {moved}')
print(f'gone and NOT in SpireMagState.cc: {sum(missing.values())}')
for l, n in missing.items():
    print(f'  MISSING x{n}: {l}')
print(f'lines new in SpireFlagBridge.cc: {sum(came.values())}')
for l, n in came.items():
    print(f'  NEW x{n}: {l}')
# the moved blocks stand contiguous and in order
old_l, dest_l = lines(old), lines(dest)
def find_run(hay, needle):
    for i in range(len(hay) - len(needle) + 1):
        if hay[i:i + len(needle)] == needle:
            return i
    return -1
for name, first, last in (('write', 'if (ss.run_mag) { // F6: the run mag ledger, keyed by run id and step, never by item id', 'json.emplace("RunMag", std::move(led_json));'),
                          ('read', 'if (json.contains("RunMag")) {', 'ret.run_mag = led;')):
    a = old_l.index(first); b = old_l.index(last, a) + 2  # with the closing brace
    block = old_l[a:b]
    print(f'block {name}: {len(block)} lines of the base, contiguous in SpireMagState.cc at line index {find_run(dest_l, block)} (-1 = not found)')
rest = collections.Counter(dest_l) - gone
print(f'lines of SpireMagState.cc that do not come from SpireFlagBridge.cc: {sum(rest.values())}')
for l in dest_l:
    if rest[l] > 0:
        rest[l] -= 1
        print(f'  OWN: {l}')
```
