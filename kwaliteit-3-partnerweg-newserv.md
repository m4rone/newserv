# kwaliteit-3 · systeem 6 punt M6: partner weg terwijl de ander ligt (newserv)

Dekking: deze opdracht dekt of een run die eindigt doordat de partner wegviel terwijl de ander lag, zijn bewaarpunt
houdt, en dat een echte nederlaag nog wist. Niet gedekt: het hervatten zelf op de client (alleen de Wii) en de andere
punten van systeem 6.

## Nameting op 7895fd9 (klopte)

Volgorde die tot wissen leidt:

1. Speler A ligt op zijn laatste leven in een gevecht; de Wii van B valt weg. newserv haalt B direct uit de lobby, dus
   `count_clients()` is meteen 1 en A krijgt de leave-melding.
2. Quest (veilig-1 HEAD 7cddbdb, `quests/spire/make_spire.py`): A's `t_out` pollt elke 15 frames (`spire.json:61`,
   `"dead": 15`), `t_fight` doet `get_number_of_players` en springt bij < 2 naar `t_end` (make_spire.py:4596-4602),
   `team_end('dead')` gaat naar het einde. De wachttijd op de partnerterm wordt overgeslagen als de partner weg is
   (make_spire.py:3659-3660), dus direct `score_and_persist` → 'K' en 'F', daarna 'X'.
3. Server: de runklok tikt maar elke seconde (`SPIRE_BRIDGE_TIMER_TICK`, SpireFlagBridge.hh:260). Komt A's 'F' eerder,
   dan roept de F-afhandeling `spire_run_mark_ended(l, "records: run finished")` aan (SpireFlagBridge.cc:3350 oud, ook 3259 voor een oefenrun) met
   `player_left = false`; `spire_savestate_end_deletes(false)` was `true` (cc:749-751 oud), dus
   `spire_savestate_on_run_end` wiste de bewaarpunten van A én B (ook B's bestand, via `savestate_files`).
4. De tik daarna (cc:1849 oud) ziet 1 < 2 en stuurt "player left", maar `mark_ended` is dan al gezet: niets meer te
   houden.

Wat de speler merkt: na opnieuw verbinden is er geen bewaarpunt meer; de 'J' van de kamerquest krijgt het antwoord
"fresh", de lobby toont geen "doorgaan"-regel, ze beginnen opnieuw. Valt de tik toevallig eerst, dan blijft het staan:
het hing af van de timing (0-1 s tegen ~0,3 s plus rondgang).

## Fix (commit bac2b35)

- `spire_run_timer_player_left(clients, clients_at_start)`: de bestaande regel van de klok, nu één functie die de tik
  zelf ook gebruikt.
- `spire_savestate_end_deletes(player_left, timer_running, clients, clients_at_start)`: houdt de bewaarpunten ook bij
  elk ander einde ('F', 'X') zolang de runklok nog loopt en al minder spelers telt dan bij zijn start, dus precies wat
  zijn volgende tik "speler weg" zou noemen.
- `spire_savestate_on_run_end` leest de klok (nieuwe `spire_run_timer_clients_at_start`); `spire_run_over` zet het
  einde nu vóór het stoppen van de klok, zodat ook een 'X' die eerst komt de klok nog ziet.
- Echte nederlaag (beiden liggen, partner nog in het spel): 2 van 2, wist zoals nu. Solo-run en een run zonder klok:
  wist zoals nu.

Dubbel uit het bewaarpunt: nee. Het bewaarpunt draagt alleen items van de runkopie (overlay, geen mags/Glass) en wordt
alleen in de runkopie teruggezet; het echte karakter krijgt niets. Splinters en Won tellen per run-id één keer
(`spire_records_credit_room`, `spire_records_finish_splinters`: "a run resumed after a partner-left 'F' still pays its
rooms", rooms al betaald = 0). Deze toestand (een 'F' gezien, bewaarpunt bewaard) bestond al als de tik eerst viel;
de fix maakt hem alleen niet meer afhankelijk van de timing.

## Rood / groen

Rood: de nieuwe regel tijdelijk teruggezet naar `return !player_left_end;` (ongecommit), build, `build/newserv
run-static-tests` exit 1:

```
E ... run-static-tests: failure at .../src/StaticTests.cc:5631: !(!spire_savestate_end_deletes(false, true, 1, 2))
```

Groen op bac2b35: `-- All static tests passed`, exit 0, 0 regels met `E `. De test dat een echte nederlaag nog wist:
`expect(spire_savestate_end_deletes(false, true, 2, 2));` (StaticTests.cc:5634), plus solo (1, 1) en zonder klok.

## Build

- `build/newserv help | head -1`: `newserv-bac2b35 built 2026-11-11 21:19:39.000000 UTC`
- `build/newserv run-static-tests`: exit 0
- `git diff --quiet ee0fa66 HEAD -- src/SpireDrops.cc src/SpireDrops.hh`: exit 0 (gelijk)

## Beslissingen

- Klok-regel gekozen boven `max_clients` van de run: de opdracht zegt "zoals bij speler weg (de regel die al geldt als
  de klok het eerst ziet)", en de regie-beslissing van 2026-10-25 (wissen bij elk echt einde, ook als er iemand weg is)
  blijft zo zo smal mogelijk geraakt. Klasse 2, omkeerbaar.

## Gezien maar niet gebouwd

- Een gewonnen 'F' in dezelfde seconde als het wegvallen van de partner houdt nu ook het bewaarpunt (zoals een tik die
  eerst valt al deed). Geen dubbele beloning (run-id), alleen een hervatbare gewonnen run. Kost: de winst-bit door
  `mark_ended` geven, ~10 regels.
- Zonder runklok (quest zonder 'S') blijft het oude gedrag: dan wist een einde met een weggevallen partner nog wel.
- `RunTimer.clients_at_start` telt bij de 'S'; viel de partner vóór 'S' weg, dan is dat 1 en geldt alles als solo.
