# veilig-1 r252 (newserv): het run-einde van de server komt na een vloerlading opnieuw

Stand 2026-11-11, tak spike/veilig, start 9f99647, fix c3f494e. Geen pc, geen labrun, geen kaart.

## Verdict

Gebouwd en statisch groen. De server bewaart het run-einde r252 ("tijd op" of "speler weg") per spel naast de
kamerclears en stuurt het na elke vloerlading opnieuw aan de client die net laadde (1,5 s en 5 s na het vloerbericht,
zoals r251), zolang de run van het spel als beëindigd gemarkeerd staat. Een oude waarde bereikt geen nieuwe run.

## Dekking

Gedekt: of de server het run-einde r252 na een vloerlading opnieuw stuurt, en dat een oude waarde geen nieuwe run
bereikt. Niet gedekt: of een client het bericht tijdens zijn lading echt mist en of 1,5 s / 5 s na het vloerbericht lang
genoeg is op de Wii (alleen de Wii beslist), en de andere herhaalde registers (die zijn questkant).

## 1. Wat een tweede r252 op de client doet (questbron veilig-1 7cddbdb)

- In de beëindigde run: niets. Het einde draait één keer: `end` springt weg als de fase al 9 is
  (make_spire.py:3614-3616), en `t_timer` stopt bij fase 9 vóór hij r252 leest (make_spire.py:4626-4628, lezen op
  :4655-4659). `srv_elapsed` in het einde leest dezelfde waarde opnieuw (:3598-3610): een toestand, geen gebeurtenis.
- In een nieuwe run zou hij kwaad doen: zodra `run_started` staat (:2237, :4032) leest `t_timer` r252 (:4639 en
  :4655-4659) en gaat bij 'E' meteen naar het einde. De quest zet r252 zelf op 0 bij zijn start (:2252, :2683, :4036),
  maar een herhaling die daarna aankomt zou hem terugzetten.
- Daarom drie sloten aan de serverkant: (a) de bewaarde waarde hoort bij `SpireReplayState` en gaat dus mee weg bij
  een questlading (SpireFlagBridge.cc:2764) en bij de eerste 'I' na het einde (:6087, in `spire_run_again_forget_game`
  op :6245, vóór het OK op :6261; dezelfde TCP-verbinding, dus een eerdere herhaling komt vóór het OK aan en de quest
  zet r252 daarna op 0); (b) de start van de teller ('S') wist hem (`spire_run_timer_forget_end`, :1749-1752, vanuit
  :1836); (c) hij gaat alleen mee zolang het spel een run-einde heeft (`spire_run_end_for`, :7629), en dat merk wist
  ook een 'B' (:3050) en de questlading (:2758).
- Elke seconde herhalen tot de client zijn einde meldt (§ 7 van het questvoorstel) is niet gebouwd: de bestaande twee
  herhalingen na elke lading volstaan voor een toestand, en een dubbele r252 is in de beëindigde run onschadelijk.

## 2. Wat er veranderd is (c3f494e)

- src/SpireFlagBridge.cc:7515 `SpireReplayState::run_end` (optional), mee vergeten met de rest van de staat.
- :1743-1747 `spire_run_timer_record_end`: bewaart r252 en markeert het einde; de tick roept hem aan op :1806.
- :1749-1752 `spire_run_timer_forget_end`: wist het eindmerk en de bewaarde r252; `spire_run_timer_start` op :1836.
- :7607-7633 `spire_replay_values`: de lijst die `spire_replay_send` stuurt (andere schrijvers, r251 van de vloer,
  r252 als laatste). `spire_replay_send` stuurt alleen die lijst; zonder staat bij de eerste poging volgt nu toch de
  tweede (een einde dat er tussen beide komt, gaat dan mee).
- src/SpireFlagBridge.hh: declaraties en de uitleg van de herhaling. De startlevel-aanroepen niet aangeraakt.
- src/StaticTests.cc: sectie "Spire veilig-1 (B-AF-55)".

## 3. Rood en groen (letterlijk)

- Groen op c3f494e: `-- Spire veilig-1 (B-AF-55): the run timer's end r252 comes again after a floor load, never in a new run`
  en `-- All static tests passed`, exit 0.
- Rood zonder de fix (mutatie A: niets bewaren, zoals 9f99647): exit 1,
  `run-static-tests: failure at .../src/StaticTests.cc:8727: !(end.has_value())`.
- Rood zonder het wissen bij de start én zonder het eindmerk-slot (mutatie B): exit 1,
  `run-static-tests: failure at .../src/StaticTests.cc:8737: !(!replayed_end(client_id, floor))` (een nieuwe run kreeg
  de oude r252). Elk slot apart houdt de test groen; dat is de bedoelde dubbele dekking.
- Beide mutaties met een omgekeerde patch van mijn eigen hunk teruggezet; `git status --short src/` leeg.

## 4. Build

- `build/newserv help | head -1`: `newserv-c3f494e built 2026-11-11 19:55:42.000000 UTC`
- `build/newserv run-static-tests`: exit 0, laatste regel `-- All static tests passed`.

## 5. Zelf genomen beslissingen (voor decisions.md van de regie)

- r252 alleen herhalen zolang het spel een run-einde heeft, bovenop het wissen bij 'S' (klasse 2, opdracht "een oude
  waarde mag nooit in een nieuwe run komen").
- De keuze welke registers een replay stuurt in een vrije functie (`spire_replay_values`) gezet, zodat de statische
  test de echte route meet in plaats van een kopie (klasse 2).

## 6. Gezien, niet gebouwd

- De newserv-worktree heeft geen vastgelegde git-flow (integratietak, uitrol); een hook meldde dat bij de commit. Dat is
  voor de regie, niet voor dit spoor.
