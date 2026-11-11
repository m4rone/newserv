# Audit 1, run 3 in newserv: rapport

Tak spike/audit-1-run3, vanaf f612d6d. Commits 04b88fe (run 3) en fee5a8b (de winkel voor Run III). Patch voor
SpireFlagBridge: `pso-roguelike/audit-1/new-features/roguelike/output/audit-1-a1-spireflagbridge.patch` (207
regels, 93 regels toegevoegd of weggehaald).

## Verdict

**Af.** Op één punt wijk ik af van de opdracht: :6973 houdt hetzelfde gedrag (zie Afwijkingen).

## Wat er veranderd is (regels in 04b88fe)

1. **SpireMeta.hh/.cc**
   - Ladder: `spire_meta_ladder_run` geeft 3 voor 913-916 (.cc:320-322). `spire_meta_ladder_quest_rung` geeft
     914→1, 915→2 en 916→3 (.cc:330-332).
   - Bereikregel: 917 en 918 tellen als kamerquest, dus 1917-3918 per rung (.cc:335-339).
   - Vlag: `SPIRE_META_FLAG_RUN3_WON = 0x038D` (.hh:35-37), en `spire_meta_ladder_flag(3)` (.cc:346-349).
     - 0x38D is vrij. In quests/ geen gset/gget/flag-gebruik: de enige treffers zijn een label `label038D` en een
       `setevt 0x38D` (event 909).
     - In src/ is 0x038D alleen een objecttype (Map.cc:2582, ReceiveSubcommands.cc:2924), geen questvlag.
   - De naam "III" (.cc:470-473), het menu over drie runs (.cc:417-437) en de winkellus `run <= 3` (.cc:770).
   - BaseRung "3" wordt ook ingelezen (.cc:85). Zonder die regel was een gekochte rung voor Run III na het laden weg.
   - Run III neemt de rung over van een rung-item dat vóór Run III gekocht is (.cc:371-380).
   - Nieuw: `spire_meta_run_final_room` (15/30/32), `spire_meta_quest_is_run3` en `spire_meta_quest_final_room`
     (.cc:823-836). `spire_meta_final_boss_room` blijft bestaan.
   - "Spire Run III" bij k30 als checkpoint_new waar is (.cc:864-866).
2. **DataIndex.cc**: K 1-32 in SpireCheckpointRooms (:1320), SpireCheckpointArrivals (:1337) en SpireRoomQuests
   (:1352). De drie foutmeldingen zeggen nu "K 1-32".
3. **ExpressionEnvironment.hh**: het veld `spire_run3` (:21) en de lookup `SPIRE_RUN3` (:38-39).
   - Client.cc:395 vult `spire_run3`.
   - ReceiveCommands.cc:3308: de menuregel krijgt de rung van Run III.
   - ReceiveCommands.cc:4625: de moeilijkheidspoort neemt de hoogste rung van de drie runs.
4. **StaticTests.cc**
   - Tien nieuwe blokken onder "-- Spire Run III" (:2741-2944).
   - Twee bestaande verwachtingen zijn aangepast aan het gevraagde gedrag:
     - De Olga Flow-tekst noemt nu "Spire Run III" (:1681).
     - De oproepen van `spire_meta_ladder_menu_note` hebben nu vier argumenten. De verwachte zinnen zijn gelijk
       gebleven.

## Ronde 2: de winkel en de statusregel

- **fee5a8b, de winkel (SpireMeta).**
  - Nieuwe vormen van `spire_shop_buy_refusal` en `spire_shop_next_price` met `std::array<uint8_t, 3>`.
  - Een rung-item is ALREADY_OPEN pas als alle drie de runs de rung open hebben.
  - RUNG_CLOSED geldt als geen van de drie runs de rung eronder open heeft.
  - De vorm met twee runs (`uint8_t[2]`) blijft en beslist als voorheen.
  - De test vergelijkt beide vormen voor elk item, zes upgrade-sets, alle rungparen, r3 tussen r1 en r2, kit2 en drie
    saldo's. De uitkomsten zijn steeds gelijk.
- **In de patch.**
  - SpireFlagBridge.cc:3257: `open_rung` wordt een array van drie runs, ook na een aankoop. De logregel toont
    "rungs open a/b/c".
  - De statusregel staat nu in de pure functie `spire_checkpoint_status_text` (in de .hh gedeclareerd).
    `spire_checkpoint_command` roept die aan.
  - Een test daarvoor: k1, k31 en k32, alleen k32, alle 32, en een Run I-voorbeeld.
- **Rood.**
  - De nieuwe vorm met de beslissing van twee runs: `RED shop block: line 2894: !(R::NONE ==
    spire_shop_buy_refusal(cat, 4, {}, 9999, hard_12))`.
  - De statuslus met grens 31: exit=1 op `StaticTests.cc:1706: std::string("Rooms cleared: 1 31 32 - last: room
    32") != spire_checkpoint_status_text(...)`.
- **Groen.**
  - Zonder patch (fee5a8b): exit=0, `-- All static tests passed`.
  - Met de complete patch: exit=0, `-- All static tests passed`.
  - De patch is getoetst gelijk aan de geteste boom (`cmp`). Daarna is alles teruggezet met `git checkout`.
- **Apply-check.**
  - Op spike/audit-1-run3: exit 0.
  - Op m4rone/spike/veilig-1-ship: de patch alleen exit 0, en de stapel f612d6d + 04b88fe + fee5a8b + patch ook exit 0.
- **Niet getest.** De aanroep in de winkelhandler zelf (SpireFlagBridge.cc:3257-3335) is doorgeefwerk zonder eigen
  test; de beslissing zit in SpireMeta en is wel getest.

## Patch voor SpireFlagBridge (niet gecommit)

- **:2476 `spire_gift_last_clear`**: nieuwe parameter `quest_number` (standaard 0). De laatste kamer is nu
  `spire_meta_quest_final_room`. De aanroep op :3852 geeft `quest_number` mee.
- **:3034**: de laatste kamer van de run, niet van de episode. Een gewonnen Run III legt k32 vast in BossesBeaten.
- **:6973 `spire_finish_boss_store_room`**: drukt de kamer nu per run uit, met het gedrag van vóór de patch:
  - Run I geeft k15.
  - Run II geeft 0, zoals nu.
  - Run III geeft 0. De aanroeper op :3031 slaat de kamer op als baaskamer (`boss = true`, uitbetaling PerBoss en
    de baastelling), en Epsilon is geen baas (EnemyType.cc:69).
- **:7367**: de status telt tot en met k32.
- **SpireFlagBridge.hh**: de twee declaraties krijgen `quest_number = 0`. Daardoor compileren de bestaande tests
  ongewijzigd.
- **StaticTests.cc**: twee testhunks, voor de laatste kamer per run en voor de opslag bij een gewonnen 'F'.

## Testuitvoer

- **Groen zonder patch** (04b88fe): `./build/newserv run-static-tests`, exit=0. Laatste regel:
  `-- All static tests passed`.
- **Groen met patch**: exit=0, laatste regel `-- All static tests passed`. Daarna is de patch teruggezet met
  `git checkout -- src/SpireFlagBridge.cc` (ook de .hh en de testhunks).
- **Rood zonder de wijziging.**
  - Opzet: de bronnen van f612d6d, aangevuld met stubs die de nieuwe functies het oude gedrag geven. Elk nieuw blok
    zat tijdelijk in een try/catch, zodat één run alle blokken toont. Niets hiervan is gecommit.
  - Run 1:
    ```
    RED existing Olga Flow text: line 1683: ... "Spire Run III, Start level 38 ..." != spire_meta_boss_unlock_text(&repo, 30, true)
    RED block 1: line 2748: 3 != spire_meta_ladder_run(q)
    RED block 2: line 2760: 1 != spire_meta_ladder_quest_rung(914)
    RED block 3: line 2779: SPIRE_META_FLAG_RUN3_WON != spire_meta_ladder_flag(3)
    RED block 4: map::at
    RED block 5: line 2812: "Spire Run III and rooms on Hard ..." != spire_meta_ladder_menu_note(3, 3, 0, 1)
    RED block 6: Invalid environment lookup in expression
    RED block 7: line 2848: 2 != spire_meta_base_rung(loaded, 3)
    RED block 8: line 2886: 32 != spire_meta_run_final_room(3)
    RED block 9: line 2910: "Olga Flow beaten! Unlocked: Spire Run III." != spire_meta_boss_unlock_text(nullptr, 30, true)
    RED block 10: SpireCheckpointRooms: Episode 1, 2 or 4, Floor 0-255, Room -1-65535, K 1-31
    ```
  - Runs 2 tot en met 6 repareren telkens één deel, zodat ook het volgende deel van een blok rood te zien is:
    ```
    block 7: map::at (bought.base_rung.at(3), shop loop)
    block 7: line 2870: 2 != spire_meta_base_rung(before, 3) (derivation)
    block 2: line 2766: d != spire_meta_ladder_quest_rung(d * 1000 + 917)
    block 8: line 2890: !(spire_meta_quest_is_run3(q))
    block 10: SpireCheckpointArrivals: ... K 1-31
    block 10: SpireRoomQuests: ... K 1-31, Variant 0-2
    ```
  - Patchtest: de body van `spire_gift_last_clear` uit f612d6d geeft exit=1:
    `K2 (258: ...) FAILED: ... StaticTests.cc:7220: !(spire_gift_last_clear(2, k, q) == (k == 32))`.
  - Wat niet rood kan zijn:
    - De test van `spire_finish_boss_store_room` met quest_number legt gedrag vast dat niet verandert. Zonder de
      patch compileert hij niet.
    - De statusregel (:7367) heeft geen static test, want hij vraagt een client met een karakterbestand.

## Past de patch op m4rone/spike/veilig-1-ship (771d844)?

- `git apply --cached --check` op een index van die tak: de patch zelf slaagt (exit 0, offsets 77-677).
- 04b88fe alleen past daar niet (SpireMeta.cc:324): hij steunt op f612d6d, dat niet op veilig-1-ship staat.
- De stapel f612d6d + 04b88fe + patch past wel (alle drie exit 0).
- Zonder 04b88fe compileert de patch niet, want hij gebruikt `spire_meta_quest_final_room`.

## Beslis-log (nog over te nemen in decisions.md; dat bestand is van het audit-spoor)

- **B-AUDIT-1-R3-1, vlag 0x38D voor Run III.**
  - Datum: 2026-11-11. Route: zelf. Type: data. Klasse: omkeerbaar.
  - Dekking: opdracht ("eigen ladder-vlag naast RUN2_WON, meet dat het nummer vrij is"). Gemeten: geen gebruik in
    quests/ of src/.
  - Beslissing: `SPIRE_META_FLAG_RUN3_WON = 0x038D`.
  - Waarom: het eerstvolgende vrije nummer binnen de vlaggen van de server, onder de relics.
- **B-AUDIT-1-R3-2, laatste kamer per quest.**
  - Datum: 2026-11-11. Route: zelf. Type: techniek. Klasse: omkeerbaar.
  - Dekking: opdracht ("Run II moet precies blijven doen wat hij nu doet").
  - Beslissing: `spire_meta_quest_final_room(q, ep)` geeft 32 voor een Run III-quest (913-918, ook op rungs).
    Voor alle andere quests geeft hij `spire_meta_final_boss_room(ep)`. Een test loopt alle q 0-4999 en ep 0-4 na.
  - Waarom: zo is Run I en II per constructie gelijk.
- **B-AUDIT-1-R3-3, :6973 houdt zijn uitkomst.**
  - Datum: 2026-11-11. Route: zelf. Type: gedrag. Klasse: omkeerbaar.
  - Dekking: audit-1-a1-grote-bestanden.md:40-42 ("geen exam en geen uitbetaling per baas"), EnemyType.cc:69 en
    SpireFlagBridge.cc:3031 (`boss = true`).
  - Beslissing: alleen Run I slaat bij een gewonnen 'F' zijn laatste kamer op. Run III slaat k32 daar niet op.
  - Waarom: anders betaalt k32 PerBoss en telt hij als baas.
- **B-AUDIT-1-R3-4, BaseRung van Run III.**
  - Datum: 2026-11-11. Route: zelf. Type: gedrag. Klasse: omkeerbaar.
  - Dekking: opdracht (winkellus `run <= 2`). SpireMeta.hh: de rung-items openen de rung voor elke run
    (`spire_shop_apply_buy`).
  - Beslissing:
    - BaseRung "3" wordt ingelezen.
    - De rung van Run III is minstens die van elk rung-item dat het karakter bezit.
    - Run I en II lezen alleen hun eigen BaseRung, zoals nu.
  - Waarom:
    - Zonder het inlezen is een gekochte rung voor Run III weg na het laden.
    - Wie open_hard kocht vóór Run III bestond, heeft geen BaseRung "3". Zijn karakter kan hem ook niet opnieuw
      kopen, want de winkel kijkt alleen naar Run I en II.
- **B-AUDIT-1-R3-5, menuregel over drie runs.**
  - Datum: 2026-11-11. Route: zelf. Type: tekst. Klasse: omkeerbaar.
  - Dekking: CLAUDE.md § Standing mandate (wording is aan de regie en de sporen).
  - Beslissing: "Spire Run" als alle drie dicht zijn, anders de dichte runs ("Spire Run II, III").
  - Waarom: dezelfde vorm als nu; de drie bestaande zinnen blijven gelijk.
- **B-AUDIT-1-R3-6, de patch raakt ook SpireFlagBridge.hh.**
  - Datum: 2026-11-11. Route: zelf. Type: techniek. Klasse: omkeerbaar.
  - Dekking: opdracht (":2476 en :6973 de nieuwe functie laten gebruiken"). Daarvoor is het questnummer nodig.
  - Beslissing: een parameter `quest_number = 0` in twee declaraties, in de patch en niet gecommit.
  - Waarom: de bestaande tests en aanroepen blijven geldig.

## Gezien maar niet gebouwd (voor observaties.md)

- **Winkel.** Opgelost in ronde 2: fee5a8b en de patch.
- **Tekst.** "Spire Run III" verschijnt alleen als k30 in dezelfde game nieuw in het checkpoint komt.
  - Een gewonnen 'F' van Run II slaat k30 niet zelf op, anders dan k15 bij Run I (critic B-6).
  - Komt de vlag van Olga Flow niet vóór het baasrecord, dan ontbreekt de regel. De ontgrendeling zelf werkt wel.
  - Ik heb dit niet veranderd, omdat Run II gelijk moet blijven.
  - Waar: SpireFlagBridge.cc:3030 en :6973.
- **Statusregel.** Opgelost in ronde 2: `spire_checkpoint_status_text` met een test, in de patch.

## Niet gemeten

- Niets gedraaid op de pc, op de Wii of in het lab.
- De patch is op veilig-1-ship alleen tekstueel getoetst (`git apply --check`), niet gebouwd.
- De quest-kant (de AvailableIf van q913-q916 met SPIRE_K30 en SPIRE_RUN3, en de config met K 31-32) bestaat nog
  niet. De server is dus alleen met static tests getoetst.
