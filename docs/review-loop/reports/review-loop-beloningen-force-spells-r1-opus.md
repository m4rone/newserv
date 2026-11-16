# Review-loop beloningen-force-spells, ronde 1, opus (invalshoek: doel)

## Maatstaf en scope

Maatstaf: het doel in `docs/review-loop/goals/beloningen-force-spells.md`, punt voor punt, in de productietoestand.
Gelezen: `git diff be9befa HEAD` in newserv (`src/SpireRewards.hh/.cc`, `src/SpireFlagBridge.cc`, `src/SpireDraw.hh/.cc`,
`src/StaticTests.cc`) en `git diff 1e7fb649 HEAD` in de quest-repo (`quests/spire/data/rewards.json`,
`quests/spire/rewards.py`, `quests/spire/check_ux_pass.py`, ook `quests/spire/make_spire.py` mag_entry_reward, dat
mee veranderde), plus de code eromheen: `spire_reward_disk_level`, `spire_reward_offer`, `spire_reward_make_offer`,
`spire_reward_draw_offers`, `spire_reward_pick` met `own_item`, de draw-tabel-lezer, en in de quest `pick_dialog`,
`give_<id>`, `curse_extra`, `apply_curse`, `exam_reward`, `rr_retry` en `exam_rares_<a>`. Eerdere review-verslagen
niet gelezen.

## Per punt van het doel: gehaald of niet, met bewijs

1. **De 18 regels geven een spell die je kunt leren.** Gehaald.
   - Precies de 18 regels F01-F15 en F35-F37 veranderen, en alleen in `naam`, `srv` en `tekst`; volgorde en bytes
     van elke regel zijn gelijk (vergelijking oud tegen nieuw `rewards.json`):
     `FO 36 entries, order same; bytes same: True ; changed keys: [('naam', 'srv', 'tekst')] in ['F01', ..., 'F15', 'F35', 'F36', 'F37']`;
     HU en RA: `changed keys: []`.
   - In de gebouwde `spire-draw.json` hebben ze `srv 4` en hun eigen bytes (bv. `3 F03 Foie True True 4 03 02 07 00 ...`).
   - De server leest techniek en level van de regel uit die bytes (`SpireFlagBridge.cc` make_offer, `own_bytes[4]`
     en `own_bytes[2] + 1`), houdt die techniek aan op `max(Sega's level voor kamer en trede, level van de regel)`
     (`SpireRewards.cc` `level_of` en het `line_tech`-blok), minstens twee boven wat de kopie kent en één aan de top
     van de klasse (`spire_reward_disk_level`, `floor`), en gaat bij te weinig MST omlaag tot `known_level + 1`
     (de lus `for (; level > known_level; level--)`).
   - Tests: het unit-blok "a line that names a disk of its own" (Lv3 boven Sega's Lv1, Sega's Rare-level als dat
     hoger is, +2 boven Lv5, MST-grens naar Lv6) en het e2e-blok `lowest_level(5, 0)` en `lowest_level(6 | 3 << 6, 3)`.
2. **Kan de kopie niet meer groeien, dan een andere spell; niets te geven, dan de oude schijf.** Gehaald, op één
   punt na (B1).
   - Andere techniek van de trede als het eigen blok niets gaf: `if (can.empty())` met `level_of(t, 0)`. Unit-test
     "the copy knows the class's highest Foie: the Foie line gives another technique of the tier".
   - Niets te geven: `spire_reward_offer` gooit, `make_offer` geeft `nullopt`, en de keuze antwoordt met de eigen
     bytes (`own_item("no offer (...)")`, en in `draw_offers` "the entry's own item").
   - Wel ging hier de vloek verloren: zie B1.
3. **De naam belooft geen level meer.** Gehaald: de namen zijn "Foie", "Barta", ... (`rewards.json`), het label van
   het aanbod is `tier_label + " " + "Foie Lv9"` (`spire_reward_offer`). De pagina zegt "Level: see the reward line".
4. **F03 en F11 zijn zeldzaam; nooit onder Rare; de vloek betaalt twee levels; bits 6 en 7; geen "(rare)".** Gehaald,
   ook hier op B1 na.
   - Trede: `if (entry.rare && (tier < SpireRewardTier::RARE)) tier = RARE` in make_offer.
   - Levels: `disk->extra_levels = entry.rare ? spire_reward_curse_disk_levels(curse) : 0` (1 en 3 geven 2, 2 geeft
     0), opgeteld binnen `max_level` vóór de MST-lus.
   - De quest stuurt `T_DIR * 64` op een zeldzame serverregel (`rewards.py` give, `let r70, T_DIR; muli r70, 64;
     addr T_TMP, r70`). `mag_entry_reward` houdt bits 0-7 (`andi T_TMP, 0xFF`) en zet het vak in bits 8-15. De
     server leest `value & 0x3F` en `(value >> 6) & 3`. Een aanbod van de 'Q' neemt de vloek uit zijn eigen trekking
     (`spire_draw_slot_curse(drawn, slot)`, dezelfde bits 24-31 die `pick_dialog` leest).
   - E2e: Cursed Chest en Glass +2, Jellen Mark 0, gewone regel 0.
   - Beloningsregel: `tiered` laat "(rare)" weg achter een aanbod van de server; de vloek blijft genoemd (tests "RARE
     Foie Lv9" en "RARE Foie Lv11$C7 (cursed: Glass)").
5. **Het examen geeft geen serverbeloning; de plaatsen blijven; op de twee plaatsen de Foie Merge.** Gehaald.
   - `exam_rares` op de echte pools: `FO srv ['F03', 'F11', 'F21', 'F22', 'F23'] -> ['F21', 'F21', 'F21', 'F22', 'F23']`;
     HU en RA ongewijzigd (`['H11'] -> ['H11']`, `['R09'] -> ['R09']`).
   - `exam_<a>` kiest willekeurig uit de drie Merges en schrijft het volgnummer in de hele lijst (`rares.index(i)`).
     `rr_<a>` springt per volgnummer naar wat `given` zegt, en 'C' 12 pakt dezelfde `given`.
   - De poort c12_wait leest de lege plaatsen via de sprongtabel van `rr_<a>`. Zijn breuk-constante `0x155134D3` is
     `19 | 19<<6 | 19<<12 | 20<<18 | 21<<24` (nagerekend: `0x155134d3`).
6. **Vaste grenzen.**
   - Volgorde en bytes ongewijzigd (punt 1).
   - Eén item per keuze: de serverregel loopt alleen via `mag_reward`. Een volle rugzak geeft `RES = FFFFFFFF` en
     een return vóór de tellers.
   - HU en RA ongewijzigd (pools en examentabel gelijk).
   - Geen wacht in een kamerthread: het examen geeft nooit een serverregel.

## Bevindingen

### B1 — Valt de server terug op de eigen schijf van een zeldzame regel, dan betaalt de vloek niets meer (minor, raakt het doel)

- **Bewijs:** vóór de wijziging gaf de quest F03 en F11 zelf, met `curse_extra`. Cursed Chest en Glass gaven dan +2
  levels (`rewards.py` `cx_r12d` en de Glass-tak: `addi r62, 2`).
- Nu is de regel een serverbeloning en loopt `give` niet meer langs `curse_extra`. Heeft de server geen aanbod
  (geen tabel, geen kopie, niets leerbaar, of regel B weigert), dan antwoordde `own_item` in `spire_reward_pick` met
  de kale eigen bytes. De quest rekent daarna wel de vloek af (`pd_taken_r` -> `apply_curse`: Cursed Chest kost een
  leven). Zo kost Cursed Chest in dat pad een leven voor niets, het geval dat B-E02-52 juist wil voorkomen. De bits
  6 en 7 kwamen wel binnen (`pick_curse`), maar `own_item` las ze niet.
- **Fix (newserv `91b062d`):** `own_item` telt op een zeldzame regel waarvan de eigen bytes een techniekschijf zijn
  (`03 02`) `spire_reward_curse_disk_levels(pick_curse)` bij het level op. Dat gebeurt binnen het hoogste level van
  de klasse (`get_max_tech_level`, hoogstens Lv30) en nooit onder het eigen level. Een gewone regel en Jellen Mark
  krijgen niets.
- **Test:** het e2e-blok "part B: a Disk reward through the bridge" krijgt een stap waarin de kopie van elke techniek
  het hoogste kent. `own_disk(5)` geeft dan `03 02 02`, met Cursed Chest of Glass `03 02 04`, met Jellen Mark
  `03 02 02`. De gewone regel 6 met vloekbits geeft `03 02 02 00 03`. Daarna worden de technieken teruggezet. De
  serverlog van de run toont het pad: `no offer (no technique disk for FOmarl at room index 1 RARE: ...); answered
  the entry's own item 030204000000000000000000`.

Verder geen bevindingen.

Gezien, geen bevinding (de wijziging maakt niets stuk en het doel vraagt het niet):
- `srv_foie` onthoudt ook een Foie die de regel "Disk" (F39) gaf, voor Sea of Fire. Strikt genomen verandert dat iets
  aan een regel die geen eigen schijf noemt, maar het is de bedoelde lijn uit B-E02-51 en maakt Sea of Fire alleen
  beter.
- Geeft de server een andere techniek (de kopie kent de eigen al op zijn hoogst), dan telt de regel toch zijn eigen
  tag (een "vuur"-keuze die Barta gaf), voor R4 Thread en de trekking. Klein, en niet in het doel.

## Mutanten (alleen als je er draaide, letterlijk)

Eén mutant, op mijn eigen fix, om te tonen dat de nieuwe test hem vangt:

```
-      uint8_t extra = entry->rare ? spire_reward_curse_disk_levels(pick_curse) : 0;
+      uint8_t extra = 0 && entry->rare ? spire_reward_curse_disk_levels(pick_curse) : 0;
```

Uitvoer van `build/newserv run-static-tests` (exit 1):

```
E ... GUARD E2E (part B: a Disk reward through the bridge; rule B's mag carries its MST, also past a reward weapon) FAILED: failure at /home/m4rck/code/worktrees/newserv/beloningen-b/src/StaticTests.cc:11672: !((own_disk(5 | (1u << 6)) == SpireMagBytes{0x03, 0x02, 0x04, 0x00, 0x00}))
E ... run-static-tests: failure at /home/m4rck/code/worktrees/newserv/beloningen-b/src/StaticTests.cc:13297: 0u != e2e_failed
```

Hersteld met `git checkout -- src/SpireFlagBridge.cc`; `git status --short` daarna leeg.

## Verificatie na de fixes (letterlijk)

Laptop aan de lader: `cat /sys/class/power_supply/A*/online` gaf `1`. Newserv op `91b062d`, quest-repo op `f4a716c5`.

Server (de mutant was net hersteld, dus `SpireFlagBridge.cc` werd opnieuw gecompileerd):

```
[  3%] Building CXX object CMakeFiles/newserv.dir/src/Revision.cc.o
[  3%] Building CXX object CMakeFiles/newserv.dir/src/SpireFlagBridge.cc.o
[  4%] Linking CXX executable newserv
[100%] Built target newserv
make: Leaving directory '/home/m4rck/code/worktrees/newserv/beloningen-b/build'
I 1215438 2026-11-16 20:34:48 - [C-BB] Deleted
I 1215438 2026-11-16 20:34:48 - [Game:7A03] Deleted
I 1215438 2026-11-16 20:34:48 - -- All static tests passed
ok: partner floor 1, leader floor 2 -> skip
ok: partner floor 15, leader floor 14 -> warp
ok: spire-lives-check
```

Bij de eerste bouw van de fix werden `SpireFlagBridge.cc.o` (20:28:17) en `StaticTests.cc.o` (20:28:42) allebei
opnieuw gebouwd.

Quest-build (één keer, exit 0, staart van de uitvoer):

```
--- q920 labels used: engine 239 of 239, rewards 768 of 768, economy 486 of 512, score 90 of 256, coop 256 of 256, path 256 of 256, e08 717 of 768, switch 45 of 256, overflow 583 of 768
--- build: .bin + .dat 241202 of 245000 bytes decompressed (.bin 166416)
--- q920: .bin 166416 of 178176 bytes decompressed, heat patch 359 fields, H 0..6 -> build/q920.heat.json
--- selftests: 36 ran green, every gate caught every break of its own input (13 s)
```

`git status --short` in de quest-repo daarna leeg.

Grid:

```
OK: /home/m4rck/code/worktrees/newserv/beloningen-b/system/spire-rewards.json matches the design grid
```

## Wat ik niet heb aangeraakt

- De quest-repo: geen fix nodig, geen commit.
- Wat buiten scope staat: mana, units, spells voor hunters en rangers, androids, Sea of Fire, de examen-extra, de
  kamerschijf bij een Merge, de winkel, Hard, Very Hard en Ultimate, de volger, regel B die weigert.
- Uitrol: niets op de pc of in het lab. Eén opmerking voor de uitrol: een nieuwe quest naast een oude newserv leest
  het regelnummer als `value & 0xFF`. Een vervloekte keuze van F03 of F11 vindt dan geen regel, krijgt geen item en
  telt toch als genomen. `apply-on-pc.sh` zet de quest en de newserv van de MANIFEST samen neer, dus langs die
  route treedt dit niet op. Een quest los kopiëren naast een oudere newserv moet niet.
- Geen pc-build en geen build van alle kamerquests. De bijgehouden kamerbestanden onder `quests/spire/build-rooms/`
  dragen nog de oude namen tot die build draait.

## Stopadvies

Stoppen kan na deze ronde. Alle vijf punten van het doel zijn gehaald met bewijs in code, tests en de questbouw. De
enige bevinding (B1, minor, in het terugvalpad) is gefixt, met een test die zijn mutant vangt. De fix is klein en
raakt alleen het pad waarin de server geen aanbod heeft. Een volgende ronde zou vooral die fix opnieuw lezen.
