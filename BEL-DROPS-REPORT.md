# BEL-DROPS rapport (spike/bel-drops, basis 695844f)

Pakket S-drops van spoor beloningen (B-REGIE-17-7, B-BEL-4): welke drops een speler in een Spire-run krijgt.
Alles server-side in `src/SpireDrops.cc/.hh`, tests in `src/StaticTests.cc`. ItemCreator.cc/.hh niet aangeraakt.

## Verificatie (eindstand, HEAD na fix E)

- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8 --target newserv` -> exit 0.
  Afwijking: het voorgeschreven `cmake --build build -j8 newserv` faalt op cmake 4.4.4 met
  `Unknown argument newserv` (build.log.exit 1); `--target newserv` is hetzelfde doel.
- `./build/newserv --config=tests/config.json run-static-tests > st.log 2>&1; echo $?` -> `exit 0`, en
  `I ... - -- All static tests passed` (st.log:14136).
- Meetregels uit dezelfde run:
  - `Spire meseta of 1000 drops: boxes 0 (quest rules) -> 513 (Spire), Boomas 106 -> 106`
  - `Spire armour: 329 of the 12 classes' frame and barrier drops replaced`
  - `Spire RAmar Lv1 fallback weapon: 0600`

Rood zonder fix: per fix een tijdelijke stub in de fix (niet gecommit), build, zelfde testcommando:

| Fix | Stub | Uitvoer (letterlijk) |
|---|---|---|
| B | `spire_drops_item_rules` geeft quest_rules terug | exit 134, `failure at .../src/StaticTests.cc:3566: !(spire_rules && (spire_rules != quest_rules))` |
| C | `spire_drops_fit_armor` geeft meteen false | exit 134, `failure at .../src/StaticTests.cc:3645: !(wears(it, cls))` |
| D | terugval zoekt meteen in groep 01-0C (oude regel) | exit 134, `failure at .../src/StaticTests.cc:3701: !((w.data1[1] >= 0x06) && (w.data1[1] <= 0x09))` |
| E | `item.data1[4] &= 0x7F` uitgecommentarieerd | exit 134, `failure at .../src/StaticTests.cc:3733: 0x05 != it.data1[4]` |

Groen met fix: de eindrun hierboven (alle vier secties lopen, `All static tests passed`).

## A. Drops en diepte: geen newserv-code, de quest-regel gaat naar 0

Het area-argument per drop is `map->floor_to_area.at(floor)` (src/ReceiveSubcommands.cc:2908 kisten, :2960
vijanden). De floor_to_area van een quest komt uit `meta.floor_assignments`, en die wijken alleen af van
`default_floor_to_area` (src/Map.cc:68-92) door `map_designate`/`map_designate_ex` (src/QuestScript.cc:5466-5490).
Gemeten: de gedisassembleerde gedeployde q921 (Run I), q922 (Run II), q981 en q984 (route-b-8 build-pc/battle)
bevatten 0 keer `map_designate`. Dus area = standaardgebied van de vloer, en zonder de override
(`box_drop_area != 0`, src/ItemCreator.cc:123-124) geeft `table_index_for_area` (src/ItemCreator.cc:127-176) per
vloer de Sega-kolom hieronder. Nu (box_drop_area 3) is het voor elke drop index 2: Cave 1 in Run I,
VR Spaceship Alpha in Run II. De tabel is mechanisch uit de bron afgeleid (spire.json/spire-ep2.json rooms,
Map.cc areas_ep1/areas_ep2, ItemCreator.cc-tabel):

| Run | k | Vloer | Kamer | Area | Sega-kolom (index) |
|---|---|---|---|---|---|
| I | 1, 2 | 1 | Forest 1 A/B | 01 | Forest 1 (0) |
| I | 3 | 2 | Forest 2 | 02 | Forest 2 (1) |
| I | 4 | 11 | Dragon | 0B | Cave 1 (2) |
| I | 5 | 3 | Caves 1 | 03 | Cave 1 (2) |
| I | 6 | 4 | Caves 2 | 04 | Cave 2 (3) |
| I | 7 | 5 | Caves 3 | 05 | Cave 3 (4) |
| I | 8 | 12 | De Rol Le | 0C | Mine 1 (5) |
| I | 9 | 6 | Mines 1 | 06 | Mine 1 (5) |
| I | 10 | 7 | Mines 2 | 07 | Mine 2 (6) |
| I | 11 | 13 | Vol Opt | 0D | Ruins 1 (7) |
| I | 12 | 8 | Ruins 1 | 08 | Ruins 1 (7) |
| I | 13 | 9 | Ruins 2 | 09 | Ruins 2 (8) |
| I | 14 | 10 | Ruins 3 | 0A | Ruins 3 (9) |
| I | 15 | 14 | Dark Falz | 0E | Ruins 3 (9) |
| II | 16 | 1 | VR Temple Alpha | 13 | VR Temple Alpha (0) |
| II | 17 | 2 | VR Temple Beta | 14 | VR Temple Beta (1) |
| II | 18 | 14 | Barba Ray | 20 | VR Spaceship Alpha (2) |
| II | 19 | 3 | VR Spaceship Alpha | 15 | VR Spaceship Alpha (2) |
| II | 20 | 4 | VR Spaceship Beta | 16 | VR Spaceship Beta (3) |
| II | 21 | 15 | Gol Dragon | 21 | Jungle North (4) |
| II | 22 | 5 | Central Control Area | 17 | Seaside (7) |
| II | 23 | 9 | Seaside Area | 1B | Seaside (7) |
| II | 24 | 12 | Gal Gryphon | 1E | Seabed Upper (8) |
| II | 25 | 10 | Seabed Upper | 1C | Seabed Upper (8) |
| II | 26 | 11 | Seabed Lower | 1D | Seabed Lower (9) |
| II | 27 | 13 | Olga Flow | 1F | Seabed Lower (9) |

De bossen volgen Sega's eigen toewijzing (Dragon -> Cave 1, CCA -> Seaside, enz.). Voor de orchestrator:
`box_drop_area 0` in spire.json:20; q922 erft het via de merge (spire-ep2.json zet het niet). Ook de
kamer-quests dragen nu 3: gemeten `ba_set_box_drop_area` met r0 = 3 in q981 (Ep1) en q984 (Ep2).

## B. Meseta: `spire_drops_item_rules` (commit 1)

- src/SpireDrops.hh:147-152, src/SpireDrops.cc:391-404: in een Spire-spel met SpireDrops krijgt de item creator
  van de lobby de regels van de quest met `meseta_mode = ALLOW` (alleen als de quest FORBID_ALL zet); alle andere
  regels blijven die van de quest, de quest-regel zelf (naar de client) verandert niet.
- Toegepast in `spire_drops_quest_loaded` (src/SpireDrops.cc:432) en opnieuw bij elke gefilterde drop
  (src/SpireDrops.cc:451), want de lobby maakt een nieuwe item creator met de quest-regels bij een leiderswissel
  (src/Lobby.cc:342) en via `$sid` (src/ChatCommands.cc:2594).
- Hermeting op 695844f: de quest-regel wiste alleen de meseta uit **kisten** (box class 5 ->
  generate_common_item_variances -> clear_item_if_restricted, src/ItemCreator.cc:684 en :634-638). Een gewone
  meseta-drop van een **vijand** gaat daar niet langs (src/ItemCreator.cc:321-323) en bleef al bestaan: gemeten
  Boomas 106 van 1000 met de quest-regels en 106 met de Spire-regels; kisten 0 -> 513.
- Rapen, server-kant: `on_pick_up_item_generic` (src/ReceiveSubcommands.cc:2315-2357) controleert bestaan,
  zichtbaarheid, afstand en tas, en roept `PSOBBCharacterFile::add_item`, die meseta optelt bij
  `disp.stats.meseta` (src/SaveFileFormats.cc:1172-1175, plafond 999999 op :1269). Er is geen
  battle-regelcheck en geen Spire-hook op 6x59/6x5A. De test laat zien: 300 + een drop van 25 = 325, tas leeg.
- Test: "Spire drops B-BEL-4: Sega's meseta drops of enemies and boxes in a Spire game" (een echte ItemCreator
  met Sega's common-table-v3-v4, BATTLE, Normal, Viridia, Forest 1).

## C. Armour voor een force: `spire_drops_fit_armor` (commit 2)

- src/SpireDrops.hh:87-92, src/SpireDrops.cc:189-230, aangeroepen in `spire_drops_filter_item` na de
  run-item-check (src/SpireDrops.cc:340-342); de reden komt terug op src/SpireDrops.cc:362.
- Een frame (01 01) of barrier (01 02) waarvan Sega's usability-bits de klasse niet dekken wordt de sterkste
  gewone van dezelfde soort die de klasse wel draagt en niet sterker is (sterkte = DFP + EVP, gelijk -> de latere),
  anders index 0 (Frame / Barrier). Slots blijven; DFP/EVP-bonus wordt op het bereik van het nieuwe item gezet.
- Pity: ongewijzigd, het item blijft een niet-wapen run-item (pity +1; test eist 2 -> 3 voor elke vervanging).
- Test: alle 12 klassen x alle frames en barriers uit de v3-tabel (329 vervangingen), plus FOnewearl:
  01 01 01 -> 00, 01 01 02 -> 00, 01 01 05 -> 04; een HUmar houdt ze.

## D. Terugvalwapen: `spire_drops_role_groups` (commit 3)

- src/SpireDrops.hh:79-85, src/SpireDrops.cc:115-124 (rolgroepen) en :147-187 (terugval).
- De terugval (niets past nu) zoekt eerst alleen in de groepen van de rol (hunter 01-05, ranger 06-09,
  force 0A-0C), daarbinnen het minste tekort; pas als daar niets is, de oude regel over 01-0C. De code kende geen
  groepen per klasse; ik heb ze gedefinieerd uit Sega's volgorde van wapenklassen (src/ItemNameIndex.cc:1471).
- Gemeten in de test: RAmar Lv1 ATP 18, ATA 40; Saber vraagt ATP 30, Handgun ATA 68. Uitkomst 0600 (Handgun)
  in plaats van een Saber. Alle level-1 rangers, 12 seeds: groep 06-09.

## E. Specials: `spire_drops_identify_weapon` (commit 4)

- src/SpireDrops.hh:131-134, src/SpireDrops.cc:365-371, aangeroepen in `spire_drops_filter` direct na
  `spire_drops_filter_item` (src/SpireDrops.cc:469-471; logt "weapon identified" als er verder niets veranderde).
- Elk gehouden wapen met bit 0x80 in data1[4] (ItemCreator.cc:537-538: special of rare) krijgt `&= 0x7F`; de
  special blijft. Geen ander item wordt aangeraakt.

## Beslissingen (zelf genomen, technisch, omkeerbaar; niet in decisions.md: buiten mijn writeSet)

- B-BELDROPS-1, B: de regels-kopie gaat op de item creator vanuit SpireDrops (quest-load + elke drop) in plaats
  van een wijziging in ItemCreator/Lobby, want die bestanden liggen buiten het Spire-pad. Dekking: B-BEL-4
  ("meseta-drops gaan weer aan op de server"), brief writeSet.
- B-BELDROPS-2, C: sterkte = DFP + EVP uit Sega's tabel; het RequiredLevel van armour telt niet mee (de trigger
  is de klasse, zoals de brief vraagt). Dekking: B-BEL-4, brief punt C.
- B-BELDROPS-3, D: de rolgroepen 01-05 / 06-09 / 0A-0C gedefinieerd, omdat de code ze nog niet per klasse kende.
  Dekking: brief punt D (de groepen staan er letterlijk).
- B-BELDROPS-4, E: identificatie als eigen functie in de glue (na filter_item), zoals de brief de plek noemt.
  Dekking: brief punt E.

## Gezien maar niet gebouwd (voor observaties.md)

- Na een nieuwe item creator (leiderswissel of `$sid`) wordt alleen de eerste drop nog met de quest-regels gemaakt
  (een kist-meseta kan dan wegvallen); vanaf de volgende drop gelden de Spire-regels weer. Dicht met één regel vóór
  de generatie in src/ReceiveSubcommands.cc:3053 (generate_item_unfiltered) of in Lobby::create_item_creator (src/Lobby.cc:231), allebei
  buiten de writeSet.
- A kan ook server-side: `spire_drops_item_rules` kan `box_drop_area = 0` zetten (één regel); niet gebouwd, de
  brief kiest de quest-regel.
- Vervangen armour houdt geen rekening met RequiredLevel: een force kan een vervangende frame krijgen die hij pas
  later mag dragen (zelfde tolerantie als Sega's eigen drops en de AHEAD-wapens).
- De voorgeschreven build-regel `cmake --build build -j8 newserv` werkt niet op cmake 4.4.4; `--target newserv`.
- De hook `werkwijze` meldt dat deze newserv-worktree geen Integratiebranch/Uitrol-regels heeft (waarschuwing
  bij elke commit, niets geweigerd).

## Meetpunten voor de Wii

1. Meseta: in een battle-quest met `meseta_drop_mode 1` laat de client de meseta van een kist en een vijand
   zien en laat hij hem oprapen; telt de meseta van het karakter op (server rekent hem bij, zie B).
2. Special: een wapen met special uit een Spire-drop verschijnt met zijn special-naam (geen ????), is uit te
   rusten en de special werkt.
3. Armour: een force krijgt alleen frames/barriers die hij kan uitrusten (geen rode markering in het menu).
4. Ranger Lv1: het terugvalwapen is een Handgun (nog niet uit te rusten op ATA 40, zoals voorheen bij de Saber).
