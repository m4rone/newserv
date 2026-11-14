# Balans v1: bundel 4 op het bankpaar

Tak spike/balans-v1. Merge 7bd1c3d (ouders 0fffb30 bankpaar, 52575b75 bundel 4), testfix abf03bc.
Bouw voor de quest-kant op abf03bc (`newserv help`: newserv-abf03bc).

## Conflicten en keuze (beide kanten behouden)
- src/ReceiveSubcommands.cc:2134-2136 (6x5D, deel van een stapel of meseta op de vloer): K7-regel
  `spire_guard_note_floor_drop` (bankpaar) en `inventory_backup_note_dropped_partial` (bundel 3) staan er allebei.
  Ze schrijven in iets anders (snapshotkrediet tegenover backup-herkomst), dus geen van beide valt weg.
- src/ReceiveSubcommands.cc:2376-2377 (oppakken): `spire_guard_note_floor_pickup` en `inventory_backup_note_picked_up`,
  zelfde reden. Bij het hele-item-drop (regel 2046-2048) had git ze al samen gezet, in dezelfde volgorde.
- src/SpireFlagBridge.cc:1118-1119 (SpireClientRun): veld `practice_kept_told` (7-M2) en `savestate_kept`
  (kwaliteit-3 plek 2) naast elkaar. In het opslagpad sluiten ze elkaar niet uit: plek 2 slaat de 'V' over,
  7-M2 werkt alleen binnen een opslag.
- src/StaticTests.cc:8883-13939: beide kanten voegden nieuwe secties in op dezelfde plek (na basisregel 7582).
  Eerst de bankpaar-secties (8883-10839), dan die van bundel 4 (10840-13939), elk byte-gelijk aan zijn ouder.
  Geen dubbele sectienamen (gemeten: `sort | uniq -d` leeg), dus geen hernummering nodig.

## Statische tests
- Eerste run na de merge: exit 1, `StaticTests.cc:11452: !(replaced)` in "K3 (plek 2 ...)".
  Oorzaak is de merge: die test staat alleen op 52575b75 en verwachtte daar dat een verse run de geparkeerde
  savestate vervangt. Op 0fffb30 bestaat de test niet; daar geldt 7-M2 (een oefenrun schrijft nooit over een
  echte geparkeerde run). Log: "'V' of practice run ... not stored: the real run parked ... stays (7-M2)".
- Keuze: het bankpaar wint (hervatten). De twee "as before"-gevallen (geen leider op slot 1+2, en k19 alleen op
  slot 1) verwachten nu `!replaced` en de oefentekst precies 1 keer (StaticTests.cc:11455, 11478). De bedoeling van
  bundel 4 (geen alleen-tekst, vers antwoord) blijft getoetst.
- Eindrun op abf03bc: exit 0, `-- All static tests passed`.

## Metingen
- `git diff --quiet 52575b75 HEAD -- src/SpireDrops.cc src/SpireDrops.hh`: exit 0 (gelijk aan bundel 4).
- `git diff --quiet 0fffb30 HEAD -- src/SpireDrops.cc src/SpireDrops.hh`: exit 1 (anders dan het bankpaar).
- `git diff --stat 0fffb30 HEAD -- src/SpireFlagBridge.cc`: 340 regels geraakt (302 erbij, 38 eruit).
- src/Revision.cc was geen symlink maar een gegenereerd bestand; unlink niet nodig.

## Dekking
Dit dekt de merge, de build en de statische tests van newserv. Het dekt niet het gedrag op de Wii.
