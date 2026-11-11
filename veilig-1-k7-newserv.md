# K7 (newserv): een item via de vloer, daarna valt een Wii weg zonder 98

Tak spike/veilig-k7, start 1a00938. Fix-commit 0816ed4 (gelezen met git rev-parse).

## Nameting op 1a00938
- De momentopname volgt in de basisgame alleen de 98: `spire_guard_end_run` zet de client in
  `spire_guard_base_follow_pending` (SpireFlagBridge.cc:5156-5170), `spire_guard_after_98` past daarna
  `spire_guard_base_game_follow` toe (5222-5250, functie 4606-4621). Zonder 98 gebeurt er niets.
- Val en opraap raken de bewaker niet: 6x2A `on_player_drop_item` (ReceiveSubcommands.cc:2029), 6x5D
  `on_drop_partial_stack_t` (2112, ook Meseta; de 6x29 erna haalt het uit de server-inventaris) en
  6x59/6x5A `on_pick_up_item_generic` (2321) passen alleen de server-inventaris en de vloer aan.
- Het pad dat bij de login terugduwt: `spire_guard_restore_if_needed` (6401) onder alleen AwaitingSave
  (6437-6475) roept `spire_guard_awaiting_save_login_apply` (4547-4556) aan: is de 61 niet gelijk aan de
  momentopname, dan RestorePending en wordt de momentopname (van vóór de basis, mét het item) geduwd; is
  de kaart gelijk aan die oude stand, dan blijft hij staan, ook mét het item. Beide: A heeft het item en
  B ook. Andersom houdt B's momentopname het item niet: valt B weg en ging A netjes weg (98), dan is het weg.

## Fix
Waar de 98 gevolgd wordt (Normal of Solo, alleen AwaitingSave, momentopname met karakter, zelfde
karakter) volgt de momentopname nu ook elke val en opraap die de server zelf afhandelt:
- val (6x2A, 6x5D): de momentopname van de valler verliest het item (zover hij het heeft); dat aantal
  is een tegoed op dat vloeritem (`spire_guard_note_floor_drop`, `spire_guard_snapshot_take_drop`);
- opraap (6x59, 6x5A, alleen na een geslaagde opraap): alleen een vloeritem met tegoed gaat naar de
  momentopname van de raper, hoogstens het tegoed (`spire_guard_note_floor_pickup`,
  `spire_guard_snapshot_give_pickup`). Het tegoed wordt één keer gebruikt.
Bestanden: src/SpireFlagBridge.cc/.hh, src/ReceiveSubcommands.cc (drie aanroepen), src/StaticTests.cc.
Niet aangeraakt: de vangnet-functies en de twee startlevel-aanroepen.

## Keuzes (met reden)
1. Behoud, geen kopie: per val en opraap een verschil op de momentopname, niet de hele
   server-inventaris overnemen. Reden: die inventaris bevat ook wat de server niet betrouwbaar ziet; met
   behoud kan het totaal over alle momentopnames nooit groeien.
2. Tegoed per vloeritem in SpireFlagBridge (adres + weak_ptr), geen veld in Lobby::FloorItem. Reden:
   binnen de genoemde bestanden; overleeft een herverdeling van item-ID's; een hergebruikt adres telt
   als geen tegoed.
3. Opraap zonder tegoed (doos, vijand, een valler zonder gevolgde momentopname, of een item dat niet in
   zijn momentopname zat, bv. in de basis gekocht) komt nooit in een momentopname. Strenger: valt de raper
   dan weg zonder 98, dan krijgt hij het niet terug.
4. Meseta: de valler verliest hoogstens wat zijn momentopname heeft; de raper krijgt hoogstens dat
   tegoed (het minimum), en nooit boven 999999.
5. Niet-stapelbaar item: eerst exact (data, ID genegeerd), anders het eerste van dezelfde soort (een
   gevoerde mag, een afgedaan unit). Reden: de valler houdt nooit een kopie; het aantal klopt altijd, in
   het zeldzame geval met twee van dezelfde soort kan het verkeerde exemplaar uit de valler gaan.
   Materiaaltellers (extension_data2 per positie) blijven op hun plek, zoals PSOBBCharacterFile::remove_item.
6. Val zonder opraap en dan wegvallen: de valler krijgt het niet terug. Reden: het ligt nog op de vloer
   voor de partner; teruggeven maakt het dubbel zodra die het oppakt.
7. Volle momentopname bij de raper: niets erbij, tegoed weg (minder terug, nooit meer).
8. Ook in gewone Normal/Solo-games onder alleen AwaitingSave, niet alleen de basisgame: dezelfde regel als
   welke 98 gevolgd wordt, en hetzelfde faalscenario (wegvallen zonder 98 duwt de oude stand terug). Dit
   gaat iets verder dan de dekking ("in de basisgame"); beperken naar alleen de basis is één voorwaarde
   in `spire_guard_floor_snapshot` (zie Afwijking).

## Rood zonder de fix, groen erna
Rood: tijdelijk `return 0;` in take_drop en give_pickup (= 1a00938, momentopname volgt niet) en een
niet-afbrekende expect in het K7-blok (3 regels extra: stub-regel = definitieve regel + 3), hier
omgerekend naar het definitieve bestand src/StaticTests.cc:
- (a) 2330 `expect_eq(0u, count(a_after, sword))` en 2331 `expect_eq(1u, count(a_after, sword) + count(b_memory, sword))`
  rood voor alle drie kaarten (oude stand, run-kopie, stand na de val): A had het zwaard terug, totaal 2.
- (b) 2342 `expect_eq(1u, count(b_after, sword))` en 2343 `... + count(as_character(a_left), sword)` rood
  voor alle drie kaarten van B: B kwijt, en met A netjes weg (98) bestond het nul keer.
  (2345, B en A beide hersteld: was ook zonder fix 1, blijft 1.)
- (c) 2356 `expect_eq(0u, count(after_login(snap, card), sword))` rood voor alle drie kaarten.
- Meseta en stapels: 2373-2390 en 2402-2407 rood (geen tegoed, geen verschuiving).
Letterlijk uit de rode run: `RED .../src/StaticTests.cc:2333 0u != count(a_after, sword)`,
`RED .../src/StaticTests.cc:2345 1u != count(b_after, sword)`,
`RED .../src/StaticTests.cc:2359 0u != count(after_login(snap, card), sword)` (stub-regelnummers).
Groen: `build/newserv run-static-tests` exit 0, laatste regel `-- All static tests passed`; de secties
8-B2 en 8-B1 en K7 lopen erin mee (d).

## Wat de speler merkt
Geeft hij in de basis een item of Meseta aan zijn partner via de vloer en valt een van beiden weg, dan
staat het na de volgende login bij precies één van hen. Laat hij iets vallen dat niemand opraapt en valt
hij weg, dan is het weg (zoals in het spel zelf). Wie iets opraapt dat niet van een gevolgde partner kwam
(doos, vijand) en wegvalt zonder het spel te verlaten, krijgt dat niet terug; netjes verlaten (98) houdt alles.

## Build en checks
- `build/newserv help | head -1`: `newserv-0816ed4 built 2026-11-11 20:40:10.000000 UTC`
- `build/newserv run-static-tests`: exit 0
- `git diff --quiet ee0fa66 HEAD -- src/SpireDrops.cc src/SpireDrops.hh`: exit 0 (gelijk)

## Dekking
Deze opdracht dekt: items en Meseta die in de basisgame tussen partners wisselen via de vloer, gevolgd
door het wegvallen van een van beiden zonder 98. Niet gedekt: de bank (B-REGIE-20-13), ruil via het
ruilvenster als de server die anders ziet dan val en opraap, en wat de Wii werkelijk opslaat (alleen de Wii).
Tegengekomen: de server ziet het ruilvenster op V3 als 6x29 bij de gever en 6x2B bij de nemer
(ReceiveSubcommands.cc: de 6x29-opmerking "after a sale, a trade" en `on_create_inventory_item_t`),
niet als val en opraap; die ruil volgt de momentopname dus niet, en hetzelfde dubbel-risico bij wegvallen
zonder 98 staat daar nog open. Ook verkopen (6x29 alleen) volgt hij niet.

## Afwijking
Keuze 8 (ook gewone Normal/Solo-games). Alle andere punten volgens de opdracht.
