# veilig-1 tests2 (newserv) — tests van intern op de veiligheidsbundel

Tak spike/veilig-tests2, start 771d844 (spike/veilig), samengevoegd met m4rone spike/intern-tests e1633fc
(61e16b4, e07c6b1, e1633fc). Samenvoeg-commit 54df26b. Servercode niet veranderd: alleen src/StaticTests.cc.

## Conflictblokken (src/StaticTests.cc)
1. Hulpfuncties: beide gehouden; `awaiting_only` (8-B2: marker blijft open tot de kaart geschreven is) staat naast
   `restored_at_login` en `next_login_is_real` van intern; geen tegenspraak, want restored_at_login vraagt de marker niet.
2. Verbinding valt weg midden in de run: alle controles van veilig gehouden (awaiting_only, snapshot, game join, de
   tweede login met de run-kopie op de kaart, de vierde login die de marker sluit) plus `restored_at_login(p2)` en als
   laatste login `next_login_is_real` op de kaart die de login-97 schreef (de volgorde van veilig blijft zo intact).
   Eén commentaarregel aangepast: de 97 wordt nu echt gespeeld, dus "de kaartschrijf van die 97 landde niet".
3. Spelafsluiting, kaart hield de run-kopie: awaiting_only en snapshot van veilig, daarna restored_at_login en
   next_login_is_real van intern.
4. Spelafsluiting, client weg vóór het herstel: awaiting_only van veilig, daarna restored_at_login en next_login_is_real.

Geen controle geschrapt of verzwakt: 3227 expect/must-regels tegen 3191 op 771d844 en 2980 op e1633fc; 16 e2e-zaken
(13 op 771d844 + de B2-zaak + de twee zaken met de duw die niet aankomt).

## Vlaggen
- SPIRE_E2E_KNOWN_GAP_B1 = true (stond al zo op 771d844)
- SPIRE_E2E_KNOWN_GAP_B2 = true (nieuw; 8-B2 dicht het gat op deze tak)

## Bouw en statische tests
- `build/newserv help | head -1`: `newserv-54df26b built 2026-11-11 21:08:10.000000 UTC`
- `build/newserv run-static-tests`: exit 0, slotregel "-- All static tests passed"
- 16 regels "GUARD E2E (...)", 0 keer "FAILED". Draaien aantoonbaar (naam in de log):
  - B1: "the base game, the player takes an item from the bank there: ... (known gap B1)"
  - B2: "the login restore, then the connection drops before the 97: ... (known gap B2)"
  - de drie login-herstelzaken (61e16b4) en de login-97 (e07c6b1): "the connection drops mid-run", "game exit, but the
    card kept the run copy", "game exit, the client is gone before the restore"
  - de duw die niet aankomt (e1633fc): "the run ends, the 'O' push never reaches the save buffer ..." en
    "the login restore's push never reaches the save buffer ..."
- Geen falende test; geen vlag tijdelijk uit gezet.

## Dekking
Deze opdracht dekt: dat de tests van intern (B1, B2, de 97 bij de login, de duw die niet aankomt) op de servercode van
de veiligheidsbundel draaien en groen zijn, zonder verzwakte controle. Niet gedekt: wat een echte Wii opslaat (alleen
de Wii), en servercode na 771d844.
