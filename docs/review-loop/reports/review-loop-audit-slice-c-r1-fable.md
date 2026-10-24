# Review-loop audit-slice-c, ronde 1, fable (invalshoek: doel)

## Maatstaf en scope
`git diff 80e8986 HEAD` op fix/audit-slice-c, getoetst tegen de vijf doelpunten (S-12, S-3, S-6, Q2-2, StaticTests). Gelezen: Client.cc (is_spire_blocked, character_file), DataIndex.cc, Lobby.cc (join- en questchecks), SpireFlagBridge.cc (registerpad, follow-the-leader, aanroepers in ReceiveCommands/ReceiveSubcommands), SpireDraw.cc, StaticTests.cc, tests/spire-follow-city.test.sh.

## Wat er goed staat (getoetst, niet aangenomen)
- S-12 beslisfunctie: mutant A (loading-vlag genegeerd) en B (0x0F-check weg) worden rood in statische tests en in het shell-script.
- Q2-2: mutant C (grens 41) en D (`z > num_tags`) worden rood in de statische tests.
- S-3: is_spire_blocked vraagt bij een Challenge-quest alleen of het karakter beschermd is; zonder karakterdata faalt het dicht. Het overlay kopieert de naam (create_battle_overlay), dus de naamvergelijking blijft kloppen.
- S-6: bestandsnaam per karakter met character_file(false, false), dus nooit het overlay; zonder karakter wordt niets opgeslagen.

## Mutanten (letterlijk)
- A `return !partner_loading && ` -> `return (partner_loading || true) && `: static=RED follow-sh=RED
- B `(partner_floor != SPIRE_FOLLOW_UNLOADED_FLOOR) &&` weg: static=RED follow-sh=RED
- C `> SPIRE_DRAW_MAX_ENTRIES` -> `> SPIRE_DRAW_MAX_ENTRIES + 1`: static=RED
- D `z >= t.num_tags` -> `z > t.num_tags`: static=RED
- E quest-nummercheck in DataIndex::is_spire_blocked uit (na fix B2): static=RED
- F `if (challenge)` -> `if (false)` in Client::is_spire_blocked: static=green (vraagt een Client; niet statisch te dekken)
- G registerbestand zonder karakter-id: eerst green; na fix B3 (mutant G2) static=RED
- H `spire_follow_pending.insert` weg: static=green (coroutine met Client; niet statisch te dekken)

## Bevindingen
### B1 (major, raakt doel S-12): een overgeslagen partner bleef achter
Mutant: geen test kon het tonen; bewijs uit code. spire_follow_leader warpte alleen bij een vloerwissel van de leider. Een partner die tijdens het laden werd overgeslagen bleef op zijn oude vloer tot de leider weer wisselde, terwijl de log zei "its own floor load follows the leader again".
Fix: overgeslagen partners komen in `spire_follow_pending`; bij hun eigen geladen vloer (geen LOADING, niet 0x0F) warpen ze naar de huidige vloer van de leider via dezelfde spire_follow_should_warp. Het warp-pad gebruikt nu die getoetste functie met de echte loading-vlag. Leeg gemaakt bij queststart. Bewijs dat het op de Wii werkt: nog niet; dit is alleen gebouwd en statisch getest (mutant H blijft groen).
### B2 (minor, test S-3): beschermd-karakter-check had geen test
Fix: statische test voor is_spire_protected_character en is_spire_blocked (taalmarker, hoofdletters, lege naam, ander serienummer, niet-gelijste quest). Mutant E rood.
### B3 (minor, test S-6): bestandsnaam per karakter had geen test
Fix: spire_bridge_registers_filename uitgelicht en getest; mutant G2 rood.

## Verificatie na de fixes (letterlijk)
- cmake + `make -j$(nproc) newserv`: `[100%] Built target newserv`
- `build/newserv run-static-tests`: `-- All static tests passed`
- `sh tests/spire-follow-city.test.sh build/newserv`: alle `ok:` regels, laatste `ok: spire-lives-check`, rc 0

## Wat ik niet heb aangeraakt
- De Challenge-poort in Client (mutant F) en het pending-pad (mutant H) zijn alleen met een echte Client te toetsen; geen test toegevoegd.
- Ep2 heeft een echte vloer 0x0F (Gol Dragon): de 0x0F-marker moet daar vervangen worden (staat al in de header-comment).
- Het oude bestand `<serial>.json` met registers wordt niet meer gelezen of gemigreerd; bestaande registerstand van een karakter begint op 0. Bewust in de oorspronkelijke diff.
- Niets uitgerold, niets gepusht, geen processen gestopt.

## Stopadvies
Nog een ronde zinvol voor B1: dat is nieuwe productiecode op het follow-pad zonder dekkende test.
