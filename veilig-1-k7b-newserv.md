# K7b (newserv): de momentopname volgt alleen wat tussen spelers verhuist

Tak spike/veilig-k7b, start 771d844. Fix-commit 4f08618 (gelezen met git rev-parse).

## Regel
- Val (6x2A, 6x5D): de momentopname van A blijft zoals hij is. Het vloeritem krijgt een tegoed: wat A's
  momentopname zou verliezen (`spire_guard_floor_drop_credit`, berekend op een kopie), met A als valler.
- Opraap door een ANDERE speler (6x59, 6x5A): A's momentopname verliest het, hoogstens het tegoed; de raper
  krijgt hoogstens wat A verloor, alleen als zijn momentopname gevolgd wordt
  (`spire_guard_floor_pickup_step`). Volgt de raper niet, dan verliest A het toch (een ander heeft het).
- Opraap door A zelf: niets verandert. Niemand raapt op: niets verandert, A's herstel geeft het terug.
- Meseta: hetzelfde, met het minimum van de val en wat A's momentopname heeft.
- Nieuw, nodig voor "geen nieuw verlies": volgt A's momentopname na de val zijn 98 (basisgame of
  normaal spel), dan heeft die het item al niet meer; het tegoed is dan "afgerekend"
  (`spire_guard_floor_settle_dropper`, in `spire_guard_after_98`). Een latere opraap geeft het de raper
  zonder iets bij A weg te halen. Zonder dit zou een opraap ná A's 98 een ander exemplaar van dezelfde soort
  uit A's momentopname halen.
- Bestanden: src/SpireFlagBridge.cc/.hh, src/StaticTests.cc. ReceiveSubcommands.cc, 6x29, de ruil en de
  twee startlevel-aanroepen niet aangeraakt.

## Rood op 771d844, groen erna
Rood: tijdelijke stub met de semantiek van 771d844 (val haalt het meteen uit A's momentopname, opraap
geeft het tegoed aan een gevolgde raper) plus een niet-afbrekende expect_eq (2 regels extra; regelnummers
hieronder omgerekend naar het definitieve bestand):
- (h) 2365 `expect_eq(1u, count(after_login(snap, card), sword))` rood voor alle drie kaarten (oude stand,
  run-kopie, stand na de val): A had het zwaard niet meer.
- (i) 2379 `expect_eq(0u, step.taken + step.given)` rood (771d844 haalde het weg en gaf het terug); de
  telling erna (2381) was op 771d844 al 1: A die zelf opraapt verloor bij 771d844 niets.
- (j) 2394 `expect_eq(1000u, after_login(snap, card).second.stats.meseta.load())` rood voor alle drie kaarten.
- 2403 `expect(spire_guard_same_state(... a_real ...)) // the drop alone moves nothing` brak de rode run af.
Letterlijk: `RED .../src/StaticTests.cc:2367 1u != count(after_login(snap, card), sword)` (3x),
`RED .../src/StaticTests.cc:2381 0u != step.taken + step.given`,
`RED .../src/StaticTests.cc:2396 1000u != after_login(snap, card).second.stats.meseta.load()` (3x),
`failure at .../src/StaticTests.cc:2405` (stub-regelnummers).
Groen: alle K7b-tests en de K7-tests (A valt weg na opraap door B: geen dubbel; B valt weg: niet weg; ook
via de nieuwe stappen), afgerekend tegoed na A's 98, stapel en Meseta-minimum, 8-B1, 8-B2 en intern.
Oude test (c) (val zonder opraap: A krijgt het niet terug) is vervangen door (h): de regel is omgedraaid.

## Wat de speler merkt
Laat hij in de basis iets vallen dat niemand opraapt, of raapt hij het zelf weer op, en valt zijn Wii weg,
dan heeft hij het na de volgende login weer. Raapt zijn partner het op, dan staat het na wegvallen van
een van beiden bij precies één van hen.

## Rest-risico (genoteerd, niet gebouwd)
A laat iets vallen, valt weg en krijgt het bij zijn herstel terug, terwijl het spel van B nog loopt en het
item nog op de vloer ligt. Raapt B het daarna op, dan haalt de opraap het uit A's momentopname zolang die
gevolgd wordt (A's volgende login duwt het dan weg); is A's momentopname dan al gesloten (98 in een
normaal spel), dan heeft B het en A ook. Dichten vraagt het vloeritem weghalen bij A's vertrek: een
keuze buiten deze opdracht.

## Build en checks
- `build/newserv help | head -1`: zie het eindrapport (binary van de rapportcommit, de HEAD).
- `build/newserv run-static-tests`: exit 0, `-- All static tests passed`, 13x "GUARD E2E", 0x "FAILED"
  (op de fix-commit).
- `git diff --quiet ee0fa66 HEAD -- src/SpireDrops.cc src/SpireDrops.hh`: exit 0 (gelijk).

## Dekking
Deze opdracht dekt: items en Meseta die in de basisgame op de vloer vallen, met en zonder opraap door een
andere speler, gevolgd door wegvallen zonder 98. Niet gedekt: de ruil en 6x29 (een andere bouwer) en wat
de Wii werkelijk opslaat.
