# Kwaliteit-1, bundel 1: start/hervatten (newserv)

Tak `spike/kwaliteit-1`, basis 756ca11. Datum 2026-11-02.

Dekking: dit dekt of hervatten wapen, pantser (en waar mogelijk EXP) server-side terugzet, getoetst met newserv-tests op
de laptop. Dit dekt niet of de GC-client op de Wii het uitrusten en de EXP echt overneemt (dat beslist alleen de Wii), en
andere hervat-problemen buiten deze drie regels.

## Regel 216 + 209: items komen onuitgerust terug — GEBOUWD (wapen en mag), pantser blijft met de hand

Commit f18b2c1 `fix: a resume equips the saved weapon and mag again`.

Vooraf gemeten: het bewaarpunt legde niet vast wat uitgerust was. `spire_savestate_items` nam alleen `ItemData` (20 bytes);
de uitgerust-vlag zit in `PlayerInventoryItem.flags` (0x08) en ging verloren. De quest maakt elk item nieuw met
`item_create2` (quests/spire/make_spire.py:1733-1781), dus nieuwe item-ids en altijd onuitgerust.

Wat er nu gebeurt:
- `src/SpireFlagBridge.hh`/`.cc`: `SpireSavestate.equipped` (JSON-sleutel `"Equipped"`, posities in `Items`). Bij 'V'
  vult `spire_savestate_items(..., &equipped)` hem uit de run-kopie. Een bestand van vóór deze wijziging heeft hem niet:
  dan blijft alles zoals het was ("Equip your gear."). Ongeldige posities worden bij het lezen weggelaten.
- Na het laatste hervat-item ('J' item i = laatste) start newserv eenmalig per hervatting `spire_resume_equip`: het wacht
  tot de `item_create2`-items in zijn kopie staan (max 10 x 500 ms), zoekt per uitgerust wapen of mag het item met dezelfde
  12 data1-bytes dat nog niet uitgerust is, en stuurt per item `SpireEquipItem` via het bestaande
  `spire_equip_item_send` (zelfde log en zelfde vangnet na 1000 ms). Alleen als r143 = 1 voor de client
  (`spire_equip_item_available_for`).
- Vangnet-tekst bij een geweigerde 6x25: wapen "Equip your weapon: Item Pack > Equip." (nieuw punt
  `SPIRE_EQUIP_RESUME_WEAPON` in `src/SpireEquipItem.hh`), mag de bestaande "Equip your mag: Item Pack > Mag.". Staat een
  item na het wachten niet in de kopie: "Equip your gear: Item Pack > Equip.".
- Hervat-tekst (`spire_run_start_ticker`, `spire_resume_equip_hint`): met r143 = 1 en een bewaarpunt met `Equipped` noemt
  hij alleen wat newserv niet kan uitrusten ("... 31 min. Equip your armor." / "... Equip your armor and units."), of
  niets ("Run resumed at room 5: 2 lives, 31 min."). Met r143 = 0 of een oud bestand: ongewijzigd "Equip your gear.".
- Mag-regel "Your Spire Mag comes back: equip it." vervalt als newserv de mag zelf uitrust
  (`spire_resume_server_equips_mag`).

Waarom pantser niet: `SpireEquipItemGC.s` weigert alles behalve wapen (data1[0] 0) en mag (2) en antwoordt 3
(system/client-functions/SpireEquipItemGC.s, blok "Weapons and mags only"). Pantser erbij betekent die .s wijzigen: buiten
de writeSet (src/, tests/) en nieuw clientgedrag dat alleen de Wii kan bewijzen. Daarom vraagt de tekst nu gericht om
"armor"/"shield"/"units". Route voor later: in de .s ook data1[0] = 1 met data1[1] = 1 (frame) en 2 (barrier) toelaten;
units (data1[1] = 3) hebben vier slots en vragen eerst uitzoeken wat vtable+0x84 voor een unit teruggeeft.

Log op de pc na een hervatting (herkennen in het journal):
`Spire equip: resume of serial ... : N item(s) were equipped at the save point, M to equip with SpireEquipItem`,
dan per item `Spire equip: resume; sending SpireEquipItem for the weapon|mag XXXXXXXX`,
`Spire equip: resumed weapon XXXXXXXX; client answered 1` en `... equipped (or gone): no line`.

### Test (src/StaticTests.cc, blok "kwaliteit-1" na de start-ticker-tests)

Toetst: bewaarpunt legt de uitgeruste Saber en het frame vast (posities 0 en 3); JSON heen en terug; oud bestand zonder
`Equipped`; de vier tekstvormen; doelkeuze na de item-lus (nieuwe ids, juiste Saber-id, frame niet); al uitgerust = niets;
items nog niet binnen = niets; vangnet-tekst wapen; mag-regel weg als de server de mag uitrust.

Rood, test tegen de broncode van 756ca11 (de drie src-bestanden teruggezet, StaticTests.cc nieuw), `make`, exit 2:
```
src/StaticTests.cc:5881:41: error: too many arguments to function 'std::vector<ItemData> spire_savestate_items(const PlayerInventory&, uint32_t, int32_t*, const SpireRunMagBytes&)'
src/StaticTests.cc:5889:10: error: 'struct SpireSavestate' has no member named 'equipped'
src/StaticTests.cc:5897:33: error: too many arguments to function 'std::string spire_run_start_ticker(bool, bool, const SpireSavestate*)'
```
Rood op gedrag (mutatie: `spire_resume_equip_hint` geeft altijd het oude " Equip your gear."), `./build/newserv run-static-tests`, exit 1:
```
E ... run-static-tests: failure at .../src/StaticTests.cc:5896: std::string("Run resumed at room 5: 2 lives, 31 min. Equip your armor.") != spire_run_start_ticker(true, false, &rs, true)
```
Groen, f18b2c1 (bron hersteld met `git show HEAD:...`, `unlink src/Revision.cc`, `make -C build -j16 newserv` exit 0),
`./build/newserv run-static-tests`, exit 0:
```
I ... - -- All static tests passed
```

## Regel 265: EXP binnen het level — NIET GEBOUWD

Hoe het level terugkomt: `spire_start_level_quest_bin` (SpireFlagBridge.cc, B-AF-3) patcht in de quest-.bin het
regelblok `arg_pushl level; ba_set_char_level` (F871). De client zet zelf het level bij ba_start. Dat regelblok
(`BattleRules`, src/PlayerSubordinates.hh:1161) heeft geen EXP-veld; de client geeft Sega's EXP van dat level. EXP kan
dus niet via dezelfde route mee.

Bestaand pakket: geen voor GC. 6xBF "Give EXP" is alleen BB (src/CommandFormats.hh:5827-5842); op GC is 6xBF de
lobbymuziek van Ep3 (:5820). EXP is op GC client-gezag.

Routes die er wel zijn, beide nieuw clientgedrag dat alleen de Wii kan bewijzen:
1. EXP bij 'V' bewaren en na het laden de volledige karakter-push van de guard (30, SetExtendedPlayerInfo met disp incl.
   EXP, verifiëren met een in-game 61) midden in een run doen. Die push draait nu alleen bij de terugkeer naar de basis;
   tijdens een load hing een B2 de lobby (v33), en een in-game 61 per bewaarpunt bevriest het mag-register tot de
   volgende quest (observatie 265 zelf).
2. Een nieuwe clientfunctie die de EXP van de eigen TObjPlayer zet: nieuwe adressen voor 3OP0, statisch te bewijzen en
   daarna alleen op de Wii.
Volgens de opdracht dus niet gebouwd.

## Commits

- f18b2c1 fix: a resume equips the saved weapon and mag again
- (dit rapport en het voortgangsbestand: docs-commit erna)

## Wat alleen de Wii kan bewijzen

- Of de GC-client na een hervatting `SpireEquipItem` uitvoert en wapen en mag echt uitgerust in beeld staan (journal:
  "client answered 1" en "equipped (or gone)").
- Of twee `SpireEquipItem`-aanroepen kort na elkaar, vlak na de `item_create2`-lus, goed gaan.
- De EXP (regel 265): niet gebouwd, dus niets te bewijzen.

## Beslissingen (voor decisions.md, door de orchestrator over te nemen)

### B-E01-K1-1 — Uitgerust-vastlegging als optionele sleutel in het bewaarpunt
- **Datum:** 2026-11-02
- **Route:** zelf
- **Type:** technisch
- **Klasse:** 2
- **Dekking:** dispatch kwaliteit-1 bundel 1 ("zo niet, leg dat vast op de plek waar het bewaarpunt geschreven wordt")
- **Beslissing:** `"Equipped"` als lijst posities in `Items`; ontbreekt hij, dan oud gedrag.
- **Waarom:** oude bewaarpunten blijven hervatbaar zonder migratie.

### B-E01-K1-2 — Uitrusten na het laatste hervat-item, items gematcht op data1
- **Datum:** 2026-11-02
- **Route:** zelf
- **Type:** technisch
- **Klasse:** 2
- **Dekking:** dispatch ("met hetzelfde mechanisme"); SpireEquipItem.hh `spire_equip_find_kit_weapon` matcht ook op inhoud
- **Beslissing:** trigger op het 'J'-antwoord voor het laatste item, wachten tot de items in de kopie staan, match op 12 data1-bytes.
- **Waarom:** de quest maakt nieuwe ids; data1 is precies wat item_create2 krijgt.

### B-E01-K1-3 — Pantser niet door de server
- **Datum:** 2026-11-02
- **Route:** zelf
- **Type:** technisch
- **Klasse:** 2
- **Dekking:** dispatch writeSet (src/, tests/); SpireEquipItemGC.s weigert type 1
- **Beslissing:** pantser, schild en units blijven met de hand; de hervat-tekst noemt ze gericht.
- **Waarom:** de .s wijzigen is buiten de writeSet en nieuw clientgedrag.

## Observaties (voor observaties.md)

- gebruiksvriendelijkheid · pantser/schild komen na hervatten nog onuitgerust terug: SpireEquipItemGC.s laat alleen wapen
  en mag toe · newserv system/client-functions/SpireEquipItemGC.s ("Weapons and mags only") · type 1 met data1[1] 1/2
  toelaten, klein in code, Wii-meting nodig; units eerst slotgedrag uitzoeken
- werkwijze · de newserv-repo heeft geen Integratiebranch/Uitrol in CLAUDE.md of .claude/omgeving.json (hook-waarschuwing bij
  commit) · newserv-worktree root · regel toevoegen, klein
