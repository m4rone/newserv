# kwaliteit-3 bundel 3 — run-mag na "nog een run" (newserv, punt 4-M2)

## Verdict
Gebouwd en statisch groen. Na een geverifieerde "nog een run" ('I') krijgt die speler in hetzelfde spel weer een
run-mag; elke speler zonder eigen geverifieerde 'I' houdt de weigering. Niet op de Wii of in het lab gemeten.

## Dekking
Deze opdracht dekt: of een nieuwe run in hetzelfde spel aan de serverkant een run-mag krijgt zonder twee mags of een
blijvende mag. Niet gedekt: de timing van de mag tegenover golf 1 op de client (dat is een andere poort, questkant) en
de andere punten van systeem 4 (4-M1, 4-M3).

## Wat de blokkade beschermde
- Plan route B, amendement 4 (`new-features/roguelike/output/plan-route-b-2026-10-26.md:20`): "'Z' after 'I': refuse
  'Z' in a game where an 'I' happened ... the second run gets no mag; that is the safe degrade". Gebouwd in 4891acc.
- Het gevaar: een 'Z' terwijl de server-overlay of het geheugen van de client niet de verse run-kopie is. Na de
  basisterugkeer ('O') heeft de client het echte karakter met de echte mag; de 61's van de 'I'-push
  (`src/SpireFlagBridge.cc` spire_run_again_push, de 30 ná het maken van de overlay) schrijven dat echte karakter in
  de overlay (`on_61_98` schrijft in `c->character_file()`, dat is de overlay, ReceiveCommands.cc:3469). Een 'Z'-swap
  op die overlay zou de echte mag wissen of een run-mag naast de echte zetten. Bij een mislukte push blijft dat zo.
- Een tweede laag deed hetzelfde: elke 61 van de push zet amendement 3 ("refreshed by 61", REFRESHED_BY_61) voor die
  client. Alleen de spelmarkering weghalen was dus niet genoeg; de run kreeg dan nog steeds geen mag.
- Wat blijft beschermen en niet geraakt is: de snapshot wordt bij 'I' alleen van het echte karakter genomen
  (spire_guard_run_again_retake, run-kopie-handtekening amendement 6), en het run-einde zet de snapshot terug, dus een
  run-mag blijft nooit na de run.

## Wat gebouwd is (b27f2bb)
- `spire_run_again_note_started`: de spelmarkering zoals voorheen; wist de eigen eerdere opening, en bij de eerste 'I'
  na een run-einde alle openingen in dat spel (die van een partner hoort bij de run die net eindigde).
- `spire_run_again_note_verified`, aangeroepen direct na "run copy verified" zonder co_await ertussen: opent 'Z' voor
  deze client alleen als het spel een 'I' had, de overlay gelijk is aan de run-kopie die de server maakte
  (spire_guard_same_state) en geen mag (type 02) bevat. Dan ook amendement 3's markering weg, want de overlay is weer
  de eigen run-kopie van de server. Een latere in-game 61 zet die markering opnieuw.
- `spire_run_mag_game_marks`: de 'Z'-handler leest run_again en refreshed_by_61 via deze ene functie.
- Questlading en de static-test-opruiming wissen de opening mee.
- Verder niets aangepast: geen weigeringsvolgorde, geen startlevel-aanroep, geen SpireDrops, geen questkant.

## Rood / groen
- Rood (note_verified tijdelijk `return false`, dus het gedrag van vóór de fix):
  `K3 (4-M2: a verified run again opens 'Z' for that player alone; every other path keeps it off) FAILED: failure at
  .../src/StaticTests.cc:8439: std::string("NONE") != z(a)`, run-static-tests exit 1.
- Groen (b27f2bb): `-- All static tests passed`, exit 0.
- De test dekt ook dat de bescherming blijft: partner zonder eigen 'I' RUN_AGAIN; run-kopie met mag, of overlay die niet
  de geverifieerde kopie is: geen opening; mislukte push: RUN_AGAIN; latere in-game 61: REFRESHED_BY_61; geen 'I' in
  het spel: niets te openen; na een run-einde neemt de eerste 'I' de opening van de partner terug; questlading wist alles.

## Build
- `build/newserv help | head -1`: `newserv-b27f2bb built 2026-11-11 20:08:08.000000 UTC`
- `build/newserv run-static-tests`: exit 0
- `git diff --quiet ee0fa66 HEAD -- src/SpireDrops.cc src/SpireDrops.hh`: exit 0 (gelijk)

## Gezien maar niet gebouwd
- Amendement 4 zegt dat 'I' "de vorige run-kopie live laat"; de code maakt sinds v49 de overlay opnieuw
  (spire_run_again_push: delete_overlay + spire_create_battle_overlay). De zin in het plan is verouderd.
- De partner die zelf geen 'I' doet (alleen de leider start opnieuw) krijgt nog steeds geen mag; de quest laat volgens
  make_spire.py:1466 elke client een eigen 'I' sturen, dus dat pad zou niet moeten bestaan. Niet op de client gemeten.
- expect_eq in StaticTests drukt de waarden niet af, alleen de expressie; de rode regel noemt dus niet "RUN_AGAIN".
