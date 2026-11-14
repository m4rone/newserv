# veilig-2b: veiligheidslevering 2 voor newserv (B-REGIE-21-24)

Tak `spike/veilig-2b`, gemaakt op `0fffb30` (de bankfix van veilig-bank). De stappen in volgorde, elk gepusht:

| stap | commit | wat |
|---|---|---|
| 1 | `51f49f4` | merge `m4rone/spike/veilig-2` (66a32cb): de intern-tests met B1 en B2 aan, en de ruilcorrectie: een ruil telt pas bij D3, volgens de D0-lijst |
| 2 | `854f3e1` | merge `m4rone/spike/intern-veilig2` (c09df6c) |
| 3 | `fb6de25` | fix: ook een basis met een tweede speler wordt verzoend |
| 4 | `b1c473e` | merge `m4rone/spike/veilig-r2` (5fea5eb, met de vier fixcommits en het rapport) |

De eindstand (de sha's, de testexit en het aantal GUARD E2E) staat onderaan.

## Botsingen

Alle drie zaten ze alleen in `src/StaticTests.cc`, en alle drie op dezelfde manier: beide kanten voegden een nieuw testblok toe op dezelfde plek. Ik heb ze opgelost door beide blokken heel over te nemen, eerst het mijne en dan dat van de andere tak. Geen enkele botsing raakte gedrag.

1. **Stap 1.** Mijn unittest van `BaseBefore` en het ruilblok van Veilig-2 staan nu na elkaar in de K7-sectie.
2. **Stap 2.** Mijn bank-e2e-gevallen en de handler-e2e-gevallen van intern-veilig2 (M1, K7, K1, de gebruiksregels) staan nu na elkaar vóór `expect_eq(0u, e2e_failed)`.
3. **Stap 4.** Git had mijn herhaaltest (B-REGIE-21-21) en het R2-blok van veilig-r2 in elkaar geweven, omdat ze allebei een eigen `Channel` hebben. Ik heb beide blokken heel teruggezet uit de twee ouders.

`SpireFlagBridge.cc` en `.hh` zijn zonder botsing samengevoegd. veilig-r2 staat al op 66a32cb, dus zijn vangnetcode (c42b6bc, 72c9fa8) botst niet met veilig-2.

De tests bewijzen dat alles bewaard bleef: alle blokken draaien en zijn groen, namelijk 35 GUARD E2E, 8 R2-gevallen, de herhaaltest en de K7/ruil-unittests.

## Stap 3: ook een gedeelde basis verzoenen

De server ziet nu precies wat er tussen spelers verhuist, nog vóór de 98 van de basis:
- de opraap door een ander (K7b);
- de uitvoering van een ruil (Veilig-2, de D0-lijst);
- in beide gevallen haalt hij het item uit de momentopname van de gever.

Het record van vóór de basis is die momentopname vlak voor de 98. Daarom zit een item dat bij een ander landde er al niet meer in, en komt het bij de verzoening niet terug (B-AF-53). De bewaking die ik voor een gedeelde game had gebouwd is daarom weg (`spire_guard_base_before_apply` heeft geen `shared` meer). Elke vloer- of ruilstap gooit een ouder record weg.

De ruilgevallen (e) uit veilig-bank deden eerst een ruil na zonder dat de server iets zag. Nu spelen ze de echte D0 en D2 door de server, en controleren ze dat de momentopname van de gever het item kwijt is.

Rood zonder de fix (op `854f3e1` met de nieuwe tests, `.lab/tests-red3.log`):
```
GUARD E2E (B-REGIE-21-24 (f) the base game with a partner who stays, a deposit on the bank, the client drops before the card write: the item exists once) FAILED: the deposited item is there 0 times after the login
GUARD E2E (B-REGIE-21-24 (f) the base game with a partner who stays, a trade to him and a withdrawal from the bank, the client drops before the card write: the withdrawn item once, the traded one gone) FAILED: the withdrawn item is there 2 times after the login
GUARD E2E (B-REGIE-21-24 (f) the base game with a partner who left before, a deposit on the bank, the client drops before the card write: the item exists once) FAILED: the deposited item is there 0 times after the login
GUARD E2E (B-REGIE-21-24 (f) the base game with a partner who left before, a trade to him and a withdrawal from the bank, the client drops before the card write: the withdrawn item once, the traded one gone) FAILED: the withdrawn item is there 2 times after the login
```

Met de fix (`fb6de25`) zijn alle vier groen. Het log toont dat de verzoening liep: bij de storting `1 item(s) back`, bij de opname `1 taken out (still in the bank)`. De ruilgevallen (e) en de opraap via de vloer blijven groen: het geruilde item komt niet terug.

Wat blijft:
- **Dubbel bij één speler, aanvaard.** Een verkocht, gevoerd, getekt of gebruikt item komt bij een val terug, en een aankoop krijgt zijn meseta terug. Ook een meseta-opname blijft dubbel.
- **Volle inventaris.** Een item dat terug moet maar niet meer past, is weg.
- **Item van dezelfde soort.** Vindt de ruilstap het item niet met precies dezelfde data, dan neemt hij het eerste item van die soort (zo werkt K7b). In dat geval kan het geruilde item zelf in het record blijven staan. Dat gebeurt alleen als de D0-data afwijkt van de 98-data. Beide worden hetzelfde gedecodeerd, dus ik verwacht het niet.
- Het uitbreidingsverzoek van veilig-bank (een ruilhaak) is hiermee overbodig: Veilig-2 heeft die haak al.

## Eindstand

- Tak: zie `git rev-parse HEAD` in het antwoord aan de regie. Dit rapport is de laatste commit.
- `git diff --quiet ee0fa66 HEAD -- src/SpireDrops.cc src/SpireDrops.hh; echo $?` geeft `0`.
- De twee startlevel-aanroepen zijn niet geraakt.
