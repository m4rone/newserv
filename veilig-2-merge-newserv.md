# Veilig-2: samenvoeging newserv (spike/veilig-2)

## Wat samengevoegd is
- Basis: spike/veilig-ruil 363e4c5 (tests van intern met B1 en B2 aan, ruilcorrectie).
- Erin: m4rone spike/veilig-k7b 4641c07 (bovenop 771d844): een val telt pas bij opraap door een andere speler;
  vertrekt A terwijl een ander in de game blijft, dan verliest A zijn open vloeritems in de momentopname.
- Merge-commit: 8ad4acc.

## Conflictblokken
- src/SpireFlagBridge.hh, commentaar boven spire_guard_floor_follows: tekst van k7b genomen (val pas bij opraap
  door een ander, vertrekregel); de slotzin van veilig-ruil teruggezet (een 6x29 op zich verandert niets, het
  ruilvenster wordt gevolgd bij uitvoering). Alleen commentaar, geen code.
- Verder geen botsingen: SpireFlagBridge.cc, Lobby.cc en StaticTests.cc voegden automatisch samen.

## Oude K7-test (c)
- "niemand raapt op: A krijgt het niet terug" kwam niet terug via de ruiltak (grep op de oude tekst: geen treffer);
  (h), (j) en (m) van k7b staan erin. Niets weggelaten.

## Tests en build
- `build/newserv help | head -1`: `newserv-8ad4acc built 2026-11-11 22:31:01.000000 UTC` (geen "+").
- make exit 0; binary sha256 6827bee5fe8b24646007c1389997ba37d6c7744a8f15a30ce97f8a6066e4e1a6.
- `build/newserv run-static-tests` exit 0, slotregel "All static tests passed"; GUARD E2E 16, FAILED 0.
- Gedraaid: "Spire guard: a floor hand-over ... (K7)" en "Spire guard: a trade through the trade window ... (Veilig-2)".
- `git diff --quiet ee0fa66 HEAD -- src/SpireDrops.cc src/SpireDrops.hh`: exit 0, SpireDrops gelijk.

## Dekking
Dit dekt: dat de drie takken samen bouwen en dat alle statische tests (ook de tests van intern en de ruil- en
K7-tests) groen zijn. Niet gedekt: wat de Wii doet, en de quest-kant van de levering.
