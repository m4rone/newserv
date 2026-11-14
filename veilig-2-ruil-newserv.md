# Veilig-2 (newserv): ruil via het ruilvenster en verkoop, daarna valt een Wii weg zonder 98

Tak spike/veilig-ruil, start 37bd1f8. Eerste fix 5df8eb7 was te breed (zie Correctie); de correctie volgt op dfc7961.

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

## Correctie (orchestrator, na 5df8eb7)
5df8eb7 liet elke 6x29 de momentopname verkleinen. Op V3 stuurt de client 6x29 ook bij een verkoop, een bankstorting,
het voeren van de mag en mogelijk de tekker (CommandFormats.hh:4052): daar is geen tweede speler en dus geen dubbelrisico,
maar het item ging na wegvallen zonder 98 verloren, en een geannuleerde ruil kostte het item ook. Dat is nieuw verlies
op een echt karakter. Nieuwe regel: de momentopname volgt alleen wat tussen spelers verhuist. K7 (val en opraap) is niet
aangeraakt; de 6x5D-markering uit 5df8eb7 is weer weg, zodat 6x2A/6x5D/6x59/6x5A gelijk zijn aan 37bd1f8.

## Fix (na de correctie)
- Een 6x29 verandert de momentopname nooit (`spire_guard_delete_apply` geeft 0; aanroep ReceiveSubcommands.cc:4633
  via `spire_guard_note_delete`): verkoop, bank, mag, tekker en de eigen 6x29 van een ruil vóór de uitvoering.
- Alleen bij de uitvoering (beide D2's, D3 verstuurd; ReceiveCommands.cc:5371, `spire_guard_note_trade_executed`):
  elke kant verliest precies de items en Meseta van zijn D0-lijst (voor zover zijn momentopname ze heeft: het tegoed),
  en de andere kant krijgt elke regel met hoogstens dat tegoed. Eerst alles afnemen, dan geven.
- Annuleren (D4): verandert niets. De D4-aanroepen uit 5df8eb7 zijn weg.

## Keuzes (met reden)
1. 6x29 alleen telt niet: geen tweede speler, dus geen dubbel; het item komt terug zoals vóór de fix, net als de kaart
   die ook niet geschreven werd. Meseta uit een verkoop telt niet mee.
2. Koppeling: de D0-lijst die de server bij de uitvoering zelf in de D3 naar de ander stuurt. Daar is de koppeling altijd
   te zien; de 6x29 en de 6x5E/6x2B van de clients worden niet gebruikt.
3. Volgt B's momentopname niet (andere game-modus, geen bestand, ander karakter): B krijgt niets en A verliest het wel.
   Dit is de enige plek waar het strenger mag.
4. Item dat niet in A's momentopname zit (in de basis gekocht): tegoed 0, B krijgt niets; A's momentopname verandert niet.
5. Meseta: het minimum (A verliest hoogstens wat zijn momentopname had, B krijgt hoogstens dat, nooit boven 999999).
6. Een niet-stapelbaar item: eerst exact (zoals K7), anders het eerste van dezelfde soort.
7. Waar het geldt: dezelfde voorwaarde als K7 (Normal of Solo, alleen AwaitingSave).
8. `spire_guard_delete_apply` en de 6x29-aanroep blijven bestaan, ook al doen ze nu niets. Reden: de regel voor de
   6x29 staat op één plek die de server ook echt doorloopt, en de tests roepen dezelfde functie aan als 5df8eb7 (dus
   echt rood daar). Kosten: één JSON-lees per 6x29, alleen in een gevolgde game.

## Rood op 5df8eb7, groen erna
Rood: de bronnen van 5df8eb7 (SpireFlagBridge.cc/.hh, ReceiveSubcommands.cc, ReceiveCommands.cc) met het nieuwe
testblok, waarvan de expects tijdelijk niet afbreken (op de plek van drie commentaarregels, dus dezelfde regelnummers).
Letterlijk uit de rode run (`RED src/StaticTests.cc:<regel> <expect>`):
- (c) verkoop: `2481 0u != spire_guard_delete_apply(&snap, nullptr, ...)` en `2486 1u != count(a_after, sword)` (3x,
  alle drie kaarten): op 5df8eb7 was het verkochte zwaard weg. De Meseta-regel 2487 (1000, verkoopprijs telt niet) was
  ook op 5df8eb7 groen.
- (e) bankstorting: `2496`, `2497` (item en 300 Meseta afgenomen), `2501 1u != count(a_after, sword)` (3x),
  `2502 1000u != a_after.second.stats.meseta.load()` (3x).
- (f) geannuleerde ruil: `2510 0u != spire_guard_delete_apply(&snap, &a_side, ...)` en
  `2512 1u != count(after_login(snap, card), sword)` (3x).
- (g) mag voeren: `2526`, `2527 !(spire_guard_same_state(...))`, `2528 4u != ...data1[5]`.
- (a) en (b) zijn op 5df8eb7 en erna groen (2463-2472: het zwaard en het schild bestaan elk één keer).
De rode run van de eerste fix (tegen 37bd1f8, met stubs) staat in de geschiedenis van dit bestand (dfc7961);
(a) en (b) waren toen rood op 2463-2465 en 2470.
Groen (6ac4c46): `build/newserv run-static-tests` exit 0, `-- All static tests passed`, 0 keer `RED`.
(d) 8-B2, 8-B1, K7 en Veilig-2 lopen mee; 16 keer `GUARD E2E`, 0 keer `FAILED`.

## Wat de speler merkt
Ruilt hij in de basis items of Meseta met zijn partner via het ruilvenster en valt een van beiden weg zonder het spel te
verlaten, dan staat het na de volgende login bij precies één van hen. Valt hij weg na een verkoop, een bankstorting of
het voeren van de mag, dan heeft hij na de login het item terug zoals vóór deze levering (de verkoopprijs niet). Wordt
een ruil afgebroken, dan houdt hij zijn items. Netjes verlaten (98) houdt alles zoals het spel het opsloeg.

## Build en checks
- `build/newserv help | head -1`: `newserv-aca05da built 2026-11-11 22:07:51.000000 UTC` (src gelijk aan 6ac4c46; de statische tests liepen op die bronnen)
- `build/newserv run-static-tests`: exit 0; GUARD E2E 16, FAILED 0
- `git diff --quiet ee0fa66 HEAD -- src/SpireDrops.cc src/SpireDrops.hh`: exit 0 (gelijk)
- K7-valafhandeling: `git diff 37bd1f8 HEAD -- src/SpireFlagBridge.cc` heeft 0 verwijderde regels; ReceiveSubcommands.cc verschilt van 37bd1f8 alleen in de 6x29-aanroep

## Dekking
Deze opdracht dekt: items en Meseta die via het ruilvenster tussen partners wisselen of verkocht worden, gevolgd door
wegvallen zonder 98. Niet gedekt: de bank (B-REGIE-20-13) en wat de Wii werkelijk opslaat (alleen de Wii).

## Onzekerheid en gezien, niet gebouwd
- De volgorde (6x29 vóór D2) komt uit newserv's eigen beschrijving, niet uit een Wii-meting. De fix gebruikt de 6x29
  niet meer, dus de volgorde doet er niet toe.
- Een gebruikt verbruiksitem (6x27) volgt de momentopname nog niet: na wegvallen zonder 98 komt een Monomate terug.
  Nooit dubbel met een ander, dus niet gebouwd.
- K7 laat de momentopname nog bij de val verliezen (6x2A/6x5D); die correctie bouwt een andere bouwer in veilig-1.
