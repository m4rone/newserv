# Kwaliteit 3, bundel 3: meldingen (systeem 10), newserv-kant

Tak spike/kwaliteit-3, basis 0f65124, fix-commit 2321231. Alleen gelezen en statisch getest; geen labrun, pc niet aangeraakt.

**Dekking.** Deze opdracht dekt: de volgorde en de tussentijd van de serverregels bij een baas-klaar, een kamer-klaar en
de terugkeer naar de lobby/basis (10-M1 t/m 10-M3). Niet gedekt: de woordkeus en lengte van de teksten, en hoe lang de
GameCube een regel echt toont (alleen de Wii).

## Nameting op 0f65124 (alle drie kloppen nog)

- **10-M1** klopt. `SpireFlagBridge.cc:3826` start bij RESULT 1 met eerdere mag `spire_run_mag_evolve_text_later`, die
  na `SPIRE_RUN_MAG_EVOLVE_TEXT_DELAY` = 500 ms (`.hh:544`) direct stuurt (`:3718`). 'C' 10 gaat ook direct (`:4149`) en
  houdt de rij niet vast.
- **10-M2** klopt. Klaar-tekst vast op +1 s buiten de rij (`:6750`, `.hh:93`). Het 'Q'-antwoord stuurt het overzicht
  direct (`:1961`), en 'C' 2 stuurt hetzelfde bewaarde overzicht nog eens direct (`:4149`). 'U' 0 ook direct (`:4149`).
- **10-M3** klopt. Alle lobbyteksten in één lus (`:489`), en de basisgame na vaste 4 s (`:5893`, `.hh:1219`), 1 s na de
  teksten (+3 s, `.hh:1209`).

## Fix (SpireFlagBridge.cc/.hh; SpireMag.cc niet nodig; startlevel-aanroepen niet geraakt)

- **10-M1.** 'C' 10 en 'C' 12 met bit 23 (de examenbeloning) gaan nog steeds meteen, maar houden de rij per speler 4 s
  vast (`spire_bridge_line_holds` + `spire_ticker_hold`, zoals de baasdeurregel). De evolutietekst neemt na zijn 500 ms
  een beurt in de rij (`spire_ticker_turn_delay`).
- **10-M2.** De klaar-tekst gaat via de rij, op +1 s of later (`spire_ticker_turn_from`). Het overzicht van 'Q' neemt
  zijn beurt (dus 4 s na de klaar-tekst) en controleert bij vertrek of het nog het bewaarde overzicht is (een Reroll of
  een verdiepingswissel laat een oud overzicht vallen). 'C' 2 en 'U' 0 nemen ook hun beurt; 'C' 2 stuurt het overzicht
  niet nog eens zolang het van 'Q' nog wacht of staat (`SpireOverviewKept::stands_until`, `spire_overview_repeat_due`).
- **10-M3.** De lobbyteksten gaan één voor één, 4 s uit elkaar (`spire_lobby_text_at`); zit de speler intussen in een
  game, dan wachten de rest op de volgende lobby. De basisgame wacht tot de laatste tekst 4 s heeft gestaan
  (`spire_base_game_settle`, minimaal de oude 4 s).

Questbron (veilig-1 7cddbdbd, alleen gelezen): de quest leest geen tickerregel, alleen registers. r251 en de
'Q'-registers (DONE) gaan nog meteen; alleen de B0-tekst wacht. De herhaling van 'C' 2 na `PROMPT_SECONDS` = 20 s
(`make_spire.py:164`) stuurt het overzicht weer, want de blokkade loopt af 4 s na het overzicht. Geen wachtpunt komt
dus later dan de quest erop wacht. De quest hoeft niets te veranderen.

## Wat de speler merkt

- Baas met run-mag: "Exam reward: …" blijft 4 s staan; "your mag evolved …" komt ongeveer 3,5 s later dan nu (op +4 s).
- Gewone kamer-klaar: "Room clear! …" nog steeds op +1 s; het beloningsoverzicht één keer, op +5 s na de klaar (nu
  meteen en dubbel); "You are back on your feet." daarna (tot ongeveer +9 s).
- Lobby na 'O': de teksten 4 s na elkaar (+3, +7, +11 s); de basisgame volgt 4 s na de laatste (1 tekst 7 s, 3 teksten
  15 s, in plaats van 4 s).

## Rood en groen (StaticTests.cc, sectie kwaliteit-3)

Rood: dezelfde tests tegen de helpers met het oude gedrag (vóór de fix), `run-static-tests` exit 1:

```
K3 10-M1: 43000A00 at +50 ms, the evolution text at +500 ms
K3 (10-M1: …) FAILED: failure at …/src/StaticTests.cc:8503: !(evolve_at + evolve >= exam_at + gap)
K3 10-M2: 'Q' at +0 ms: clear text at +1000 ms, overview at +0 ms
K3 (10-M2: …) FAILED: failure at …/src/StaticTests.cc:8528: !(q_at + overview >= t0 + clear + gap)
K3 10-M3: 1 text(s): last at 3000 ms, base game at 4000 ms
K3 (10-M3: …) FAILED: failure at …/src/StaticTests.cc:8555: !(settle >= spire_lobby_text_at(n - 1) + gap)
```

Groen na de fix, exit 0:

```
K3 10-M1: 43000A00 at +50 ms, the evolution text at +4050 ms   (ook 43830A40, 43830C40, 43800C00)
K3 10-M2: 'Q' at +1200 ms: clear text at +1000 ms, overview at +5000 ms   (ook +0 … +1100 ms)
K3 10-M3: 3 text(s): last at 11000 ms, base game at 15000 ms   (1 t/m 8 teksten)
-- All static tests passed
```

Eén bestaande test kreeg alleen een vierde veld `{}` in de initialisatie van `SpireOverviewKept` (geen assertie
veranderd).

## Build

- `build/newserv help | head -1`: `newserv-2321231 built 2026-11-11 20:23:47.000000 UTC`
- `build/newserv run-static-tests` op die binary: exit 0, 0 keer FAILED, laatste regel "All static tests passed".
- `git diff --quiet ee0fa66 HEAD -- src/SpireDrops.cc src/SpireDrops.hh`: exit 0, SpireDrops gelijk.

## Vervolg: bovengrens voor de basisgame (commit 4d601b4)

Gemeten in de bron: een volger wacht `SPIRE_BASE_GAME_LEADER_WAIT_POLLS` = 40 keer 500 ms = 20 s op de basisgame van de
leider (`SpireFlagBridge.hh:1244`, de lus in `SpireFlagBridge.cc:5999-6004` op 54f6958), te tellen vanaf zijn eigen
wachttijd van minstens 4 s. 2321231 liet de leider met 5 of meer teksten langer wachten dan dat.

- Bovengrens `SPIRE_BASE_GAME_SETTLE_MAX` = 16 s: 4 s onder de 20 s van de volger. De basisgame komt nooit later dan
  dat.
- In de lobby gaan alleen de teksten die vóór die grens nog 4 s kunnen staan, en dat zijn er drie (+3, +7, +11 s).
  De basisgame komt dan op 15 s.
- Wat er niet meer in past, gaat **door in de basisgame** en valt niet weg. Zodra de basisquest loopt en de speler niets
  meer laadt, gaan die teksten in volgorde door de rij per speler: de eerste 3 s later, daarna steeds 4 s ertussen.
  Waarom doorlopen en niet wegvallen: dit zijn de enige meldingen van wat er is vrijgespeeld, en de rij kan ze in de
  basisgame net zo goed tonen als tijdens de run (als één regel, net als elke regel in een game).
- Is de speler binnen 120 s (3 x `SPIRE_BASE_GAME_WAIT`) niet in een basisgame met een lopende quest, dan wachten die
  teksten weer op de volgende lobby. Elke tekst wordt dus verstuurd of opnieuw bewaard, nooit allebei. Daarom komt geen
  tekst twee keer.
- Gaat een speler niet naar een basisgame (alleen terug naar de lobby), dan blijven alle teksten in de lobby, 4 s uit
  elkaar, zoals in 2321231.

Rood op 54f6958 (het gedrag van 54f6958, waarin alle teksten in de lobby gaan; alleen de nieuwe functie bestond als
stub), `run-static-tests` exit 1:

```
K3 10-M3 r2: 6 text(s): base game at 27000 ms (bound 16000 ms), 6 in the lobby
K3 (10-M3: …) FAILED: failure at …/src/StaticTests.cc:8570: !(settle <= SPIRE_BASE_GAME_SETTLE_MAX)
```

Groen op 4d601b4, exit 0:

```
K3 10-M3: 3 text(s), 3 in the lobby: last there at 11000 ms, base game at 15000 ms
K3 10-M3 r2: 6 text(s): base game at 15000 ms (bound 16000 ms), 3 in the lobby
K3 10-M3 r2: 8 text(s): base game at 15000 ms (bound 16000 ms), 3 in the lobby
-- All static tests passed
```

De lus 1 t/m 8 in de bestaande 10-M3-test toetst nu de laatste tekst die in de lobby gaat, niet de laatste tekst van
allemaal. Dat gedrag is bewust veranderd. Het geval met 3 teksten is ongewijzigd groen: basisgame op 15000 ms.

Build: `build/newserv help | head -1` gaf `newserv-4d601b4 built 2026-11-11 20:33:53.000000 UTC`.
`run-static-tests` op die binary: exit 0, 0 keer FAILED. `git diff --quiet ee0fa66 HEAD -- src/SpireDrops.cc
src/SpireDrops.hh`: exit 0, SpireDrops nog gelijk.

## Gezien maar niet gebouwd

- De oude 'ROOM'-route (register 248, `SpireFlagBridge.cc` 0f65124 rond 4096) stuurt de klaar-tekst nog direct; die
  speelt alleen in questbuilds zonder serverklaar (niet q921).
- Andere vaste "later"-regels buiten de drie moet-punten (hervat-mag na 4 s, tweede bewaarpuntregel na 4 s) gaan nog
  buiten de rij.
- Stapt de speler binnen ongeveer 5 s na de klaar, dan komt het overzicht ("… Walk to choose.") terwijl de lijst al
  open is. De server weet niet of de lijst open is (dat is r103 in de quest).
- (Opgelost in 4d601b4: met 5 of meer lobbyteksten wachtte de leider langer dan de 20 s van de volger.)
- Een tekst die in de basisgame op zijn beurt wacht en de speler verlaat die game vóór zijn beurt, valt weg. Dat is
  hetzelfde gedrag als bij elke regel in de rij tijdens een run.
- Gaat een speler al tijdens de lobbyteksten een game in, dan wachten de lobbyteksten die nog niet weg zijn op de
  volgende lobby. De teksten voor de basisgame lopen dan wel door, dus de volgorde kan dan door elkaar raken (geen
  tekst dubbel).
- Lukt de basisgame niet (de speler blijft in de lobby), dan komen de overgebleven teksten pas bij de volgende lobby
  en niet meteen in deze.
