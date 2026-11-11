# Veilig-2 (newserv): ruil via het ruilvenster en verkoop, daarna valt een Wii weg zonder 98

Tak spike/veilig-ruil, start 37bd1f8. Fix-commit 5df8eb7 (gelezen met git rev-parse).

## Nameting op 37bd1f8
- Ruil op V3 (CommandFormats.hh:2479-2490, de volgorde zoals newserv hem beschrijft; niet op de Wii gemeten):
  - D0 van elke kant (`on_D0_V3_BB`, ReceiveCommands.cc:5268): de lijst die die speler geeft, met de item-ID's van
    zijn eigen client; de server bewaart hem in `c->pending_item_trade->items` (5287-5292) en stuurt D1.
  - Elke kant wist daarna zelf wat hij geeft met een 6x29 (`on_destroy_inventory_item`, ReceiveSubcommands.cc:4616;
    `remove_item` op 4630 uit de server-inventaris), en schrijft zijn kaart, vóór zijn D2.
  - D2 van beide (`on_D2_V3_BB`, ReceiveCommands.cc:5306): pas als beide binnen zijn voert de server de ruil uit
    (5369-5376) en stuurt op V3 elke kant een D3 met de D0-lijst van de ander (5363, `send_execute_item_trade`).
  - Wat de ontvanger daarna doet (zijn eigen 6x5E/6x2B) komt van zijn client en ziet eruit als een aankoop; de server
    kan daaraan niet zien dat het van de ruil komt. D4 (5379) annuleert.
- Betrouwbaar koppelbaar: A's D0-lijst, A's 6x29 met hetzelfde item-ID (Meseta: ID 0xFFFFFFFF, op soort) en de
  uitvoering bij beide D2's. Niet te zien: of B het werkelijk kreeg.
- Verkoop op V3: alleen een 6x29; de Meseta die de winkel betaalt ziet de server niet.
- Vóór de fix raakte geen van die berichten de momentopname (K7 volgde alleen 6x2A/6x5D/6x59/6x5A).

## Fix
- Elke 6x29 die de server afhandelt bij een speler wiens momentopname volgt (zelfde voorwaarde als K7) haalt het
  gewiste item of de Meseta uit die momentopname (`spire_guard_note_delete`, SpireFlagBridge.cc:5637; aanroep
  ReceiveSubcommands.cc:4633), behalve de 6x29 direct na zijn eigen 6x5D (die nam K7 al; aanroep 2134).
- Hoort de 6x29 bij een item uit zijn openstaande ruillijst, dan is wat eruit ging het tegoed van die regel
  (`spire_guard_delete_apply`, 5537).
- Bij de uitvoering (ReceiveCommands.cc:5371, `spire_guard_note_trade_executed`, SpireFlagBridge.cc:5675): eerst
  alles afnemen, dan geven. Een regel zonder 6x29 vooraf wordt dan afgenomen; B's momentopname krijgt elke regel van
  A met hoogstens zijn tegoed (`spire_guard_trade_give`, 5582). Annuleren (D4, 5391/5404): niets terug.

## Keuzes (met reden)
1. Elke gevolgde 6x29 is verlies, niet alleen een ruil: een verkoop is een 6x29 alleen, en een gevoerde mag kost het
   item echt. Strenger: afnemen kan nooit iets dubbel maken.
2. De 6x29 na een 6x5D wordt overgeslagen, maar alleen de eerstvolgende en alleen bij dezelfde soort en hoeveelheid.
   Reden: K7 nam hem al bij de 6x5D; een blijvende markering zou later een verkoop kunnen inslikken.
3. B krijgt pas iets bij de uitvoering (beide D2's), niet bij zijn eigen 6x5E/6x2B: dat is het enige moment waarop de
   server zag dat het van A's ruil kwam. Hoogstens wat A's momentopname verloor.
4. Een regel zonder 6x29 vóór de uitvoering (Meseta zonder eigen 6x29, of een latere 6x29) wordt bij de uitvoering
   afgenomen; een late 6x29 daarvan wordt één keer overgeslagen, zodat er geen tweede gelijk item uit gaat.
5. Geannuleerde ruil na A's 6x29: wat A's momentopname verloor blijft weg, B krijgt niets (strenger).
6. Item dat niet in A's momentopname zit (in de basis gekocht): geen tegoed, B krijgt niets.
7. Meseta: het minimum (A verliest hoogstens wat zijn momentopname had, B krijgt hoogstens dat). Meseta uit een
   verkoop telt nooit mee.
8. Valt B weg vóór de uitvoering, of volgt B's momentopname niet: B krijgt niets; A verloor het wel.
9. Waar het geldt: dezelfde voorwaarde als K7 (Normal of Solo, alleen AwaitingSave), dus ook buiten de basisgame.

## Rood zonder de fix, groen erna
Rood: tijdelijk `return 0;` in `spire_guard_delete_apply` en `spire_guard_trade_take_rest`, en
`side.items.clear(); return 0;` in `spire_guard_trade_give` (= 37bd1f8: niets gevolgd), plus een niet-afbrekende
expect in het nieuwe blok (op de plek van drie commentaarregels, dus dezelfde regelnummers). Letterlijk uit de rode run:
- (a) `RED src/StaticTests.cc:2463 0u != count(a_after, sword)` (3x, alle drie A-kaarten),
  `2464 1u != count(a_after, sword) + count(b_after_trade, sword)` (3x: het zwaard bestond twee keer),
  `2465 1u != count(a_after, shield) + ...` (3x, de andere richting).
- (b) `RED src/StaticTests.cc:2470 1u != count(b_after, sword)` (4x, alle B-kaarten). De regel "niet dubbel met A's
  herstel ook" (2472) was ook zonder fix 1 (B verloor het dan) en blijft 1.
- (c) `RED src/StaticTests.cc:2480 1u != spire_guard_delete_apply(&snap, nullptr, ...)` en
  `2484 0u != count(a_after, sword)` (3x): het verkochte zwaard kwam terug.
- Meseta en de rest: 2498, 2501-2503 (Meseta-minimum), 2517-2523 (afname bij uitvoering), 2545-2550 (stapel, tegoed),
  2579 (een andere 6x29 na een 6x5D wordt wel gevolgd).
Groen: `build/newserv run-static-tests` exit 0, laatste regel `-- All static tests passed`, 0 keer `RED`.
(d) De secties 8-B2, 8-B1, K7 en Veilig-2 lopen mee (log-regels 104-107); 16 keer `GUARD E2E`, 0 keer `FAILED`.

## Wat de speler merkt
Ruilt hij in de basis een item of Meseta met zijn partner via het ruilvenster en valt een van beiden weg zonder het
spel te verlaten, dan staat het na de volgende login bij precies één van hen. Verkoopt hij iets en valt hij weg, dan
krijgt hij het item niet terug (en de verkoopprijs ook niet). Wordt een ruil afgebroken nadat zijn Wii het item al wiste
en valt hij dan weg, dan is het weg. Netjes verlaten (98) houdt alles zoals het spel het opsloeg.

## Build en checks
- `build/newserv help | head -1`: `newserv-5df8eb7 built 2026-11-11 21:42:39.000000 UTC`
- `build/newserv run-static-tests`: exit 0; GUARD E2E 16, FAILED 0
- `git diff --quiet ee0fa66 HEAD -- src/SpireDrops.cc src/SpireDrops.hh`: exit 0 (gelijk)

## Dekking
Deze opdracht dekt: items en Meseta die via het ruilvenster tussen partners wisselen of verkocht worden, gevolgd door
wegvallen zonder 98. Niet gedekt: de bank (B-REGIE-20-13) en wat de Wii werkelijk opslaat (alleen de Wii).

## Onzekerheid en gezien, niet gebouwd
- De volgorde (6x29 vóór D2) komt uit newserv's eigen beschrijving, niet uit een Wii-meting. De fix is in beide
  volgordes nooit dubbel (keuze 4), alleen strenger.
- Gooit `remove_item` in 6x29 (server-inventaris loopt uit de pas met de client), dan komt de handler niet bij de
  bewaker en neemt hij niets af (bestaand gedrag, ReceiveSubcommands.cc:4630).
- Een gebruikt verbruiksitem (6x27) volgt de momentopname nog niet: na wegvallen zonder 98 komt een Monomate terug.
  Nooit dubbel met een ander, dus niet gebouwd. Hoe V3 de tekker afhandelt (6xB8 alleen BB hier) is niet nagemeten:
  wist de client daar met een 6x29, dan verliest de momentopname het wapen (strenger, nooit dubbel).
