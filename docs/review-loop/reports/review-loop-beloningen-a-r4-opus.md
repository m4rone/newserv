# Review-loop beloningen deel A, ronde 4: opus (invalshoek doel)

Datum 2026-11-16. Koude review, ronde 4. Fix-commits: newserv `0ef70c6`, quest-repo `00fe5888`.

## Maatstaf en scope

- **Wat ik las.** `git diff 1e0cb5a HEAD` in newserv `feature/beloningen` (HEAD bij aanvang `9c8f2a6`) en de quest-repo
  `7677a86c..0d14b8db`. De geraakte bestanden heb ik volledig gelezen: `SpireRewards.*`, het beloningsdeel van
  `SpireFlagBridge.cc`, `SpireMag.cc`, `SpireDrops.cc`, de deel-A-tests in `StaticTests.cc`, en `rewards.py`,
  `make_spire.py`, `make_spire_rewards.py`, `pc-build.sh`, `apply-on-pc.sh` en `apply-target.sh`.
- **De maatstaf.** Marcks woorden: "de wapen type upgrade is cool, dus als je die kiest krijg je gewoon van die lijn.
  tot aan legendaries is ook wel vet." en "moet je het wel kunnen equippen natuurlijk, nogal nutteloos als het niet
  kan." Daarnaast deel A:
  - Wapen volgt de lijn van het vastgehouden wapen;
  - Nieuwe lijn en Mag-voer;
  - W1-W6 per kamer;
  - vier tiers met de kansen van KV §4, en Legendary na de eerste De Rol Le;
  - elk wapen is te equippen, regel B (de mag groeit mee, nooit boven 200, zakt nooit);
  - één ATA-sleutel voor het grid en voor SpireDrops;
  - de guard geeft het echte karakter zijn inventaris en mag exact terug.
- **Wat een bewijs is.** Een doelpunt telt als getoetst als een mutant op zijn dragende regel een test rood maakt.
  Blijft de mutant groen, dan staat het doelpunt alleen op papier.
- **Buiten scope.** Deel B, uitrol naar pc of lab, een herstart van newserv, nieuw Episode 2- of moeilijkheidswerk, en
  de fase 0-fixes.
- **Eerdere rapporten.** Het r3-rapport heb ik pas gelezen nadat mijn oordeel en mijn fixes vastlagen (zie "Wat ik niet
  heb aangeraakt").

## Wat er goed staat (getoetst, niet aangenomen)

- **Het grid, cel voor cel.** De static tests lopen elke cel door:
  `rewards: 1680 cells checked, 0 need mismatches, 90720 rule B mags checked`. Voor elke klasse, lijn, W, tier en
  special geldt dan:
  - het antwoord is de grid-code;
  - de behoefte van regel B op de run-kopie van het grid is de mag van het grid;
  - de mag van regel B haalt het wapen, blijft ten hoogste 200 en zakt niet.
- **Wapen volgt het vastgehouden wapen.** Mutant ML1 (`spire_reward_held_weapon` geeft altijd `nullptr`) wordt rood.
- **Nooit boven 200, nooit lager.**
  - MF1 (geen `spire_mag_fit_200`) wordt rood.
  - MF2 (de gedragen mag niet als vloer) wordt rood.
- **Legendary pas vanaf trede 2.** MT2 (`trede + 1 >= legendary_from_trede`) wordt rood.
- **SpireDrops leest de AtaModel-sleutel.** MA1 (SpireDrops altijd `TABLE`) wordt rood.
- **De equip wacht op de mag van regel B.** MB1 (de equip zonder wachten) wordt rood.
- **Mag-voer op POW voor een hunter.** MB2 (voer altijd op DEF) wordt rood.
- **Geen itemverlies.** MG1 (de guard neemt de inventaris van de run over) wordt rood.
- **Grid en ontwerp zijn gelijk.** `make_spire_rewards.py --check` geeft OK. De quest-build slaagt met
  `34 gates caught every break of their own input`, en bytes en labels blijven binnen de grens.

## Mutanten (letterlijk)

Elke mutant is één exacte vervanging, gevolgd door een build en `build/newserv run-static-tests`. De runner zette het
bestand terug vanaf een kopie, niet met `git checkout --` (zie de noot onder de tabel). "Voor" is de stand vóór mijn
fixes (`9c8f2a6`), "na" is `0ef70c6`.

| Mutant | Bestand | Oud → nieuw | Voor | Na |
|---|---|---|---|---|
| ML1 | SpireFlagBridge.cc | `spire_reward_held_weapon(...) {` → `{ return nullptr;` | rood, StaticTests.cc:10713 `!(need.pow >= 1)` | – |
| ML2 | SpireRewards.cc | `auto it = table.line_of.find(code);` → `auto it = table.line_of.end(); (void)code;` | **groen** | rood, :7836 `line_name != spire_reward_line_of(rewards, cls, offer.item.data())` |
| MA1 | SpireDrops.cc | `player.ata_model = spire_ata_model_configured();` → `= SpireAtaModel::TABLE;` | rood, :15840 `!(safe_changed)` | – |
| MA2 | SpireFlagBridge.cc | `spire_reward_mag_need(offer->info, p.atp, p.ata, p.mst, table->ata_model)` → `..., SpireAtaModel::SAFE)` | **groen** | rood, :10889 `TAG \| ITEM \| MAG_FOLLOWS != quest_asks(... "the Handgun pick, screen_bare")` |
| MP | SpireFlagBridge.cc | `if ((it != offers.by_slot.end()) && ...)` → `if (false && ...)` | **groen** | rood, :10854 `!(w6_offer.has_value())` |
| MK | SpireFlagBridge.cc | `in.k, in.path == 1, ...` → `1, in.path == 1, ...` (de 'Q' trekt altijd kamer 1) | **groen** | rood, :10854 `!(w6_offer.has_value())` |
| MF1 | SpireMag.cc | `return spire_mag_fit_200(ret, current, need, main_stat);` → `ret[2] = spire_mag_level_of(ret); ... return ret;` | rood, :6114 `200 != c[2]` | – |
| MF2 | SpireMag.cc | `if (held) {` → `if (false && held) {` | rood, :6106 `2350 != word(b, 1)` | – |
| MT2 | SpireRewards.cc | `if (trede >= table.legendary_from_trede) {` → `if (trede + 1 >= ...) {` | rood, :7920 | – |
| MB1 | SpireFlagBridge.cc | `if (item_id && p.mag_due) {` → `if (false && item_id && p.mag_due) {` | rood, :10732 `!(!line_sent(...))` | – |
| MB2 | SpireMag.cc | voer op `main_stat` → voer op stat 0 | rood, :10756 `!(food_rise)` | – |
| MS1 | SpireMag.cc | `return (archetype == "FO") ? 3 : 1;` → `(void)archetype; return 1;` | **groen** | rood, :7927 `spire_mag_stat_word(*led.last_mirrored, z) + ... != spire_mag_stat_word(h.decision.bytes, z)` |
| MG1 | SpireFlagBridge.cc | `merged->inventory = snapshot.inventory;` geschrapt | rood, :10251 | – |
| B9 | SpireFlagBridge.cc | `run_bosses = rr_it->second.boss_rooms;` → `(void)rr_it;` | **groen** | rood, :10911 `1u != spire_static_test_reward_trede(...)` |
| D1 | SpireRewards.cc | de nieuwe `std::sort` van `line_order` geschrapt (B2 teruggedraaid) | n.v.t. | rood, :7903 `std::string("Saber") != cls.line_order.front()` |
| M-B1 (quest) | apply-on-pc.sh | `cp "$P/spire-rewards.json" "$TS/spire-rewards.json"` geschrapt | n.v.t.: de regel bestond niet en de test was groen (36 ok) | `test_apply_rollout_order.sh`: 35 ok, 1 FAIL `clean: the new binary runs, files and config installed ...` |

Twee noten bij de tabel:

- **Mijn eigen fout.** De eerste runs MA2-r en MS1-r gaven rood op :7903. Dat was geen bewijs. De runner zette na ML2-r
  terug met `git checkout --` en wiste daarmee ook mijn nog niet gecommitte B2-fix in `SpireRewards.cc`. Ik heb de fix
  opnieuw aangebracht en de runner omgebouwd naar terugzetten vanaf een kopie. MA2 en MS1 heb ik opnieuw gedraaid
  (r2); hun rode regels staan in de tabel.
- **Hoe "voor" gemeten is bij MK en B9.** `src/StaticTests.cc` stond tijdelijk op `9c8f2a6`. Daarna is het bestand
  teruggezet, de build herhaald en de volledige suite opnieuw gedraaid. `git status` was daarna leeg.

## Bevindingen

### B1 (major, productiecode, quest-repo): de uitrol neemt het beloningsgrid niet mee

- **Wat er gebeurt.**
  - `pc-build.sh` neemt `system/spire-shop.json` en `system/spire-mag.json` uit de vastgezette newserv, maar niet
    `system/spire-rewards.json`.
  - `apply-on-pc.sh` installeert er ook geen.
  - De live `$ROOT/system` op de pc wordt niet door de newserv-checkout bijgewerkt.
- **Gevolg na een uitrol van deze tak.** Elke Wapen-, Nieuwe lijn- en Mag-voer-pick geeft het eigen item van het
  entry (Saber +8, Dagger +5, een Frame), en de beloningsregel noemt geen tier. De doelpunten "van die lijn" en "tot
  aan legendaries" zouden op de pc dus niet bestaan.
- **De vastgezette newserv mist deel A.** `1e0cb5a` heeft geen deel A:
  `git show 1e0cb5a:system/spire-rewards.json` faalt, en die newserv negeert 'Z' sub 6. De server-entries zouden dan
  helemaal niet te pakken zijn.
- **Fix (`00fe5888`).**
  - `pc-build.sh` haalt het grid uit `$NEWSERV` en eist dat `make_spire_rewards.py --check` OK geeft. Een pin zonder
    deel A wordt geweigerd.
  - `apply-on-pc.sh` eist `build-pc/spire-rewards.json` en zet het in `stage/tree/system`.
  - `test_apply_rollout_order.sh` legt het bestand in de fixture en toetst dat het geïnstalleerd is.
- **Bewijs.**
  - De nieuwe pc-build-stappen los gedraaid:
    - pin `1e0cb5a`: `ERROR: newserv 1e0cb5a... has no system/spire-rewards.json`, rc=1;
    - pin `9c8f2a6`: `grid OK`, rc=0;
    - een afwijkend grid: `FAIL: ... is not what make_spire_rewards.py builds`, rc=1.
  - Uitroltests:
    - `test_apply_rollout_order.sh`: 36 ok, 0 FAIL;
    - `test_apply_client_gate.sh`: 13 ok;
    - `test_terugdraai_client_gate.sh`: 25 ok;
    - `test_terugdraai_binary_check.sh`: 14 ok;
    - `test_apply_bridge_migration.sh`: 18 ok.
  - Mutant M-B1 wordt rood.

### B2 (minor, productiecode): de terugvallijn zonder bekend wapen was de hash-volgorde, niet Saber

- **Wat er gebeurt.** `spire_rewards_parse` bouwt `line_order` uit een phosg-dict, en die is een `unordered_map`. Het
  commentaar "the class's first line (Saber for all)" klopte niet. Mijn probe gaf Mechgun voor HU, Shot voor RA en Wand
  voor FO.
- **Waarom de test het niet zag.** De bestaande test `expect_eq(line_order.front(), bare.line)` was een tautologie.
- **Fix (`0ef70c6`).**
  - `line_order` wordt gesorteerd met Saber vooraan.
  - De test eist voor elke klasse "Saber", zowel bij geen wapen (`nullptr`) als bij een onbekend wapen
    (`00 EE 00`).
- **Bewijs.** D1 wordt rood op :7903.

### B3 (major, tests): de lijn houden met een Sega-rare in de hand stond alleen op papier

- **De mutant.** ML2 (`line_of` nooit gevonden) bleef groen.
- **Fix.** Voor elke cel van het grid eist een test dat het aangeboden item weer op dezelfde lijn uitkomt, en dat een
  volgend Wapen met dat item in de hand dezelfde lijn geeft. Dit is Marcks "krijg je gewoon van die lijn. tot aan
  legendaries".
- **Bewijs.** ML2 wordt rood op :7836.

### B4 (major, tests): dat de pick de AtaModel-sleutel volgt stond alleen op papier

- **De mutant.** MA2 (de pick altijd `SAFE`) bleef groen.
- **Fix.** De bridge-case pakt een Handgun op en trekt opnieuw. Met ATA gelijk aan de eis van het wapen pakt hij
  hetzelfde slot drie keer:
  - onder `"table"` eist hij `TAG | ITEM`, dus geen mag;
  - onder `"screen_bare"` eist hij `TAG | ITEM | MAG_FOLLOWS`;
  - daarna zet hij de sleutel terug op `"safe"`.
- **Bewijs.**
  - MA2 wordt rood op :10889.
  - De log toont `needs mag POW 0 DEX 0 MIND 0 (table ATA model)` en `needs mag POW 0 DEX 21 MIND 0 (screen_bare ATA
    model)`.

### B5 (major, tests): dat de pick het aanbod geeft dat de beloningsregel noemde stond alleen op papier

- **De mutanten.**
  - MP (altijd een vers aanbod in de kamer van de laatste 'V') bleef groen.
  - MK (de 'Q' trekt altijd kamer 1) bleef ook groen.
- **Fix.** De bridge-case trekt in kamer 11 (W6) en pakt een wapenslot. Hij eist:
  - dat het antwoord een W6-aanbod van de gedragen lijn is (elke tier en elke special geprobeerd);
  - dat de beloningsregel het label van dat aanbod noemde.
- **Bewijs.**
  - MP en MK worden rood op :10854.
  - De log toont `offer Wapen Epic for room 11 (index 11, W6, ...): EPIC LAST SURVIVOR`, en daarna
    `pick Wapen Epic ...: EPIC LAST SURVIVOR 000206...`.

### B6 (minor, tests): mag-voer op MIND voor een force stond alleen op papier

- **De mutant.** MS1 (voer altijd op POW) bleef groen. Alleen de hunter-tak was getest, via MB2.
- **Fix.** Een unit-test stuurt voor HU, RA en FO een 'Z' ASK met 10 voerniveaus. Hij eist +1000 op stat 1 (POW)
  voor HU en RA, en op stat 3 (MIND) voor FO. De andere stats blijven gelijk.
- **Bewijs.** MS1 wordt rood op :7927.

### B7 (minor, tests met een test-hook): Legendary via de eigen bossen van de run stond alleen op papier

- **Herkomst.** Ik vond het groene resultaat eerst als een uitvoerbestand van ronde 3 in `/tmp/review-loop`. Daarna heb
  ik het zelf gemeten: mutant B9 bleef groen op `9c8f2a6`.
- **Waarom het ertoe doet.** Een oefenrun schrijft geen BossesBeaten in het meta-bestand. Daar opent Legendary na
  De Rol Le alleen via de bosskamers van deze run.
- **Fix.**
  - Een static-test-hook `spire_static_test_run_boss` in `SpireFlagBridge.cc`. Hij zet een boss in de set van de run,
    zoals een 'V' dat doet, en verandert geen gedrag.
  - De bridge-case eist bij een leeg meta-bestand 0, dan 1 (de Dragon in deze run) en 2 (De Rol Le in deze run).
- **Bewijs.** B9 wordt rood op :10911.

## Verificatie na de fixes (letterlijk)

Alles hieronder is gedraaid op newserv HEAD `0ef70c6` en quest HEAD `00fe5888`, na de laatste mutant opnieuw gebouwd.

```
$ mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc) newserv
build exit=0
[100%] Built target newserv
$ build/newserv run-static-tests
static exit=0
56915:I 230887 2026-11-16 10:36:44 - -- All static tests passed
rewards: 1680 cells checked, 0 need mismatches, 90720 rule B mags checked
$ sh tests/spire-follow-city.test.sh build/newserv
follow-city exit=0 ok=12
$ NEWSERV_DIR="$D" bash .../quests/spire/build.sh
quest build exit=0
--- q920 labels used: engine 239 of 239, rewards 768 of 768, economy 486 of 512, score 90 of 256, coop 256 of 256, path 256 of 256, e08 710 of 768, switch 42 of 256, overflow 561 of 768
--- build: .bin + .dat 239982 of 245000 bytes decompressed (.bin 165196)
--- q920: .bin 165196 of 178176 bytes decompressed, heat patch 359 fields, H 0..6 -> build/q920.heat.json
--- selftests: 34 gates caught every break of their own input (56 s)
$ NEWSERV_DIR=... python3 quests/spire/make_spire_rewards.py --check
OK: /home/m4rck/code/worktrees/newserv/beloningen-a/system/spire-rewards.json matches the design grid
$ bash quests/spire/test_apply_rollout_order.sh
rollout test exit=0 ok=36 fail=0
```

Twee noten bij de uitvoer:

- **"skipping" in de log.** De 152 regels met "skipping" in de static-log zijn `ItemCreator ... skipping special
  generation`, geen overgeslagen tests.
- **`git status` na de commits.**
  - newserv is schoon.
  - De quest-repo toont alleen `M new-features/roguelike/overzicht.html`. Die wijziging stond er al vóór deze ronde en
    is niet van mij.

## Wat ik niet heb aangeraakt

- **De pin in `pc-build.sh`.** Die blijft op `1e0cb5a`, want uitrollen valt buiten scope. Met de B1-fix weigert
  `pc-build.sh` die pin nu. De uitrol van fase 1 moet de pin verzetten naar een gepushte newserv met deel A (inclusief
  `0ef70c6`). De weigering is bedoeld: met `1e0cb5a` zijn de beloningsentries van deze quest-tak niet te pakken.
  `main` van de quest-repo heeft die entries niet, dus de huidige uitrol op de pc is niet geraakt.
- **De open punten van ronde 3.** Na mijn oordeel heb ik het r3-rapport gelezen. Drie punten staan daar als niet
  gefixt, en ik heb ze niet opnieuw gemeten:
  - B8: mag-voer kan twee keer tellen na een verloren antwoord;
  - B10: het ontwerp spreekt zichzelf tegen over de tier van Nieuwe lijn;
  - B11: een oud aanbod kan terugkomen als de quest zelf trekt.

  r3-B13 is mijn B7 en is nu gefixt.
- **`test_apply_lab_refusal.sh`.** Die test stopt met exit 2 (`no build-pc/battle; run pc-build.sh first`). Hij vraagt
  een echte build-pc, en die heb ik niet gebouwd. `pc-build.sh` draait deze test zelf.
- **Verder niet aangeraakt.**
  - Deel B, de pc, het lab, de draaiende newserv op de laptop en geheugenkaarten.
  - Niets gepusht.
  - De harnas-regel (`harness-drift-provenance.js`) geldt niet: dit is geen harnaswerk.

## Stopadvies

- **Het doel staat nu.** Elk doelpunt van deel A heeft een test die een mutant op zijn dragende regel rood maakt.
- **Wat er aan productiecode veranderde is klein.**
  - B1: de uitrol neemt het grid mee en weigert een pin zonder deel A.
  - B2: Saber als terugvallijn.
  - B7: een test-hook zonder gedrag.
- **Advies.** Stoppen met de loop voor deel A is verantwoord. Wil je toch nog een ronde, laat die dan alleen de
  uitrolwijziging van B1 lezen.
- **Vóór de uitrol.**
  - Zet de pin in `pc-build.sh` op de gepushte newserv met deel A.
  - Draai `pc-build.sh`; die toetst het grid nu zelf.
  - De bouwagent beslist nog over r3-B8, B10 en B11.
