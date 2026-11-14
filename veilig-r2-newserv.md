# Veilig-r2 (newserv): vier fixes uit systeemcritic ronde 2

Tak spike/veilig-r2, gemaakt op 66a32cb. Bronnen: systeem-critic-fixlijst-r2-veilig.md (7r2-M1, 7r2-M2, 2r2-M1,
2r2-M2), de rapporten van systeem 7, 2 en 1, en B-REGIE-21-22. Bouw met `make -C build -j4`. Logs staan in
`logs-r2/` (niet gecommit).

Eindstand: 72c9fa8. `build/newserv help | head -1` geeft `newserv-72c9fa8 built 2026-11-14 10:12:01.000000 UTC`.
`run-static-tests` geeft exit 0 (gelezen zonder pijp) en "All static tests passed". Er zijn 0 regels met FAILED,
16 GUARD E2E-gevallen (0 FAILED) en 8 nieuwe R2-gevallen.
SpireDrops is niet aangeraakt: `git diff --quiet ee0fa66 HEAD -- src/SpireDrops.cc src/SpireDrops.hh; echo $?` geeft 0.
De startlevel-aanroepen (`spire_resume_level_for_client` en `spire_start_level_quest_bin`), de restore-duw en
`spire_replay_after_floor_load` zijn niet aangeraakt.

Hoe "rood zonder fix" is gemeten: in de eindboom is per fix alleen de fix weggehaald (een mutant), daarna gebouwd en
getest. Daarna is de bron teruggezet en opnieuw gebouwd. Elke commit is ook apart gebouwd en getest: exit 0, 0 FAILED.

## 1. 7r2-M1: een te laat hervat-antwoord wist de geparkeerde run niet meer — a3b63fc
- Keuze: bij het run-einde gaat een echte savestate van het run-id alleen weg als een client van de game die run
  echt speelde. Dat is zijn 'B', die geen oefenrun was. Het staat vast als `SpireGameRun::real_played`, dat bij de
  'B' wordt gezet, en als pure functie `spire_savestate_end_deletes_file`. Een oefen-savestate gaat weg zoals
  vroeger. "Time up" loopt via `spire_run_timer_record_end` naar hetzelfde `spire_savestate_on_run_end`, dus dat pad
  is mee gedekt.
- Tests: R2-gevallen voor 'X' en voor "time up". Elk stuurt 'J' k9, 'B' (start 9, K0 1, zonder hervat-bit), 'V' en
  het einde door `spire_bridge_handle_register`. Een derde geval toont dat een echt hervatte run zijn bestand nog
  steeds wist. Daarnaast is er een eenheidstest op de functie.
- Rood (de controle weggehaald):
  `R2 (7r2-M1: a resume answer that came too late makes a practice run in the parked run's id; its 'X' keeps the run) FAILED: failure at .../src/StaticTests.cc:10752: !(std::filesystem::is_regular_file(file))`
  `R2 (7r2-M1: the same practice run in the parked run's id ends by time up; the run stays) FAILED: failure at .../src/StaticTests.cc:10767: !(std::filesystem::is_regular_file(file))`
- Groen: a3b63fc, exit 0.

## 2. 7r2-M2 = 1r2-M1: één geparkeerde run per moeilijkheid per episode, vervangen vanaf kamer twee — 5f59ca3
- Bestanden: Ep1 houdt `<stem>.savestate.<rung>.json`. Ep2 krijgt `<stem>.savestate.ep2.<rung>.json`. Ep1 houdt de
  oude naam, zodat Ep1-runs zichtbaar blijven na een terugdraai.
- Oud formaat: een rung-bestand van vroeger wordt toegewezen via zijn eigen veld `"Episode"`, dat in elke savestate
  sinds v44 staat. Daardoor is het eenduidig zolang het bestand leesbaar is.
  - Voor Ep1 telt het rung-bestand, tenzij het Ep2 vermeldt.
  - Voor Ep2 telt het als het Ep2 vermeldt en Ep2 nog geen eigen bestand heeft.
  - Schrijft Ep1 over een rung-bestand met een Ep2-run, dan wordt die run eerst naar het Ep2-bestand gekopieerd, als
    dat nog niet bestaat.
  - De eerste Ep2-schrijf wist zijn oude Ep2-run uit het rung-bestand.
  - Het heel oude `.savestate.json` telt per episode en rung, en gaat alleen weg bij een schrijf van dezelfde
    episode en rung.
  - Niet eenduidig: een onleesbaar rung-bestand. Dat geldt als bestand van Ep1, op grond van zijn naam. Ep2 laat het
    staan en leest het als "geen".
- Vervangen: een nieuwe run laat bij de 'V' van zijn eerste kamer (`spire_savestate_is_first_room_v`: kamer gelijk
  aan de startkamer) een geparkeerde run met een ander run-id staan. Dat geldt ook voor een onleesbaar bestand. Vanaf
  de 'V' van de volgende kamer vervangt hij die run. Het ligt vast in `spire_savestate_may_replace`. Dit geldt ook
  voor losse kamer 1, voor Spire Run en voor de eerste kamer van Ep2.
- Melding: bij de 'B' van een nieuwe echte run gaat er één tickerregel uit via `spire_send_ticker`, als er op die
  episode en moeilijkheid een run met een ander id staat:
  "Your saved run (room 9, Normal) is replaced when the next room starts." (met " elite" of " rest" waar dat geldt).
- Mee gedaan als kan-punten op dezelfde regels:
  - K-2: de oefenregel noemt de kamerquest, bijvoorbeeld "Practice is not saved: your run waits in Spire room 5 elite Hard.".
  - K-3: bij een onleesbaar bestand zegt de regel "Practice is not saved: the saved run here cannot be read.".
  - K-1: een R2-geval stuurt de 'V' van een oefenrun door `spire_savestate_write` en controleert het bestand en de regel.
- Ook per episode: `$spire continue` (beide episodes), de melding bij binnenkomst en de regel "elsewhere" (de episode
  van de game), de lijst van bestanden bij het run-einde (de Ep2-bestanden erbij) en het zoeken naar het grootboek
  van de run-mag.
- Rood (geen episode-splitsing en geen eerste-kamerregel):
  `R2 (7r2-M2: one parked run per difficulty per episode, ...) FAILED: failure at .../src/StaticTests.cc:10793: stem + ".savestate.ep2.hard.json" != spire_savestate_slot_filename(stem, EP2, 1)`
  `R2 (7r2-M2: "Spire room 1" next to a parked run: ...) FAILED: failure at .../src/StaticTests.cc:10922: bytes != phosg::load_file(file)`
  `R2 (7r2-M2: the first room of Episode 2 next to a parked Episode 1 run: ...) FAILED: failure at .../src/StaticTests.cc:10936: 0u != p.ch->count("is replaced when the next room starts")`
- Groen: 5f59ca3, exit 0.
- Een questwijziging is niet nodig.

## 3. 2r2-M1: alleen de delen van één lichaam geven hun treffer aan de ouder — c42b6bc
- Keuze: de benoemde functie `spire_child_hit_is_parent_hit(EnemyType)` geeft alleen voor PAN_ARMS waar.
  `spire_wave_enemies` neemt alleen zulke ouders op in `parent_index`. Sinow-spiegelbeelden zijn bewust niet
  opgenomen. Elk type extra in de lijst haalt een vangnet weg, dus de lijst blijft zo klein mogelijk.
- Test: "a Chaos Sorcerer hit through a Bee" is aangepast. Na een treffer op de Bee (hit claim en 6x0A-vlaggen) zijn
  `held_only_by_unhit` en `event_without_living_enemy` voor golf 4 waar. Een treffer op de Sorcerer zelf maakt hem
  weer levend. De Pan Arms-asserts zijn ongewijzigd groen.
- Rood (de oude regel, elke ouder in `parent_index`):
  `run-static-tests: failure at .../src/StaticTests.cc:3285: !(spire_wave_held_only_by_unhit(wave(4)))`
  Dit blok gebruikt `expect` zonder runner, dus de fout breekt de run af. Er is daarom geen regel van de vorm "FAILED:".
- Groen: c42b6bc, exit 0.

## 4. 2r2-M2: een test die de netkamer-voorwaarde ziet — 72c9fa8
- Keuze: één blik van `spire_dead_event_wait`, zonder klok, staat nu in `spire_dead_event_look_counts`. De
  voorwaarde `if (!both.empty() || spire_net_rooms.count(key))` is letterlijk mee verhuisd. Het gedrag is
  ongewijzigd. De testhaak `spire_static_test_note_net_room` zet de netmarkering van een kamer.
- Test: een MapState met de events 111 -> 112 -> 113. 112 staat op 0x18 (geteld door het net), 113 is niet gestart,
  en seen en now zijn leeg. Met de markering geeft de functie {W-113}, zonder markering niets. Een andere kamer geeft
  ook niets.
- Rood (`|| spire_net_rooms.count(key)` weggehaald):
  `R2 (2r2-M2: after a net counted a non-last wave, the room mark alone starts the chain behind it (the look of spire_dead_event_wait)) FAILED: failure at .../src/StaticTests.cc:11018: 1u != counted.size()`
- Teruggezet: `git diff HEAD -- src/SpireFlagBridge.cc | wc -l` geeft 0, en regel 7723 is weer
  `if (!both.empty() || spire_net_rooms.count(key)) {`.
- Groen: 72c9fa8, exit 0.

## Genoteerde kan-punten (niet gebouwd)
- Systeem 7: geen open punten. K-1, K-2 en K-3 zijn meegenomen (zie fix 2).
- Systeem 2:
  - K3: het tweede vangnet slaat via de keten de rest van de kamer over voor een golf die niemand aanraakte, na 120 s
    stilte.
  - K4: "Enemy out of reach? Wait: the room opens by itself." kan al na 45 s lopen komen.
  - K5: de logcitaten in SpireFlagBridge.hh kloppen niet meer met de huidige `info_f`-regels.
  - De vier scenario's onder "Wacht op Fable" zijn niet gebouwd.
- Systeem 1: geen kan-punt op deze regels.
- Nieuw gezien:
  - De melding bij binnenkomst toont nu alleen de run van de episode van de game. Een Ep2-run wordt in een Ep1-game
    niet meer genoemd. Vroeger deelden ze één bestand, en een Ep2-quest staat toch niet in het menu van een Ep1-game.
  - Een nieuwe run naast een onleesbaar bestand krijgt bij de 'B' geen regel. Zijn eerste kamer schrijft niet, en de
    volgende kamer overschrijft het bestand zonder melding.

## Beslissingen (zelf genomen, binnen B-REGIE-21-22 en de opdracht)
- "Eerste eigen bewaarpunt" opgevat als de 'V' van de kamer na de startkamer. De 'V' van de startkamer is "de start".
  Reden: alleen zo kost een per ongeluk gestarte kamer 1 geen run.
- De eerste-kamerregel beschermt elke geparkeerde run met een ander id, ook een oefenrun en een onleesbaar bestand.
  Oefenrun tegenover oefenrun blijft zoals het was: de oefenregel zou anders iets onwaars zeggen.
- Ep1 houdt de oude bestandsnaam, zodat Ep1-runs zichtbaar blijven na een terugdraai.
- Een onleesbaar rung-bestand is van Ep1, op grond van zijn naam. Ep2 laat het staan.
- Pan Arms is het enige lichaam met delen. Sinow-spiegelbeelden zijn bewust niet opgenomen.
