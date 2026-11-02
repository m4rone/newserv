# Route B bundel 9, newserv deel E: rapport

Worktree `/home/m4rck/code/worktrees/newserv/route-b-9`, tak `spike/route-b-9`, basis 5e374c8. Niet gepusht.

## Dekkingsclaim

Dekt: 'C' 12 aan de newserv-kant en de bewaarpunt-melding, met hun StaticTests. Dekt niet: § 3 (uitrusten, r15,
'C' 13/14), de quest, en wat de GameCube-client met een tickerregel doet (lengte op het scherm beslist alleen Dolphin
of de Wii).

## Commit

`ea18962` feat: Spire 'C' 12 exam text and save point notice at game join (beide punten in één commit: ze delen
`SpireFlagBridge.cc/.hh` en `StaticTests.cc`). Bestanden: `src/SpireFlagBridge.cc`, `src/SpireFlagBridge.hh`,
`src/Client.hh`, `src/ReceiveCommands.cc`, `src/StaticTests.cc`. Dit rapport en `ROUTEB9-NEWSERV-E-VOORTGANG.md` zijn
bewust niet gecommit (werkbestanden, houden de merge met de equip-tak schoon).

## Punt 1: 'C' 12 (contract § 1)

- `spire_text_exam_reward_waiting` (SpireFlagBridge.cc), `case 12` in `spire_bridge_message_text`; de handler laadt
  draw-tabel en archetype nu ook voor punt 12 (zelfde regels als 'C' 10). De naam-opzoeking van 'C' 10 is
  uitgetrokken naar `spire_text_archetype_entry_name` en door beide gebruikt (gedrag 'C' 10 ongewijzigd, tests groen).
- Vormen, eerste die past: "Exam reward: M meseta, <extra>. Still waiting: <rare>. Make room." -> zonder ", <extra>" ->
  "Exam reward: M meseta. A rare still waits: make room."
- Header-commentaarblok (SpireFlagBridge.hh, na de 'C' 11-regel) bijgewerkt; `SpireBridgeTextContext::archetype`
  noemt 'C' 12.
- Compatibiliteit 5e374c8: een onbekend 'C'-punt valt in `default: return ""` van `spire_bridge_message_text`; de
  handler doet dan `if (msg.empty() && kind == 'C') return true;` ("a retired wait point: no text, no log line").
  Een oude newserv neemt 'C' 12 dus stil aan: geen tekst, geen logregel, geen fout. Klopt met de compatibiliteitsregel.
- Bereikbaarheid: de loader kapt een entry-naam op 22 en een extra op 24 tekens. De volle vorm kan dan 101 tekens
  worden (eerste terugval bereikbaar); de vorm zonder extra is hooguit 75, dus de laatste terugval is met een geladen
  draw.json onbereikbaar en staat er als vangnet (getest met een naam die na het laden is gezet).

## Punt 2: bewaarpunt-melding (contract § 2)

- `spire_savestate_notice_on_game_join(c, l)` (SpireFlagBridge.cc), aangeroepen in `on_6F` (ReceiveCommands.cc) direct
  na `spire_guard_battle_join`. Pure delen voor de tests: `spire_savestate_room_quest_name`,
  `spire_savestate_notice_saved_line`, `spire_savestate_notice_continue_line`, `spire_savestate_notice_take`.
- Client-veld: `Client::spire_savestate_told` (Client.hh, naast `inventory_loss_told`).
- Tickerhelper: `spire_send_ticker` (regel 1, meteen) en `spire_send_ticker_later` (regel 2, na
  `SPIRE_SAVESTATE_NOTICE_SECOND_DELAY` = 4 s, alleen als de client nog in die game zit). Beide via
  `send_text_message` (B0), regeleinden tot spaties.
- Questnaam: `quest_index->get("Spire room <k>[ elite| rest]")`, alleen geldig als de episode van de quest gelijk is
  aan die van het bewaarpunt en de quest een versie voor de clientversie heeft; de menunaam komt uit de
  categorie-index van die quest (op de pc "PSO Spire: losse kamers"). Niet gevonden, geen categorie, of regel boven 90:
  "To continue: type $spire continue."
- Niet gestuurd: client zonder GC-licentieserial of karakterdata, geen bewaarpunt, onleesbaar bewaarpunt (gelogd door
  `spire_savestate_load_logged`), of al gestuurd in deze sessie. Alles in try/catch: gooit nooit.

## Tests (letterlijk)

Fail-first: build met stubs (case 12 leeg, de vier notice-functies leeg; b8-eindcontrole tijdelijk naar een logregel
zodat het B9-blok ook draaide), `./build/newserv run-static-tests` exit 134:

```
E ... B8 (B9 'C' 12 the exam reward while an older rare waits) FAILED: failure at .../StaticTests.cc:5456: std::string("Exam reward: 600 meseta, Photon Drop x2. Still waiting: Lavis Cannon. Make room.") != spire_bridge_message_text(0x43830C21, with_names)
E ... B9 (the room quest name the build gives) FAILED: ... std::string("Spire room 1") != spire_savestate_room_quest_name(b9_savestate(1, 0, 3, 0))
E ... B9 (normal, elite and rest: both lines) FAILED: ... size_t(2) != lines.size()
E ... B9 (a missing field is left out) FAILED: ... std::string("Saved run: Room 5 elite, 2 lives.") != spire_savestate_notice_saved_line(ss)
E ... B9 (the room quest not found: the $spire continue line) FAILED: ... std::string(SPIRE_SAVESTATE_NOTICE_NO_QUEST_TEXT) != spire_savestate_notice_continue_line("", "")
E ... B9 (once per session, and nothing without a savestate) FAILED: ... size_t(2) != spire_savestate_notice_take(&told, b9_savestate(2, 0, 3, 90), category, "Spire room 2").size()
E ... B9 (every notice line one line of at most 90 characters) FAILED: ... !(!line.empty() && (line.size() <= SPIRE_BRIDGE_TEXT_MAX) && (line == spire_ticker_line(line)))
```

Stubs teruggezet (geen "FAIL-FIRST" meer in de bron), daarna twee eigen testfouten gerepareerd (de laatste-vorm-fixture
ging via de loader, die namen op 22 kapt; 3599 s is 59:59, niet 60:00). Eindrun op ea18962: build `[100%] Built target
newserv`, 0 warnings, exit 0:

```
B8 (B9 'C' 12 the exam reward while an older rare waits)
B9: 393216 'C' 12 texts checked
-- Spire bundle 9 (ux-pass): the save point notice at a game join (ROUTEB9-PROTOCOL.md § 2)
B9 (the room quest name the build gives)
B9 (normal, elite and rest: both lines)
B9 (a missing field is left out)
B9 (the room quest not found: the $spire continue line)
B9 (once per session, and nothing without a savestate)
B9 (every notice line one line of at most 90 characters)
B9: 1728 notice lines checked, longest 75 characters
-- All static tests passed
```

Wat de test "tweede on_6F stuurt niets" feitelijk meet: de poort `spire_savestate_notice_take` met hetzelfde
`told`-veld (tweede en derde aanroep leeg, `told` blijft waar). `on_6F` zelf draait niet in de static tests; dat
die de poort met `c->spire_savestate_told` aanroept staat in de code, niet in een test.

## Keuzes (beslis-log, nog over te nemen in de decisions.md van de epic; ik schrijf buiten deze boom niet)

### B-E09-E1 — 'C' 12 zonder bekende rare-naam
- **Datum:** 2026-10-27
- **Route:** zelf
- **Type:** technisch (tekst)
- **Klasse:** 2
- **Dekking:** ROUTEB9-PROTOCOL.md § 1 ('C' 12: "missing parts left out", laatste vorm "A rare still waits: make room.")
- **Beslissing:** onbekende wachtende rare -> de woorden van de laatste vorm, met de extra als die past.
- **Waarom:** letterlijk weglaten gaf "..., <extra>. Make room." zonder te zeggen waarvoor; de contractwoorden dekken het.

### B-E09-E2 — told pas zetten als de melding echt uitgaat
- **Datum:** 2026-10-27
- **Route:** zelf
- **Type:** technisch
- **Klasse:** 2
- **Dekking:** ROUTEB9-PROTOCOL.md § 2 ("Not sent when the client has no save point ... or a second time in the same session")
- **Beslissing:** `spire_savestate_told` wordt alleen gezet wanneer de twee regels gestuurd worden.
- **Waarom:** "eenmaal per sessie" gaat over het sturen; een bewaarpunt dat later in de sessie ontstaat (run afgebroken
  door een partner-crash) wordt dan bij de volgende game-join nog gemeld.

### B-E09-E3 — tweede regel 4 s later, categorienaam uit de index
- **Datum:** 2026-10-27
- **Route:** zelf
- **Type:** technisch
- **Klasse:** 2
- **Dekking:** ROUTEB9-PROTOCOL.md § 2 (twee tickerregels; questnaam uit de questindex); bestaand patroon
  SPIRE_RESUME_MAG_TEXT_DELAY (SpireFlagBridge.hh)
- **Beslissing:** regel 2 via `spire_send_ticker_later` na 4 s; de menunaam is de naam van de categorie van de
  gevonden quest; de quest telt alleen bij gelijke episode en een versie voor de client.
- **Waarom:** twee B0-regels direct na elkaar duwen de eerste weg (v36-meting); de categorienaam is wat de speler in het
  menu ziet; een "Spire room k" van een andere episode of versie zou de speler naar een quest sturen die hij niet heeft.

## Gezien maar niet gebouwd

- tekst · "1 lives" / "0 lives" in "Saved run" (contract-letterlijk, zelfde vorm als de bestaande resume-ticker) ·
  SpireFlagBridge.cc `spire_savestate_notice_saved_line` · klein (enkelvoud bij 1).
- ux · de melding komt ook bij de eerste game-join als die game al de kamerquest van dat bewaarpunt draait (meedoen
  met de game van de partner): dan staat "To continue: Quest > ..." op het scherm terwijl hij al in die kamer zit ·
  `spire_savestate_notice_on_game_join` · klein (overslaan als `l->quest` de gevonden quest is).
- dekking · `on_6F` zelf (het aanroepen met het Client-veld) heeft geen static test; alleen de pure poort ·
  ReceiveCommands.cc on_6F · een harnas met een nep-Client zou nodig zijn.
- merge · de equip-tak (route-b-9-equip) voegt waarschijnlijk ook commentaar toe na de 'C' 11-regel in
  SpireFlagBridge.hh en een testblok vlak vóór "-- All static tests passed": verwacht een kleine, tekstuele
  merge-conflict op die twee plekken.
- harnas · de werkwijze-hook meldde bij de commit dat de newserv-worktree geen `Integratiebranch:`/`Uitrol:` heeft
  (.claude/omgeving.json of CLAUDE.md § Git flow) · alleen een waarschuwing, niets geweigerd; niet door mij ingevuld.

## Blockers

Geen.
