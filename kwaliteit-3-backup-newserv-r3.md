# kwaliteit-3 back-up: fixronde 3, op de tweede critic (newserv 6cedb4b..dc6c6a9)

Datum 2026-11-11. Tak spike/kwaliteit-2, gepusht naar m4rone.

**Dekking.** Deze opdracht dekt: de punten B1, B2 en M1-M6 van de tweede critic, als statische tests op de
serverbron. Niet gedekt: een echte harde stop van een draaiende server met een client erop (geen labrun), en de
kan-punten van de critic tenzij ze op het pad van een moet-punt liggen.

Commits: e2be77a (B1, het halve werk van de vorige bouwer, gelezen en groen bevonden) en dc6c6a9 (B2, M1-M4, M6).
Binary: `build/newserv help | head -1` gaf `newserv-dc6c6a9 built 2026-11-11 20:03:08.000000 UTC`.
`build/newserv run-static-tests`: exit 0, laatste regel `-- All static tests passed`, 0 regels FAILED.
`git diff --quiet ee0fa66 HEAD -- src/SpireDrops.cc src/SpireDrops.hh`: exit 0, dus gelijk.

Rood is gemeten per punt: de fix in de gebouwde bron tijdelijk teruggedraaid (een mutatie per punt, alleen
InventoryBackup.cc), gebouwd, tests gedraaid, daarna de bron teruggezet (`git diff --quiet HEAD -- src` gaf schoon)
en opnieuw gebouwd.

## Per punt

- **B1 (bewaakte Spire-game telt niet als verder gespeeld).** Gebouwd door de vorige bouwer, niet gecommit; ik
  heb het gelezen en ongewijzigd gecommit. `played_on` kent geen Spire-uitzondering meer; een opzij gezette back-up
  telt ook als verder gespeeld. Test: de varianten SPIRE_98, SPIRE_98_GAVE, SPIRE_EXIT_GAVE en SPIRE_EXIT_SAME.
  Rood: `StaticTests.cc:9335: KbR::SUPERSEDED != d.refusal`. Groen erna.
- **B2 (rotatie gooit, back-up buiten de boekhouding).** `inventory_backup_rotate` is een eigen functie die nooit
  gooit; `inventory_backup_save` geeft het pad terug zodra het bestand er staat, en meldt een rotatiefout apart; de
  join zet `current` en `open_backups` en logt de rotatiefout als waarschuwing. Test: map schrijfbaar maar niet
  leesbaar. Rood: `StaticTests.cc:9431: !(!saved.empty() && std::filesystem::exists(saved))`. Groen erna, ook het
  einde (98) komt in het bestand.
- **M1 (Meseta-storting op de GameCube onzichtbaar).** Niet te meten (geen labrun). Twijfel is strenger: na een
  harde stop en na een Exit geeft `$restore` geen Meseta meer terug, en de login-controle telt geen Meseta mee
  (`inventory_backup_restore_meseta`). Een import houdt zijn Meseta. De bewezen eigen Meseta blijft op schijf en in de
  bron staan, zodat dit met één regel terug kan als een meting op de Wii laat zien dat de storting zichtbaar is.
  Twee bestaande verwachtingen gingen van 100 en 200 naar 0. Rood: `StaticTests.cc:9449: 0u !=
  inventory_backup_restore_meseta(from)` en `:9387: 0u != d.plan.meseta`.
- **M2 (6x5D zonder herkomst).** De 6x5D-handler roept `inventory_backup_note_dropped_partial` aan: eerst reconcile
  (K5), dan krijgt het floor item het bewezen eigen deel (Meseta: tot het bewezen deel; een stapel: tot de bewezen
  eenheden van de bronstapel). Test: 300 Meseta en één Monomate via 6x5D en 6x29, zelf opgeraapt: van hem; door B
  opgeraapt: niet van B. Rood: `StaticTests.cc:9505: 500u != from_a.meseta` (de stapelregel staat erna in dezelfde
  case en is in de mutatie niet apart rood gezien).
- **M3 (tekst).** "Niets kwijt" wordt "Niets terug te leggen uit ..."; na een harde stop en een Exit komt er een regel
  bij: "Wat je van een ander kreeg, kocht of uit de bank haalde, en Meseta komen niet terug." Rood: `:9530:
  !(t.own_only && ... "van een ander" ...)`.
- **M4 (LOST heet harde serverstop en wordt gepind).** `inventory_loss_verdict` geeft NONE voor een opzij gezette
  back-up, dus geen tekst en geen Loss-merk. Rood: `:9546: !(inventory_loss_verdict(b, false, 2, 500, false) ==
  InventoryLossVerdict::NONE)`.
- **M5 (fsync per afgifte).** Niet gebouwd: het punt vraagt eerst een meting op de pc (p50/p99 van de bestaande
  regel `interim state N written (... us)`), en die valt buiten deze opdracht (geen labrun, pc niet aanraken). Geen
  statische test: een tijdsmeting op de laptop zegt niets over de schijf van de pc.
- **M6 (alias in client-dropmodus).** 6x5F roept nu `inventory_backup_note_leader_drop(l, floor, id, client_mode,
  ene_st, obj_st)` aan, dat een samengestelde vijand als zijn alias target toetst en telt. Rood: `:9571:
  !(world(0xCC100000))`.

## Wat niet gebouwd is

- M5: zie boven, eerst meten.
- De kan-punten K1-K10, behalve K5 (de reconcile vóór de 6x5D-markering lag op het pad van M2).
- Twee commits in plaats van één per punt: B2 en M1-M6 delen hunks in dezelfde bestanden (InventoryBackup.cc,
  de testblok in StaticTests.cc); splitsen per hunk was de beurten niet waard.

## Beslissingen (buiten de schrijfset van decisions.md; hier vastgelegd)

- M1 strenger dan B-AF-53 ("Meseta het minimum"): geen Meseta na harde stop of Exit. Dekking: de opdracht
  ("twijfel = strenger herstel, nooit dubbel") en de critic (M1, alternatief als de storting onzichtbaar is).
  Gevolg voor de speler: na een harde stop of een verbroken verbinding is de Meseta die hij bij zich droeg weg.

## Gezien, niet gebouwd

- gebruiksvriendelijkheid · de login-tekst noemt verloren Meseta niet meer nu die niet terugkomt · `src/InventoryBackup.cc` (`inventory_backup_on_login`) · klein: een regel in `inventory_loss_text`
- bewijs · of de GameCube bij een Meseta-storting iets stuurt (6x29 met FFFFFFFF) · Wii · één meting; bij ja kan M1 terug
