# Review-loop beloningen-b-units, ronde 1: opus (invalshoek doel)

## Maatstaf en scope

- **Maatstaf:** het doel "beloningen-b-units", deel B van `new-features/roguelike/design/beloningen-per-klasse.md`
  (B-E02-23 punt 1, 2 en 4; B-E02-25; B-E02-26):
  - units (Kracht, serversoort 5): de server kiest bij de trekking per klassefamilie en tier, trekt de unit daarna aan in
    het eerste vrije slot van het gedragen frame; zonder vrij slot of frame blijft hij in het pak en leest de speler
    waarom; gedragen units tellen mee voor de ATP- en MST-eis van een wapen; één nieuwe regel "Unit" aan het eind van
    elke pool, met een eigen item als de server niets te geven heeft;
  - kit-frame: gemaakt met alle bytes (slotbyte 4) en door de server aangetrokken bij de 'C' 13 van het kitwapen,
    alleen bij r143 = 1; pool- en winkelframes ook 4 slots;
  - vaste grenzen: geen itemverlies en geen dubbele gift, geen freeze, geen item tijdens een golf, de tellers per
    poolregel van een bewaarde run kloppen (een pool groeit alleen aan het eind), het echte karakter komt terug zonder
    iets uit de run.
- **Scope:** newserv `git diff 8e77957 HEAD` (eb494a9 plus de goal-commit daa689e) en de quest-repo sinds 458ee903
  (a93df718, 3f83fed9, 8c9ad24a). Gelezen: de hele diff en de geraakte functies volledig (`SpireEquipItem.hh`,
  `SpireFlagBridge.cc` pick/RESULT/equip-coroutines en `spire_equip_kit_send`, `SpireRewards.hh/.cc`, `SpireDraw.cc`,
  de nieuwe statische tests; `rewards.py` `kit_meta_frame`/`give_kit_item`, `make_spire_rewards.py` `build_units`,
  `data/rewards.json`, `data/pool-order.json`, `sim/curve_spire.py` `armor_item`, de economy-bestanden).
- **Buiten scope (opdracht):** schijven (soort 4), deel A behalve waar B het raakt, de rung-bestanden Hard/Very
  Hard/Ultimate, ATA-units, uitrol en herstarts, de bouwscripts en poorten van de quest-repo (`build.sh`,
  `pc-build.sh` behalve de pinregel, `check_*.py`, `test_*`).
- Geen eerdere review-verslagen gelezen.

## Wat er goed staat (getoetst, niet aangenomen)

- **Unit per tier uit de juiste lijst.** Mutant M7 (elke tier trekt uit de Normal-lijst) wordt rood:
  `StaticTests.cc:7920: tiers[t][r] != code`. De test loopt elke klasse, tier en lijstpositie af (128 unit-aanbiedingen).
- **Slots van het frame tellen.** Mutant M8 (de slotbyte genegeerd, altijd vier) wordt rood:
  `StaticTests.cc:7949: !(spire_reward_free_unit_slot(0x00, 0) == std::nullopt)`.
- **God/Ability telt voor ATP en MST.** Mutant M9 (stat 8 telt niet voor ATP) wordt rood:
  `StaticTests.cc:7941: 70u != b.atp`.
- **Een pool groeit alleen aan het eind.** Quest-mutant Q2 (de HU-regel "Unit" vóór H30) wordt rood in
  `check_pool_order.py`: `FAIL rewards.json pool HU: index 27 is H31 Unit, the shipped order has H30 Trimate x4 there
  (a resumed run would read its pick count as that line's)`.
- **Het grid in newserv is wat de generator maakt.** Quest-mutant Q3 (techniek-units vanaf Normal in plaats van Rare)
  wordt rood in `make_spire_rewards.py --check`: `FAIL: .../system/spire-rewards.json is not what make_spire_rewards.py
  builds from .../beloningen-per-klasse.json`.
- **Winkelframes met 4 slots.** `sim/economy_regen.py regen` naar een lege map en `gate` tegen `data/`: `gate=0`, en
  `cmp` op alle vier economy-bestanden: identiek. De vier bestanden zijn dus exact wat `armor_item` met slotbyte 4
  maakt.
- **De kit-frame-routine maakt het frame met slotbyte.** In de gebouwde bron `build/spire-gc-e.src.txt` staat
  `label08EB`: `let r60,r61; let r61,r62; let r62,r63; let r65,r64; clear r63, r64, r66..r71; item_create2 r60-r71, r72`;
  zes kit-reeksen roepen hem aan met r61-r64 = 01 01 00 04. Gelezen, niet door een poort bewaakt (zie B7).
- **Ruimte.** q920 `.bin + .dat 242494 of 245000 bytes`, labels binnen hun blokken (rewards 768 of 768, economy 486
  of 512); 35 selftest-poorten vingen elke breuk van hun eigen invoer.

## Mutanten (letterlijk)

Elke mutant met `python3` op de bron gezet, newserv herbouwd, `build/newserv run-static-tests`, daarna
`git checkout -- src`. Vóór de fixes (newserv eb494a9/daa689e):

| mutant | wat | vóór de fixes | na de fixes (3c6f250) |
|---|---|---|---|
| M1 | `SpireDraw.cc`: srv 5 weer gelezen als geen serverbeloning (`(srv >= 0) && (srv <= 3)`) | groen | rood, `StaticTests.cc:8649` (B8) |
| M2 | `spire_equip_find_kit_frame`: frame zonder slots telt ook | groen | rood, `StaticTests.cc:7982` |
| M3 | `spire_equip_find_kit_frame`: een gedragen frame stopt het zoeken niet | groen | rood, `StaticTests.cc:7988` |
| M4 | `spire_reward_worn`: ook niet-gedragen units tellen | groen | rood, `StaticTests.cc:7958` |
| M5 | pick: gedragen units tellen niet voor de eis (`p.atp = stats.atp; p.mst = stats.mst;`) | groen | rood, E2E `StaticTests.cc:11222` |
| M6 | `spire_equip_kit_send`: het kit-frame gaat nooit uit (`if (false)`) | groen | niet opnieuw (B7) |
| M7 | unit-aanbod: elke tier uit de Normal-lijst | rood, `StaticTests.cc:7920` | |
| M8 | vrij slot: slotbyte genegeerd | rood, `StaticTests.cc:7949` | |
| M9 | God/Ability (stat 8) telt niet voor ATP | rood, `StaticTests.cc:7941` | |
| M10 | geen regel als de unit in het pak blijft (ticker weg) | groen | rood, E2E "did not get to the Unit's line" |
| M11 | `Units Items` neemt een niet-unit aan (`if (false) throw`) | groen | rood, `StaticTests.cc:7999` |
| M12 | (nieuw, na de fix) r143 = 0 met vrij slot: geen "equip"-regel | n.v.t. | rood, E2E "did not get to the Unit's line" |
| Q1 | `rewards.py` `kit_meta_frame`: regel `let(r65, r64)` (de slotbyte) weg | groen: build rc=0, `35 gates caught every break`, `label08EB` zonder `let r65, r64` | niet opnieuw (B7) |
| Q2 | HU-regel Unit vóór H30 | rood, `check_pool_order.py` | |
| Q3 | `UNIT_LIST_FIRST_TIER = {'techniek': 0}` | rood, `--check` | |

M1-M6, M10 en M11 samen op één build vóór de fixes: `make=0`, `static=0`, `-- All static tests passed`, en de
quest-build daartegen `rc=0`, `--- selftests: 35 gates caught every break of their own input (130 s)`. Die claims
stonden dus alleen op papier.

## Bevindingen

### B1 — serversoort 5 in de trektabel was niet getest (minor)

- **Mutant:** M1 bleef groen: de loader-test "a server reward entry's srv and own bytes" had geen srv-5-regel, en
  geen enkele end-to-end-case speelde een Unit-pick.
- **Fix (3c6f250, alleen test):** de loader-case krijgt een regel `{"Unit","srv":5,"bytes":"03 0B 01 ..."}` en eist
  `srv_kind == 5` met eigen bytes; de nieuwe end-to-end-case speelt de Unit-pick door de bridge.
- **Bewijs:** M1 op 3c6f250 is rood: `B8 (loader: a server reward entry's srv and own bytes; ...) FAILED: failure at
  .../StaticTests.cc:8649: 5 != e[6].srv_kind`, en daarmee `StaticTests.cc:8665: 0u != b8_failed`.

### B2 — het kit-frame zoeken ('C' 13) was niet getest (minor)

- **Mutanten:** M2 en M3 bleven groen; `spire_equip_find_kit_frame` had geen enkele test.
- **Fix (3c6f250, alleen test):** gevallen voor een frame met slots (zijn id), twee frames (het nieuwste), een
  ingepakt frame (0x400, het vorige), een frame zonder slots (geen) en een frame dat al gedragen wordt (geen).
- **Bewijs:** M2 op 3c6f250 is rood: `StaticTests.cc:7982: !(spire_equip_find_kit_frame(inv) == std::nullopt)`; M3
  is rood: `StaticTests.cc:7988: !(spire_equip_find_kit_frame(inv) == std::nullopt)`.

### B3 — "gedragen units tellen mee voor de eis" stond alleen op papier (minor)

- **Mutanten:** M4 en M5 bleven groen. `spire_reward_worn` was `static` en vroeg de tabel via de client; er was geen
  test die slots, bonus of de pick met units raakte.
- **Fix (3c6f250, productiecode zonder gedragswijziging plus test):** `SpireRewardWorn` en
  `spire_reward_worn(ipt, inventory)` staan nu in `SpireFlagBridge.hh`, met de item parameter table als invoer; beide
  aanroepen geven `item_parameter_table(Version::GC_V3)` mee, zoals de functie eerst zelf deed. Unittest: frame met
  4 slots, God/Power in slot 0, God/Mind in slot 2, Knight/Power in het pak: `occupied 0x05`, ATP en MST uit de tabel,
  vrij slot 1; zonder tabel tellen de slots wel en de stats niet; slotbyte 9 geeft 4; frame in het pak geeft niets.
  End-to-end (FOmarl, Saber- en Cane-lijn in W2): met God/Power gedragen antwoordt een Saber-pick zonder
  `MAG_FOLLOWS`, met Knight/Power in plaats daarvan mét; hetzelfde voor MST met God/Mind tegen Priest/Mind. Journaal:
  `needs mag POW 0 DEX 0 MIND 0 (safe ATA model; worn units count ATP +50 MST +0); the run mag already meets it` tegen
  `needs mag POW 23 ... (worn units count ATP +5 MST +0); rule B's run mag follows`.
- **Bewijs:** M4 op 3c6f250 is rood: `StaticTests.cc:7958: static_cast<uint32_t>(ipt->get_unit(0x03).amount) !=
  worn.bonus.atp`; M5 is rood: `GUARD E2E (review r1 (part B): ...) FAILED: failure at .../StaticTests.cc:11222:
  SPIRE_REWARD_ANSWER_TAG | SPIRE_REWARD_ITEM != weapon_pick("a Saber pick with God/Power worn")`.

### B4 — bij r143 = 0 bleef een unit zonder enige regel in het pak (minor)

- **Wat:** `spire_reward_equip_unit` toetste eerst of de client SpireEquipItem heeft. Bij r143 = 0 eindigde hij daar:
  geen regel, ook niet zonder frame of met een vol frame. Het doel zegt "without a free slot or a frame, the unit
  stays in the pack and the player reads why". Bij r143 = 0 maakt de kit `equip_item` een frame zónder slots, dus elke
  Unit uit de pool viel juist daar stil in het pak. De regel "Unit slots full" klopte ook niet voor een frame zonder
  slots.
- **Mutant:** M10 (de regel weg) bleef groen: geen test zag de regel.
- **Fix (3c6f250, productiecode):** eerst geen frame of geen vrij slot (regel, ongeacht r143), dan r143 = 0 met een
  vrij slot: `spire_equip_missing_line_in_turn(..., SPIRE_EQUIP_RESUME_UNIT, ...)`, de bestaande "Equip your units:
  Item Pack, Equip."-regel op zijn beurt, net als bij een wapen bij r143 = 0. De tekst is nu "No free unit slot: the
  unit is in your Item Pack.", die past bij vol en bij geen slots. Bij r143 = 1 verandert alleen de volgorde van twee
  toetsen die elkaar uitsluiten: hetzelfde gedrag. End-to-end bij r143 = 0: frame met vrij slot geeft de
  equip-regel, geen frame de no-frame-regel, vier units in vier slots de no-slot-regel.
- **Bewijs:** M10 op 3c6f250 is rood: `GUARD E2E (review r1 (part B): ...) FAILED: the server did not get to the
  Unit's line within 15 seconds`. M12 (de nieuwe equip-regel bij r143 = 0 weg) is rood met dezelfde regel. Journaal
  van de groene run: `equip in the pack (r143 = 0, the player's own; free slot 1)`, dan `Spire equip: item 00810043
  still not equipped when its line's turn came (planned 7999 ms after its check); sent "Equip your units: Item Pack,
  Equip."`, daarna `equip in the pack (no frame worn)` en `equip in the pack (no free slot)`, elk met zijn regel.

### B5 — het eigen item van Mag food was een frame zonder slots (minor, quest-repo)

- **Wat:** B-E02-26 zegt "één regel: elk frame heeft vier slots", maar H14, R14 en F24 (Mag food, `srv mag_voer`) geven
  als de server niets te geven heeft `01 01 00 00 00 00 06 ...`: Frame DEF+6 met 0 slots. Wie dat frame aantrekt
  verliest zijn units uit de slots, en daarmee de stats die zijn wapen dragen.
- **Mutant:** geen poort ziet het: de quest-build vóór de fix was groen met deze bytes (`rc=0`, 35 selftests).
- **Fix (quest-repo fd81e2c3, data):** slotbyte 4 (`01 01 00 00 00 04 06 ...`) in `data/rewards.json` en, met de
  hand zoals bij de vier pool-frames, in de pin van `data/pool-order.json` (met een zin in `bron`). Het
  q929-labartefact opnieuw gebouwd (`build.sh --c4 server-all`): alleen `build-c4-server-all/spire-draw.json`
  verandert (3 regels), `q929-gc-e.bin` blijft byte-gelijk.
- **Bewijs:** `check_pool_order.py`: `pool order: 4 file(s), 0 FAIL`; de gebouwde `build/spire-draw.json` draagt drie
  keer `Mag food 3 01 01 00 00 00 04 06 00 00 00 00 00`; `git show HEAD:quests/spire/data/rewards.json` telt 3 regels
  met de nieuwe bytes.

### B6 — de weigering van een niet-unit in `Units` was niet getest (minor)

- **Mutant:** M11 bleef groen.
- **Fix (3c6f250, alleen test):** het echte grid plus één ingevoegd `Units`-item `000100` (een Saber) moet bij het
  laden een `runtime_error` geven; het onveranderde grid laadt met evenveel units als de rest van de test.
- **Bewijs:** M11 op 3c6f250 is rood: `StaticTests.cc:7999: expected exception, but none raised`.

### B7 — de kit-frame-keten heeft geen poort, niet gefixt (minor)

- **Wat:** twee schakels van "kit-frame met alle bytes en door de server aangetrokken" staan alleen op papier:
  - newserv `spire_equip_kit_send` (M6: het frame gaat nooit uit, groen). De harness kan SpireEquipItem niet spelen:
    de nep-client gooit op een onbekende functie-aanroep, en met `spire_equip_item_patch` uit is r143 = 0 en loopt
    dit pad niet;
  - quest `kit_meta_frame` (Q1: zonder `let r65, r64` is het kit-frame weer 0 slots, build en alle 35 poorten groen).
- **Waarom niet gefixt:** een poort op Q1 hoort in `check_*.py`/`test_*`, buiten mijn scope (die raakt de aparte
  bouwsessie); M6 vraagt een SpireEquipItem-dubbel in de harness, een eigen stuk werk. Zelf gelezen: de gebouwde bron
  (`label08EB`) en de C++ kloppen.
- **Wat het nu bewijst:** Marcks avondrun. Eerste kamer: het kit-frame toont 4 slots en de eerste Unit staat in slot 1.

## Verificatie na de fixes (letterlijk)

Alles na de laatste mutant, op newserv 3c6f250 en quest fd81e2c3; `src` schoon (`git status --short` in newserv
toont alleen dit verslag):

```
$ cd /home/m4rck/code/worktrees/newserv/beloningen-b && mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc) newserv
build_rc=0
[  4%] Linking CXX executable newserv
[100%] Built target newserv

$ build/newserv run-static-tests
static_rc=0
I 815194 2026-11-16 16:50:33 - -- All static tests passed
(0 regels met FAILED; "rewards: 25 units, 128 unit offers checked";
 "GUARD E2E (review r1 (part B): a Unit reward through the bridge, its line, and worn units in a weapon's requirement)")

$ sh tests/spire-follow-city.test.sh build/newserv
follow_rc=0
ok: partner floor 1, leader floor 2 -> skip
ok: partner floor 15, leader floor 14 -> warp
ok: spire-lives-check

$ D=$(mktemp -d) && ln -s .../newserv/beloningen-b/build/newserv .../src .../system "$D"/ && NEWSERV_DIR="$D" bash .../pso-roguelike/beloningen-b/quests/spire/build.sh
quest_rc=0
--- q920 labels used: engine 239 of 239, rewards 768 of 768, economy 486 of 512, score 90 of 256, coop 256 of 256, path 256 of 256, e08 715 of 768, switch 45 of 256, overflow 590 of 768
--- build: .bin + .dat 242494 of 245000 bytes decompressed (.bin 167708)
--- q920: .bin 167708 of 178176 bytes decompressed, heat patch 359 fields, H 0..6 -> build/q920.heat.json
--- selftests: 35 gates caught every break of their own input (232 s)

$ cd /home/m4rck/code/worktrees/pso-roguelike/beloningen-b && NEWSERV_DIR=/home/m4rck/code/worktrees/newserv/beloningen-b python3 quests/spire/make_spire_rewards.py --check
OK: /home/m4rck/code/worktrees/newserv/beloningen-b/system/spire-rewards.json matches the design grid
grid_rc=0

$ python3 data/check_pool_order.py        (na B5)
pool order: 4 file(s), 0 FAIL

$ NEWSERV_DIR="$D" bash build.sh --c4 server-all        (het q929-labartefact, na B5)
rc=0
--- build-c4-server-all: .bin + .dat 238794 of 245000 bytes decompressed (.bin 164008)
a7e4041b7db275cba3b58ff303a6c4bf  build-c4-server-all/q929-gc-e.bin
```

In de quest-repo toont `git status --short` alleen de drie ongetrackte mappen van vóór deze ronde (`build-ep2-lab/`,
`build-ep2/`, `build-ladder/`). Fixes: newserv 3c6f250 (`fix(review): test the part B unit claims and say why a unit
stays`), quest fd81e2c3 (`fix(review): give the Mag food fallback frame four slots too`). Niets gepusht.

## Wat ik niet heb aangeraakt

- **De newserv-pin in `pc-build.sh` (regel 14)** staat nog op 8e77957 (deel A). `pc-build.sh` weigert daarmee via
  `make_spire_rewards.py --check` (vóór deze ronde gemeten: rc=1). Niet verzet: de pin hoort op de newserv-commit
  ná de review-loop, en een pin op een ongepushte commit (3c6f250) kan de pc-build niet halen. Dit is de stap na de
  loop.
- ATA-units (buiten scope; de bonus telt ATA bewust niet, en dat maakt regel B's mag alleen iets ruimer).
- De rung-bestanden Hard, Very Hard en Ultimate: daar zijn H12, H28, R12, R29 en de Frame DEF+6-regels nog 0 slots
  (buiten scope; niet nagemeten of ze gegenereerd worden).
- Bij r143 = 0 blijft het kit-frame een frame zonder slots (`equip_item` draagt geen slotbyte); dat staat al in de
  observaties van de bouw. B4 zorgt dat de speler dan leest waarom een unit in het pak blijft.
- Het volgersgedrag (een tweede speler) bij een Unit-pick: niet apart getoetst.
- Geen uitrol, geen herstart, niets op de pc of in het lab, geen geheugenkaart, niets gepusht.

## Stopadvies

**Stoppen mag na deze ronde: een volgende ronde voegt voor dit doel weinig toe.** Elke claim van het doel die in de
harness te spelen is, heeft nu een mutant die rood wordt (M1-M5, M7-M12, Q2, Q3). De twee gedragsfouten zijn
gefixt: B4 (geen regel bij r143 = 0) en B5 (een frame zonder slots uit de pool). Wat overblijft is B7: het kit-frame
dat de server bij 'C' 13 aantrekt, en de slotbyte die de quest daarvoor maakt. Dat bewijst alleen de Wii. De volgorde
is daarom:

1. de newserv-pin in `pc-build.sh` naar de commit na de loop (na de push naar `m4rone`);
2. pc-build en uitrol volgens de vaste route;
3. Marcks eerste kamer laat zien of het kit-frame 4 slots heeft en de eerste Unit in slot 1 staat.

Een volgende ronde heeft alleen zin als hij B7 oppakt: een SpireEquipItem-dubbel in de harness, of een poort op
`kit_meta_frame` in de bouwsessie.
