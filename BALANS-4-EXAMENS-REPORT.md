# Balans 4: elke verslagen baas telt als examen (B-REGIE-20-9), de newserv-kant

Tak `spike/balans-examens`, start a5fa1dc, commit 0f2d80c. Geschreven 2026-11-11; alle getallen zijn die dag in deze
worktree gemeten. De questkant staat in
`/home/m4rck/code/worktrees/pso-roguelike/balans-examens/BALANS-4-EXAMENS-REPORT.md`.

## Uitkomst in één alinea

De server hoeft voor dit besluit niets anders te doen dan hij deed. De quest rekent de score voortaan met elke
verslagen baas en stuurt die score heel in 'K'; de server bewaart hem zoals gestuurd (bit 22 van 'F', bundel 3). Het
examenveld van 'F' (bits 16-17) blijft wat het was: de twee ontgrendelstappen, door de quest afgeklemd op 2. De commit
bevat daarom geen gedragswijziging: drie testcases, drie testbestanden en een commentaar in `src/SpireMeta.hh`.

## Dekking

Dit dekt: de score-rekening in de quest, de eindcode, het bewaarde record en de ontgrendelingen op de server, en de
compatibiliteit met oude records en oude quests. Het dekt niet: wat de GameCube-client op de Wii toont (alleen de Wii
beslist dat) en niet de balans van kamers en vijanden.

## De route, getoetst

Het aanbevolen startpunt had drie delen. Getoetst tegen de code van a5fa1dc:

| Deel | Bevinding |
|---|---|
| Het veld van 2 bits blijft het afgeklemde getal voor de ontgrendelingen | Overgenomen. Afklemmen is nodig, niet alleen netjes: bit 18 direct boven het veld is het deathless-bit. Een 4 in het veld leest als "0 examens en niemand dood". |
| De quest rekent zijn score met het volledige aantal | Overgenomen (questrapport). |
| De server telt het volledige aantal zelf uit wat hij zag | Bestond al: `spire_meta_exams_passed` (`src/SpireMeta.cc:798`) telt alle bazen (3 en 4 komen voor in de bestaande test, `src/StaticTests.cc:7641-7651`). `spire_finish_seen` klemt dat op 2 voor de ontgrendelingen (`src/SpireFlagBridge.cc:7060-7061`). Meer gebruikt de server er niet van, en meer is niet nodig: zie hieronder. |

**Het gat (een run die de server niet kent).** Zou de server de score of een bewaard examenaantal uit zijn eigen
telling afleiden, dan krijgt een run zonder 'B' in deze game te weinig: de server kent dan alleen het veld (hoogstens
2). Opgelost door niets bewaards van de servertelling te laten afhangen. De score reist compleet in 'K' en de server
neemt hem over. Gemeten in de rij "no 'B' seen": score 11000 bewaard, vlaggen van stap 2.

**De server bewaart geen examenaantal.** `records.json` heeft BestScore, BestTime, Runs, Won, Splinters,
SplintersTotal, Difficulties en Credited (`src/SpireFlagBridge.cc:2801-2835`). De examens zitten in het record via de
score. Het tweede deel van AC 2 ("het examenaantal dat de server bewaart") is daarmee leeg: er is geen getal dat van
het eindscherm kan afwijken. Ik heb geen veld toegevoegd; niets leest het (zie Gezien).

## AC per stuk

| AC | Stand | Bewijs |
|---|---|---|
| 1 score op het eindscherm | questkant | questrapport |
| 2 bewaarde score gelijk aan het eindscherm, ook na hervatten en vanaf Episode 2 | gehaald | case 2 (twaalf runs door 'B', 'V', 'K', 'F') en case 3 (hervat uit een bewaarpunt van a5fa1dc) |
| 3 ontgrendelingen exact als nu | gehaald | case 1: voor elke afloop van beide episodes is de 'F'-code bit voor bit die van de vorige quest, en de vlaggen volgen één regel (stap 1 vanaf één baas, stap 2 vanaf twee, een gewonnen run beide) |
| 4 oude records, oude quest, oud bewaarpunt | gehaald | case 3 op drie bestanden die a5fa1dc schreef; de bestaande K3-cases (quest d6fc2ef) blijven groen |
| 5 kleinste route | gehaald | geen wijziging in `src/SpireFlagBridge.cc/.hh`, geen formaatwijziging |

## Tests

Nieuw in `src/StaticTests.cc` (binnen het blok `-- Spire kwaliteit-3`, na case 5), drie cases:

1. `balans-4: at every run end the server keeps the score with every exam and unlocks what it unlocked before`:
   68 aflopen (per episode 15 verloren en een gewonnen met en zonder dode, elk met 0 en 3 pacts), zonder client.
2. `balans-4: what a run of the new quest stores, through 'B', 'V', 'K' and 'F': ...`: twaalf runs met een echte
   `Client` in een echte `Lobby`.
3. `balans-4: records, unlocks and a savestate from before load, show and lose nothing`: de drie bestanden van één
   karakter, geschreven door a5fa1dc op de telling van de quest 3991ce3.

De testbestanden (`tests/spire-records-before-all-exams.json`, `tests/spire-meta-before-all-exams.json`,
`tests/spire-savestate-before-all-exams.json`) zijn op 2026-11-11 door de ongewijzigde servercode van a5fa1dc
geschreven, met een tijdelijk blok in de test dat daarna is verwijderd: Spire Run I verloren in k12 (9500), Spire
Run I gewonnen zonder dode (11500, 3000 s), Spire Run II verlaten aan het begin van k20.

### Groen

Commando, op 0f2d80c:

```
cd /home/m4rck/code/worktrees/newserv/balans-examens
./build/newserv run-static-tests > build/b4-final.log 2>&1
echo $?
```

Uitvoer: `0`. Staart van het log:

```
I 889526 2026-11-11 15:05:46 - [C-22] Deleted
I 889526 2026-11-11 15:05:46 - -- All static tests passed
```

14990 regels, 0 keer `FAILED`. Twaalf W-regels: de elf van de basis (a5fa1dc: 14403 regels, exit 0) plus één nieuwe
uit de rij zonder 'B': `Spire records: no run id at 'F' (no 'B' in this game): paid the old rooms + 5 per boss`. Dat
is het bestaande terugvalpad, door de test bewust geraakt. Eén E-regel, dezelfde als op de basis (back-uptest).

De twaalf runs van case 2, letterlijk (ingekort tot de meetregel):

```
B4 measure: Episode 1 q921 lost in k1: quest 'F' 46400BB8, end screen score 1000 (before: 1000) -> flags 0380; BestScore 1000, BestTime 0, Runs 1, Won 0
B4 measure: Episode 1 q921 lost in k5, past the Dragon: quest 'F' 46410BB8, end screen score 4500 (before: 4500) -> flags 0380 0381 0384 0385; BestScore 4500, BestTime 0, Runs 1, Won 0
B4 measure: Episode 1 q921 lost in k9, past De Rol Le: quest 'F' 46420BB8, end screen score 8000 (before: 8000) -> flags 0380 0381 0382 0384 0385 0386 0387; BestScore 8000, BestTime 0, Runs 1, Won 0
B4 measure: Episode 1 q921 lost in k12, past Vol Opt: quest 'F' 46420BB8, end screen score 11000 (before: 9500) -> flags 0380 0381 0382 0384 0385 0386 0387; BestScore 11000, BestTime 0, Runs 1, Won 0
B4 measure: Episode 1 q921 lost in k12, 2 pacts: quest 'F' 46520BB8, end screen score 16500 (before: 14250) -> flags 0380 0381 0382 0384 0385 0386 0387; BestScore 16500, BestTime 0, Runs 1, Won 0
B4 measure: Episode 1 q921 won deathless: quest 'F' 46C60BB8, end screen score 14500 (before: 11500) -> flags 0380 0381 0382 0383 0384 0385 0386 0387 0388 038B 03A0 03A1 03A2; BestScore 14500, BestTime 3000, Runs 1, Won 1
B4 measure: Episode 2 q922 lost in k16: quest 'F' 46400BB8, end screen score 1000 (before: 1000) -> flags 0380; BestScore 1000, BestTime 0, Runs 1, Won 0
B4 measure: Episode 2 q922 lost in k19, past Barba Ray: quest 'F' 46410BB8, end screen score 4000 (before: 4000) -> flags 0380 0381 0384 0385; BestScore 4000, BestTime 0, Runs 1, Won 0
B4 measure: Episode 2 q922 lost in k22, past the Gol Dragon: quest 'F' 46420BB8, end screen score 7000 (before: 7000) -> flags 0380 0381 0382 0384 0385 0386 0387; BestScore 7000, BestTime 0, Runs 1, Won 0
B4 measure: Episode 2 q922 lost in k28, past the Gal Gryphon: quest 'F' 46420BB8, end screen score 11500 (before: 10000) -> flags 0380 0381 0382 0384 0385 0386 0387; BestScore 11500, BestTime 0, Runs 1, Won 0
B4 measure: Episode 2 q922 won deathless: quest 'F' 46C60BB8, end screen score 14500 (before: 11500) -> flags 0380 0381 0382 0383 0384 0385 0386 0387 0388 038C 03A0 03A1 03A2; BestScore 14500, BestTime 3000, Runs 1, Won 1
B4 measure: Episode 1 q921 lost in k12, no 'B' seen: quest 'F' 46420BB8, end screen score 11000 (before: 9500) -> flags 0380 0381 0382 0384 0385 0386 0387; BestScore 11000, BestTime 0, Runs 0, Won 0
```

De vlaggen per run zijn dezelfde als in de tabel van bundel 3 (`kwaliteit-3-robuust-newserv.md:55-62`).

### Rood

**Eerlijk gezegd: deze tests waren niet rood op a5fa1dc, en dat kan ook niet.** De server verandert niet, dus er is
geen serverfix om zonder te meten. Op de ongewijzigde server waren case 1 en 2 bij hun eerste echte run groen
(`build/b4-fixtures.log`, 12 meetregels zonder `EXPECTED`); wat daar rood stond waren twee fouten in mijn eigen test
(een masker dat het deathless-bit meenam, en een verkeerde volgorde in case 3: een nieuwe run wist het bewaarpunt van
de vorige, dus het oude bewaarpunt moet eerst hervat worden).

Om te tonen dat de cases iets vangen heb ik drie eigenschappen waar ze op rusten tijdelijk uitgezet in één build
(alle drie in `src/SpireFlagBridge.cc`, niet gecommit, daarna teruggezet; `git status` toont de boom schoon op
0f2d80c):

| Mutatie | Wat hij uitzet |
|---|---|
| M1 `if (seen.start_room > 1) {` in plaats van de toets op bit 22 | de score van de quest blijft niet meer staan |
| M2 `passed` zonder `std::min(..., 2)` in `spire_finish_seen` | de server zet zijn volle telling in het veld |
| M3 `ret.new_score = (score > rec.best_score) \|\| !won;` | een verloren run overschrijft de beste score |

Uitvoer (`build/b4-red.log`, exit 1), de regels met `FAILED` en `EXPECTED`:

```
K3 (1: a finished run gets the exams and the score of what it played) FAILED: failure at .../src/StaticTests.cc:7714: e1.finish != got.finish_value
K3 (balans-4: at every run end the server keeps the score with every exam and unlocks what it unlocked before) FAILED: failure at .../src/StaticTests.cc:8230: now.finish != got.finish_value
B4 measure: Episode 2 q922 lost in k16: quest 'F' 46400BB8, end screen score 1000 (before: 1000) -> flags 0380; BestScore 0, BestTime 0, Runs 1, Won 0 (EXPECTED flags 0380; score and BestScore 1000; before 1000)
B4 measure: Episode 2 q922 lost in k19, past Barba Ray: ... BestScore 0, ... (EXPECTED ...; score and BestScore 4000; before 4000)
B4 measure: Episode 2 q922 lost in k22, past the Gol Dragon: ... BestScore 0, ... (EXPECTED ...; score and BestScore 7000; before 7000)
B4 measure: Episode 2 q922 lost in k28, past the Gal Gryphon: ... BestScore 4000, ... (EXPECTED ...; score and BestScore 11500; before 10000)
B4 measure: Episode 2 q922 won deathless: ... BestScore 7000, BestTime 3000, Runs 1, Won 1 (EXPECTED ...; score and BestScore 14500; before 11500)
K3 (balans-4: what a run of the new quest stores, through 'B', 'V', 'K' and 'F': the end screen's score, the flags of before) FAILED: failure at .../src/StaticTests.cc:8361: 0u != wrong
K3 (balans-4: records, unlocks and a savestate from before load, show and lose nothing) FAILED: failure at .../src/StaticTests.cc:8431: 11500u != rec1.best_score
```

Welke mutatie welke case raakt: case 1 valt op M2 (Ep1, k12: het veld wordt 3), case 2 op M1 (alleen de
Episode 2-rijen, elk 7500 te laag), case 3 op M3 (met M1 alleen blijft 11500 staan, want 4000 is lager). Dat laatste
is beredeneerd uit de code, niet apart gebouwd.

Een eerdere poging met twee andere mutaties (`f.exams >= 3` in `spire_meta_flags_for_finish` en
`ret.new_score = true`) kwam niet tot mijn cases: de bestaande check `L4 the same 'F' again (after a reload) counts
nothing` stopte de run op `src/StaticTests.cc:1134` (`build/b4-red-existing-tests.log`). Die twee regressies zijn dus
al door bestaande tests gedekt.

## Geraakte bestanden

- `src/SpireMeta.hh:25-29` en `:46`: commentaar. Bits 16-17 zijn de ontgrendelstappen, niet het examenaantal van de
  score.
- `src/StaticTests.cc:8156-8490`: de drie cases en hun hulpfuncties.
- `tests/spire-records-before-all-exams.json`, `tests/spire-meta-before-all-exams.json`,
  `tests/spire-savestate-before-all-exams.json`: nieuw.
- `src/SpireFlagBridge.cc` en `.hh` (ander spoor): niet geraakt in de commit. Alleen tijdelijk gemuteerd voor de
  rode meting en teruggezet.

## Beslissingen in dit werk (door de regie te loggen)

- De server krijgt geen gedragswijziging. Dekking: AC 5 ("kies de kleinste route die AC 1-4 haalt") en de meting
  hierboven: AC 2, 3 en 4 zijn op a5fa1dc al waar voor de nieuwe quest.
- Geen examenaantal in `records.json`. Dekking: AC 4 (oude records blijven geldig) en AC 5; niets leest zo'n veld.
- De commit heet `test:` en niet `feat:` zoals de opdracht voorschreef, omdat er geen gedrag verandert.

## Gezien, niet gebouwd

- log · `Spire meta: finish ... (exams 2, ...)` noemt de ontgrendelstappen (hoogstens 2), naast een score waar vier
  examens in zitten · `src/SpireMeta.cc:480` · één woord in een logregel, maar wie het journaal leest kan het als
  verschil zien
- datakwaliteit · het terugvalpad van de splinters (geen Payout-blok, of geen 'B' in de game) telt als bazen het
  grootste van het examenveld (hoogstens 2) en de baaskamers die de server zag; een gewonnen run zonder geziene
  baaskamers krijgt er 2 betaald in plaats van 4 · `src/SpireFlagBridge.cc:3128` · bestond al, verandert niet door
  dit werk
- datakwaliteit · de quest zonder bit 22 (d6fc2ef) die na kamer 1 begint houdt in de scorecorrectie van de server de
  oude telling (twee examens) · `src/SpireFlagBridge.cc:7069` · klopt met wat die quest zelf toont
- ux · het Records-bord toont geen examens; "meeste bazen in één run" zou een nieuw veld en een nieuwe tekst zijn ·
  `src/SpireFlagBridge.cc:2988`
- proces · deze worktree loopt 1 commit achter op `m4rone/spike/kwaliteit-2` (50fedd8, raakt `src/StaticTests.cc` in
  het back-updeel direct onder mijn cases): bij samenvoegen kan daar een conflict staan
- proces · de hook meldt bij de commit dat integratiebranch en uitrol voor deze repo niet zijn vastgelegd; ik heb
  niets samengevoegd of uitgerold
