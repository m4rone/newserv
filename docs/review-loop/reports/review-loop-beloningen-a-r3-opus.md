# Review-loop beloningen deel A, ronde 3: opus (invalshoek doel)

Reviewer: Opus 5.5, koud: ik heb deze wijziging niet gebouwd. Ik heb de eerdere review-rapporten niet gelezen voordat
mijn oordeel vaststond.

## Maatstaf en scope

De maatstaf is deel A van `new-features/roguelike/design/beloningen-per-klasse.md` (quest-repo). Ik heb elk doelpunt
in de productiestand getoetst:

1. **Drie soorten beloning.** Wapen volgt de lijn van het wapen dat je vasthoudt. Nieuwe lijn geeft een andere lijn die
   je klasse mag dragen. Mag-voer is de derde soort.
2. **W1-W6 per kamer**, met de tiers Normal, Rare, Epic en Legendary en de kansen van KV §4. Legendary kan pas na de
   eerste De Rol Le.
3. **Elke beloning is draagbaar zodra je hem krijgt**, via mag-regel B. De mag groeit, komt nooit boven 200 en zakt
   nooit.
4. **Eén config-sleutel voor het ATA-model**, die SpireDrops ook leest.
5. **Geen itemverlies.** De guard zet de inventaris en de mag precies terug.

Gereviewd:

- newserv `git diff 1e0cb5a HEAD` op `feature/beloningen` (bad1729 … eefdd1b, 16 bestanden, +1768/-86);
- de quest-repo `git diff 7677a86c HEAD` (69c38050 … 6253875c).

Ik heb de diff en de geraakte bestanden volledig gelezen. Elke dragende claim kreeg een mutant. Elke mutant heb ik
teruggezet met `git checkout --`.

Buiten scope:

- deel B;
- uitrol naar de pc of het lab, en herstarts;
- Episode 2 en moeilijkheidsgraden;
- de fixes van fase 0.

## Wat er goed staat (getoetst, niet aangenomen)

Elke regel hieronder is getoetst met een mutant die rood werd (letterlijk onder "Mutanten"), of met een eigen meting.

- **Het grid.** `make_spire_rewards.py --check` geeft "matches the design grid". Ik heb dat los nagemeten tegen de IPT
  (`/tmp/review-loop/ipt_check.py`, `grid_vs_ipt.py`):
  - alle 1680 cellen zijn draagbaar voor hun klasse;
  - de eisen in het grid zijn die van de IPT;
  - de lijn van elk cel-wapen leidt terug naar zijn eigen lijn;
  - de hoogste needsom is 195 (onder 200);
  - de bosskamers hebben geen beloningslijst.
- **De lijn volgen.** M1 maakt van `from_line` een lege lijn en wordt rood. M2 laat Nieuwe lijn dezelfde lijn geven en
  wordt rood.
- **W per kamer.** M18 zet W vast en wordt rood. M29 haalt de Episode 2-index weg en wordt rood. Hit op de cel (M19) en
  mag-voer per tier (M27) worden allebei rood.
- **De KV §4-kansen.** De cap (M28) wordt rood. De trede-grens van Legendary (M3: Legendary al bij trede 1) wordt rood.
  De trede uit de bosskamers van de run (M4) wordt rood.
- **Regel B, de pure kant.** De boost in `spire_run_mag_decide` (G13) wordt rood. Het verhogen naar de need (G15) wordt
  rood. De need-deling van ATP (M6) wordt rood.
- **De mag-swap in de quest.** Een tweede `item_create2` in de reward-route (Q4) breekt de build op
  `check_mag_swap.py`.
- **Geen itemverlies.** De guard-e2e-tests na een run met beloningen staan groen. De nieuwe bridge-case (B2) laat zien
  dat er per pick hoogstens één item komt.

## Mutanten (letterlijk)

Runner: `/tmp/review-loop/run_mut2.sh` (newserv) en `/tmp/review-loop/run_qmut.sh` (quest-build met al zijn poorten).
"Voor" is de stand voor mijn fixes. "Na" is de stand op de fix-commits.

### newserv

| Mutant | Bestand: van → naar | Voor | Na (test die rood wordt) |
|---|---|---|---|
| M1_held_line_ignored | SpireRewards.cc `ret.from_line = spire_reward_line_of(table, cls, held);` → `(held && false) ? "x" : ""` | RED `StaticTests.cc:7721: line_name != offer.line` | niet opnieuw gedraaid (de rode test is ongewijzigd) |
| M2_newline_same | SpireRewards.cc `if (ln != ret.from_line) {` → `if (true) {` | RED `7778: !(offer.line != line_name)` | niet opnieuw gedraaid (de rode test is ongewijzigd) |
| M3_legendary_trede1 | `if (trede >= table.legendary_from_trede)` → `trede + 1 >=` | RED `7825` (Legendary op trede 1) | niet opnieuw gedraaid (de rode test is ongewijzigd) |
| M4_trede_run_rooms | `… \|\| run_boss_rooms.count(k)` → `… \|\| (run_boss_rooms.count(k) && false)` | RED `7808: 2u != spire_reward_trede(rewards, {4}, {8})` | niet opnieuw gedraaid (de rode test is ongewijzigd) |
| M6_need_atp_div | need ATP `/ 2` → `/ 3` | RED `7789: 0u != need_mismatches` | niet opnieuw gedraaid (de rode test is ongewijzigd) |
| M18_w_flat | `table.room_w.find(room_index)` → `find(1)` | RED `7699: 6 != spire_reward_w(rewards, 40)` | niet opnieuw gedraaid (de rode test is ongewijzigd) |
| M19_no_hit | `if (cell.hit) {` → `if (false) {` | RED `7733` | niet opnieuw gedraaid (de rode test is ongewijzigd) |
| M27_mag_food_tier | `mag_food[tier]` → `mag_food[0]` | RED `7795` | niet opnieuw gedraaid (de rode test is ongewijzigd) |
| M28_odds_cap | `e = std::min(e, table.cap - l);` → `e = e;` | RED `7818: 60u != elite.epic` | niet opnieuw gedraaid (de rode test is ongewijzigd) |
| M29_room_index_ep2 | `(k > 15) ? (k - 15) : …` → `std::max<uint32_t>(k, 1)` | RED `7697: 1u != spire_reward_room_index(16)` | niet opnieuw gedraaid (de rode test is ongewijzigd) |
| G13_decide_no_boost | SpireMag.cc `if (new_step \|\| ledger.boost_due) {` → `if (new_step) {` | RED `7854` | niet opnieuw gedraaid (de rode test is ongewijzigd) |
| G15_rule_b_no_need | `need.of(z) * 100u` → `* 0u` | RED `7765` | niet opnieuw gedraaid (de rode test is ongewijzigd) |
| M14_model_configured_hard | `cached = json.contains("AtaModel") ? … : SAFE;` → `cached = SpireAtaModel::SAFE;` | **GREEN** | RED `7766: !(spire_ata_model_configured() == model)` |
| M15_parse_model_hard | `ret.ata_model = spire_ata_model_for_name(…)` → `= SpireAtaModel::SAFE` | **GREEN** | RED `7765: !(spire_rewards_parse(json).ata_model == model)` |
| G10_drops_model_fixed | SpireDrops.cc `player.ata_model = spire_ata_model_configured();` → `= SpireAtaModel::TABLE` | **GREEN** | RED `15826: !(safe_changed)` |
| G11_drops_meet_table | `spire_ata_dex_needed(model, …)` → `(SpireAtaModel::TABLE, …)` | **GREEN** | RED `15826: !(safe_changed)` |
| G12_result_keeps_boost | SpireMag.cc `if (ledger.offered_boost) {` → `if (false) {` | **GREEN** | RED `7965: !(!led.boost_due && !led.offered_boost && (led.feed_levels == 0))` |
| G7_overview_names_ignored | SpireDraw.cc `slot_names.find(slot)` → `slot_names.end()` | **GREEN** | RED `8459` (de naam in slot 3) |
| G8_visible_size_ignored | `spire_draw_visible_size(line) <= MAX` → `line.size() <= MAX` | **GREEN** | RED `8469` (drie gekleurde namen passen) |
| G9_srv_not_parsed | `e.srv_kind = ((srv >= 0) && (srv <= 3)) ? srv : 0;` → `(srv > 99) ? srv : 0` | **GREEN** | RED `8486: 1 != e[0].srv_kind` |
| G1_result_no_boost | SpireFlagBridge.cc RESULT `led->boost_due = true;` → `false` | **GREEN** | RED `10732: !(!line_sent(sent_result, SPIRE_EQUIP_WEAPON_MISSING_TEXT))` |
| G1b_result_no_need | `led->need = p.need;` → `SpireMagNeed{}` | **GREEN** | RED `10738: !(spire_reward_meets(info, …, &boost, SAFE))` |
| G2_pick_no_follows | `spire_reward_answer(c, &offer->item, p.mag_due);` → `…, false);` | **GREEN** | RED `10708: … \| SPIRE_REWARD_MAG_FOLLOWS != picked` |
| G3_pick_no_need | `p.need = spire_reward_mag_need(…);` → `SpireMagNeed{}` | **GREEN** | RED `10708` |
| G4_equip_gate_no_wait | `led && led->boost_due` → `false` in de equip-gate | **GREEN** | RED `10732` |
| G5_trede_no_meta | `beaten = spire_meta_load_for_client(…).bosses_beaten;` → `std::set<uint8_t>{}` | **GREEN** | RED `10771: 1u != spire_static_test_reward_trede(p.c, r.l, SERIAL, rewards_e2e)` |
| G6_reward_line_unnamed | `spire_draw_overview_line(…, reward_names)` → `…, decltype(reward_names){})` | BUILD FAILED (mijn eerste vorm) | RED `10670: !(tiered)` |
| G14_savestate_need_dropped | to_json `if (led.need.pow \|\| …) {` → `if (false) {` | **GREEN** | RED `6337: !(back.run_mag->need == led.need)` |
| G18_food_not_owed | `feed_levels + offer->food_levels` → `+ 0 * offer->food_levels` | **GREEN** | RED `10756: !(food_rise)` |
| B1_meta_null_filename | SpireMeta.cc `std::string& name = filename ? *filename : own_filename;` → `= *filename;` | (de bug zelf) | RED, exit 139 (SIGSEGV na "draw … r181=00042100, r248=DONE") |
| B4_feed_not_clamped | `std::clamp<int64_t>(…"FeedLevels"…, 0, 200)` → `std::min<int64_t>(…, 200)` | (de bug zelf) | RED `6368: 0 != edited("FeedLevels", "-1").feed_levels` |
| B4_need_not_clamped | `std::clamp<int64_t>(need[0]->as_int(), 0, 200)` → `std::min<int64_t>(…, 200)` | (de bug zelf) | RED `6367: !(edited("Need", "[-1, 999, 7]").need == (SpireMagNeed{0, 200, 7}))` |
| B9_trede_no_run_bosses | `spire_reward_trede(table, beaten, run_bosses)` → `(…, beaten, {})` | n.v.t. (nieuw) | **GREEN**, zie B13 |

De regelnummers in "Voor" komen uit de testbestanden van vóór mijn tests. "Na" is gedraaid op `d3ca1e3`, en G5 en
B9 op `464e19d`.

### Quest-repo (quest-build met alle poorten)

| Mutant | make_spire.py: van → naar | Voor | Na |
|---|---|---|---|
| Q1_pb_no_mag_clear | `s.call(L['mag_clear'])` in pb → `pass` | **GREEN** | RED `RB2 reward: leader pause B: engine:room_k1_pauseB (line 19601) reaches its ack … without calling flow:mag_entry_clear` |
| Q2_mirror_no_mag_clear | `s.call(L['mag_clear'])` in mirror_pause_b_board → `pass` | **GREEN** | RED `RB3 reward: follower pause B: overflow:coop:mirror_pause_b_board (line 29558) reaches its ack …` |
| Q3_follows_not_pending | `s.leti(MAG_PENDING, 1)` → `pass` | **GREEN** | RED `RB1 reward: … "leti r135, 1" in it: 0` |
| Q4_second_create | tweede `item_create2` vóór de RESULT | RED `check_mag_swap.py: item_create2 outside flow:mag_create … (a second swap site)` | niet opnieuw gedraaid (de rode test is ongewijzigd) |
| Q5_no_phase9_check | `s.jmpi_eq(R.phase, 9, M['free'])` na `got` weg | **GREEN** | RED `RB5 reward: the create at e08:flow:mag_reward_got (line 16337) is reached … without a run-end test` |
| Q6_no_lock | `s.leti(MAG_SUB, MAG_REWARD)` → `pass` | **GREEN** | RED `RB4 reward: e08:flow:mag_entry_reward (line 16274) reaches e08:flow:mag_reward_send without taking the run-mag lock` |

De quest-kolom "Na" draaide op `check_mag_before_wave.py` zoals het in `8509144f` staat, net voor die commit. De
mutanten raakten alleen `make_spire.py`. Na elke mutant gaf `git status --short` alleen mijn eigen, nog niet
gecommitte bestanden terug. Na de commits is dat alleen nog dit rapport.

## Bevindingen

De fix-commits zijn newserv `464e19d` en quest-repo `8509144f`. `464e19d` is `d3ca1e3` met B9 en B12 erbij (amend,
niet gepusht).

### B1 (major, productiecode): newserv crasht bij de eerste trekking met een serverbeloning

- **Wat er mis was.** `spire_reward_make_offer` haalt de trede op met
  `spire_meta_load_for_client(c, serial_number, nullptr)`. Die functie schreef `*filename = …` en schreef dus via een
  null-pointer.
- **Gevolg.** Een SIGSEGV en geen exceptie, dus de `try/catch` eromheen helpt niet. Dat gebeurt bij elke 'Q' waarin
  een slot een serverbeloning-entry trekt. De echte trektabel van q920 heeft 9 zulke entries (`"srv": 1/2/3`, elk 3×
  in `quests/spire/build/spire-draw.json`). Met deze build was newserv dus bij de eerste beloningstrekking van Marcks
  run omgevallen, met iedereen in de game eruit.
- **Waarom geen test het zag.** Geen enkele test riep `spire_reward_make_offer` aan; zie B2.
- **Bewijs voor de fix.** De nieuwe bridge-case crashte op de eerste 'Q'. Uit `coredumpctl debug 132698`:
  ```
  #0  spire_meta_load_for_client(std::shared_ptr<Client>, unsigned int, std::__cxx11::basic_string<...>*) ()
  #1  spire_reward_make_offer(std::shared_ptr<Client>, std::shared_ptr<Lobby const>, unsigned int, SpireRewardTable const&, ...
  #2  spire_draw_answer(std::shared_ptr<Client>, std::shared_ptr<Lobby const>, unsigned int, unsigned int, unsigned int) ()
  #3  spire_bridge_handle_register(...)
  ```
- **Fix.** `src/SpireMeta.cc` accepteert nu `nullptr`, met een eigen string als doel. De header zegt dat ook.
- **Mutant.** `B1_meta_null_filename` zet de oude vorm terug (`std::string& name = *filename;`). Uitkomst: zie de
  tabel.

### B2 (major, tests): de bridge-kant van deel A stond alleen op papier

- **Wat groen bleef.** Deze mutanten in `SpireFlagBridge.cc` bleven groen:
  - RESULT zet geen boost meer (G1);
  - RESULT vergeet de need (G1b);
  - de pick meldt niet dat de mag volgt (G2);
  - de pick rekent geen need (G3);
  - de equip wacht niet op regel B's mag (G4);
  - mag-voer wordt niet geschuld (G18).
- **G6.** Daarnaast is er G6: de beloningsregel noemt de serveraanbiedingen niet. Mijn eerste vorm van die mutant
  bouwde niet. Een "voor"-meting heb ik dus niet, maar na de fix is hij rood.
- **Wat ontbrak.** Er was geen test die een beloning door `spire_bridge_handle_register` speelde.
- **Fix.** Een e2e-case: "review r3: a reward weapon and mag food through the bridge; rule B's mag meets the weapon
  before its equip". Die speelt achter elkaar:
  1. de kit-mag;
  2. een 'Q' met een eigen trektabel (drie wapen-entries `srv 1` en één mag-voer-entry `srv 3`);
  3. de beloningsregel, met één regel met tierwoorden en zonder de eigen entry-namen;
  4. de pick van een wapen, waarna r41 = TAG | ITEM | MAG_FOLLOWS en het wapen een POW-need heeft die de kit-mag niet
     haalt;
  5. de RESULT;
  6. 2,5 s geen equip;
  7. de ASK van pauze B, waarvan de mag het wapen haalt en geen lager level heeft;
  8. dan pas de equip;
  9. mag-voer: TAG | MAG_FOLLOWS, en de POW-stijging is precies één `MagFood`-waarde × 100;
  10. een ASK zonder nieuwe stap, die 0 geeft.
- **Test-setup.** `ServerWrote` bewaart nu ook elk verstuurd commando (`sent`), zodat de case de teksten kan lezen.

### B3 (major, tests): één ATA-sleutel voor grid en SpireDrops stond alleen op papier

- **Wat groen bleef.** M14 (`spire_ata_model_configured` altijd SAFE), M15 (`spire_rewards_parse` negeert
  `AtaModel`), G10 (SpireDrops altijd TABLE) en G11 (`spire_drops_stats_meet` rekent altijd met TABLE). De enige test
  vergeleek "safe" met "safe".
- **Fix, deel 1.** In het deel A-blok een test in een tijdelijke cwd. Die schrijft het grid met elke waarde van
  `AtaModel` en eist dat zowel `spire_rewards_parse` als `spire_ata_model_configured` die waarde geeft. Zonder sleutel
  of zonder bestand moet het SAFE zijn.
- **Fix, deel 2.** Een k3-case: "review r3: spire_drops_filter follows the AtaModel key of spire-rewards.json".
  - Een RAmar krijgt een drop van een ranged wapen dat onder TABLE draagbaar is en onder SAFE niet.
  - Met `"table"` blijft de drop staan.
  - Met `"safe"` wordt hij vervangen.

### B4 (major, productiecode): regel B's deel van de ledger had geen savestate-test, en een bewerkt bestand liep over

- **Wat groen bleef.** G14 laat `Need` weg uit de savestate en bleef groen. Na een hervatting is de need van het
  genomen wapen dan weg, dus de volgende mag hoeft het wapen niet meer te dragen.
- **Overloop.** `std::min<int64_t>(…, 200)` liet een negatieve `Need` of `FeedLevels` uit een bewerkt bestand
  doorlopen tot 65535.
- **Fix in de productiecode.** `std::clamp<int64_t>(…, 0, 200)` in `spire_savestate_from_json`, zoals `Lean` dat al
  doet.
- **Fix in de tests.** Een f6-case: "review r3 B3: rule B's part of the ledger (Need, BoostDue, FeedLevels) comes back
  from the savestate file". Die doet:
  - de rondreis van need {57, 31, 4}, de boost en 20 voer;
  - een decide op de hervatte ledger, die POW, DEX en MIND minstens op de need zet;
  - een kale ledger, die geen van de drie sleutels schrijft;
  - de randwaarden `[-1, 999, 7]` → {0, 200, 7}, en `FeedLevels` -1 → 0 en 70000 → 200.
- **Noot.** De testnaam zegt "B3". Dat was mijn werknummer van vóór de nummering in dit rapport.

### B5 (minor, tests): dat RESULT 1 de boost en het voer afrekent, was niet getest

- **Wat groen bleef.** G12: RESULT 1 laat `boost_due` staan.
- **Fix.** In het ledger-blok.
  - Na RESULT 0 blijven de boost en het voer geschuld, en geeft een ASK weer een mag.
  - Na RESULT 1 zijn `boost_due`, `offered_boost` en `feed_levels` leeg, blijft de need staan en geeft een ASK niets.

### B6 (minor, tests): de beloningsregel had geen test op namen, kleurcodes en `srv`

- **Wat groen bleef.** G7 (de namen van de slots genegeerd), G8 (breedte inclusief kleurcodes) en G9 (`srv` niet
  gelezen).
- **Fix.** Twee b8-cases:
  - de naam van een serveraanbieding in zijn slot;
  - de oude, naamloze tabel;
  - een slot zonder naam;
  - drie gekleurde namen van 20 tekens die precies op 90 passen en bij 21 de staart inkorten;
  - `spire_draw_visible_size("$C1ab$C7cd") == 4`;
  - `srv` en eigen bytes per entry, ook voor waarden die niet geldig zijn.

### B7 (major, quest-gate): de quest-kant van regel B had geen poort

- **Wat groen bleef.** Alle vijf deze quest-mutanten bouwden groen, met alle poorten:
  - Q1: pauze B van de leider vraagt de geschulde mag niet;
  - Q2: pauze B van de volger vraagt hem niet;
  - Q3: "mag volgt" zet geen `MAG_PENDING`;
  - Q5: na het antwoord geen test op het run-einde;
  - Q6: de pick neemt de run-mag-lock niet.
- **Gevolg van Q1 tot en met Q3.** Het wapen komt in de rugzak maar is niet draagbaar tot de volgende kamer, en de
  equip wacht mee.
- **Gevolg van Q5.** Een beloningsitem na 'O'.
- **Gevolg van Q6.** Twee run-mag-vragen delen r41 en r20-r31.
- **Fix.** `quests/spire/check_mag_before_wave.py` heeft nu de regels RB1-RB5 (docstring), elk met een eigen
  selftest-mutatie. Op `build/spire-gc-e.src.txt`:
  ```
  RB1 reward: an answer whose run mag follows marks it owed (r135 at e08:flow:mag_reward_follows (line 16363))
  RB2 reward: 15 leader pause B label(s) ask the owed mag (call flow:mag_entry_clear) before their ack
  RB3 reward: the follower's pause B handler overflow:coop:mirror_pause_b_board (line 29558) asks the owed mag before its ack
  RB4 reward: e08:flow:mag_entry_reward (line 16274) takes the run-mag lock before e08:flow:mag_reward_send
  RB5 reward: every way from e08:flow:mag_reward_got (line 16333) to its item_create2 tests the run end
  ; selftest 18 mutations caught
  ```

### B8 (minor, niet gefixt): mag-voer kan twee keer tellen na een verloren antwoord

- **Wat er gebeurt.** `spire_reward_pick` betaalt het voer bij de pick (`feed_levels +=`) en wist het slot
  (`by_slot.erase`). Stopt de quest met wachten (RS_POLLS 20 × RS_FRAMES 15 frames, ongeveer 5 s), dan blijft de
  beloning op het bord. Pakt de speler hem opnieuw, dan maakt de tak "an offer now" een nieuw voeraanbod en telt het
  voer twee keer.
- **Waarom het zelden gebeurt.** De bridge antwoordt in dezelfde `handle_register`. Het gebeurt dus alleen bij een
  verbinding die meer dan 5 s hapert.
- **Gevolg.** Alleen winst (de mag blijft onder 200), geen verlies.
- **Waarom ik het niet fix.** Een fix moet bij de pick weten in welke kamer je bent. Een kale "al betaald"-markering
  zou in een latere kamer waar de quest zelf trok een echt voer inslikken. Dat is erger. Voor de bouwagent.

### B9 (major, productiecode): Legendary na de eerste De Rol Le over runs heen stond alleen op papier

- **Wat groen bleef.** G5 (`beaten = {}` in plaats van de BossesBeaten uit het meta-bestand). De pure trede-functie is
  wel getest (M3 en M4 rood), maar het lezen van het meta-bestand niet.
- **Fix.** De trede-berekening van `spire_reward_make_offer` staat nu in `spire_reward_trede_for`, met
  `spire_static_test_reward_trede` voor de tests. De bridge-case schrijft BossesBeaten in het meta-bestand van het
  testkarakter en eist:
  - leeg → 0;
  - de Dragon → 1;
  - De Rol Le → 2.

### B10 (minor, ontwerp, niet gewijzigd): het ontwerp spreekt zichzelf tegen over de tier van Nieuwe lijn

- **De twee regels.** `beloningen-per-klasse.md:28` zegt: "Elk type heeft vier tiers". Regel 66 zegt: "**Nieuwe
  lijn** geeft het Normal-wapen van een andere lijn op dezelfde W."
- **Wat de code doet.** `spire_reward_offer` (SpireRewards.cc:357-373) volgt regel 28 en neemt de cel van de getrokken
  tier.
- **Wie dit beslist.** Volgens het mandaat beslist de bouwagent dit, want beloningsgroottes zijn niet aan Marck. De
  bouwagent kiest en logt in `decisions.md`. Ik heb niets veranderd.

### B11 (minor, niet gefixt): een oud aanbod kan terugkomen als de quest zelf trekt

- **Hoe het gebeurt.** `by_slot` wordt alleen bij een 'Q' vervangen. Faalt de 'Q' van een latere kamer en trekt de
  quest zelf, dan geeft een pick met hetzelfde slot en dezelfde entry het niet-genomen aanbod van de vorige kamer: een
  lagere W, geen verlies.
- **Waarom ik het niet fix.** Een bewaking op de kamer hangt af van wanneer `v_room` bij de pick bijgewerkt is. Dat
  kon ik niet verifiëren, en een fout daar zou het hoofdpad raken. Dit is een terugval van een terugval.

### B12 (nit, gefixt)

Het commentaar van de ledger (`SpireMag.hh:161`) noemde de opgeslagen sleutel "Feed". De sleutel heet `FeedLevels`.

### B13 (minor, niet gefixt): de eigen bossen van de run in de trede zijn niet getest

- **Wat groen blijft.** Mutant B9_trede_no_run_bosses (de bosskamers van deze run genegeerd) blijft groen.
- **Waarom het weinig uitmaakt.** In een gewone run schrijft `spire_checkpoint_record_boss` dezelfde boss bij zijn
  clear in het meta-bestand, en dat pad is nu getest (B9). Het gat raakt alleen een oefenrun: daar schrijft het
  meta-bestand niets, en opent Legendary na De Rol Le alleen via deze route.
- **Waarom ik het niet fix.** Een test moet daarvoor een boss-checkpoint door `spire_checkpoint_store` spelen. Daar is
  geen test-hook voor, en die bouwen valt buiten deze ronde. Voor de bouwagent.

## Verificatie na de fixes (letterlijk)

De stand is newserv `464e19d` en quest-repo `8509144f`. Alles is in de voorgrond gedraaid, achter elkaar, en zonder
een mutant ernaast.

```
$ cd /home/m4rck/code/worktrees/newserv/beloningen-a && mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc) newserv
build exit 0
[  4%] Linking CXX executable newserv
[100%] Built target newserv

$ build/newserv run-static-tests
run-static-tests exit 0
I 148982 2026-11-16 09:29:56 - -- All static tests passed
("failure at"-regels: 0)
I 148982 2026-11-16 09:29:00 -    F6 (review r3 B3: rule B's part of the ledger (Need, BoostDue, FeedLevels) comes back from the savestate file)
I 148982 2026-11-16 09:29:06 -    GUARD E2E (review r3: a reward weapon and mag food through the bridge; rule B's mag meets the weapon before its equip)
I 148982 2026-11-16 09:29:56 -    K3 (review r3: spire_drops_filter follows the AtaModel key of spire-rewards.json)

$ sh tests/spire-follow-city.test.sh build/newserv
follow-city exit 0
ok: partner floor 1, leader floor 2 -> skip
ok: partner floor 15, leader floor 14 -> warp
ok: spire-lives-check

$ D=$(mktemp -d /tmp/review-loop/qv.XXXXXX) && ln -s …/newserv/beloningen-a/build/newserv …/src …/system "$D"/ && NEWSERV_DIR="$D" bash …/pso-roguelike/beloningen-a/quests/spire/build.sh
quest build exit 0
--- build: .bin + .dat 239982 of 245000 bytes decompressed (.bin 165196)
--- q920: .bin 165196 of 178176 bytes decompressed, heat patch 359 fields, H 0..6 -> build/q920.heat.json
--- selftests: 34 gates caught every break of their own input (53 s)

$ cd /home/m4rck/code/worktrees/pso-roguelike/beloningen-a && NEWSERV_DIR=/home/m4rck/code/worktrees/newserv/beloningen-a python3 quests/spire/make_spire_rewards.py --check
OK: /home/m4rck/code/worktrees/newserv/beloningen-a/system/spire-rewards.json matches the design grid
grid check exit 0

$ python3 quests/spire/check_mag_before_wave.py quests/spire/build/spire-gc-e.src.txt --selftest --quiet
OK: ...; selftest 18 mutations caught

$ git status --short   (quest-repo)
(leeg)
$ git status --short   (newserv, vóór de rapport-commit)
?? docs/review-loop/reports/review-loop-beloningen-a-r3-opus.md
```

- **De bridge-case speelt de route zoals het servers-log hem toont** (`/tmp/review-loop/v-static.log`, de
  verificatierun hierboven, ingekort met `…`):
  ```
  Spire reward: offer Wapen Normal for room 1 (index 1, W2, trede 0, odds L<0 E<5 R<35): NORMAL Sword +3
  Spire reward: offer Mag-voer Rare for room 1 (index 1, W2, trede 0, odds L<0 E<5 R<35): RARE Mag food +10
  Spire reward: offer Wapen Rare for room 1 (index 1, W2, trede 0, odds L<0 E<5 R<35): RARE Sword +3 Draw
  Spire bridge: draw …: sent "Rewards: NORMAL Sword +3, $C1RARE Mag food +10$C7, $C1RARE Sword +3 Draw$C7. Walk to choose."
  Spire reward: pick Wapen Normal slot 3 …: NORMAL Sword +3 000200030000000000000000 (line Sword); needs mag POW 1 DEX 0 MIND 0 (safe ATA model); rule B's run mag follows in this room's pause B, the we…
  Spire reward: the equip of Sword +3 00810020 waits for rule B's mag
  Spire run mag: RESULT 1 …: step 0 confirmed, Mag 00 02000600F401640000000000 (run 30F8BC8E)
  Spire reward: TAKEN … Wapen line Sword W2 Normal Sword +3 … (req ATP 82 ATA 0 MST 0, weapon ATA 15); run copy Lv10 ATP 80 ATA 43 MST 53; mag before L5 DEF 5/POW 0…
  Spire reward: pick Mag-voer Rare slot 4 …: RARE Mag food +10 on POW; … mag now L6 DEF 5/POW 1/DEX 0/MIND 0 (kind 00); owed food 10 levels; …
  Spire run mag: RESULT 1 …: step 0 confirmed, Mag 00 02001000F4014C0400000000 (run 30F8BC8E)
  Spire run mag: ASK room 1 …: no change (NONE); …
  ```
  In de laatste mag zijn de bytes `F401` (DEF 500, level 5) en `4C04` (POW 1100, level 11). Het level is 16 (`10`):
  1 + 10 voer.
- **Wat deze verificatie niet bewijst.**
  - Clientgedrag. De equip op de Wii, de mag-swap in een echte pauze B en de timing tussen twee clients zijn niet
    gemeten.
  - Die meting is Marcks avondrun. Volgens CLAUDE.md is een claim over de client pas waar na een meting op de Wii.

## Wat ik niet heb aangeraakt

- Geen uitrol, geen herstart en geen ssh naar de pc. De draaiende newserv op de laptop heb ik niet aangeraakt. Geen
  geheugenkaart, geen Wakiru.
- Niets gepusht. Geen dependencies en geen lockfiles.
- `decisions.md` en de handover zijn van de bouwagent. Daarin heb ik niets gezet.
- B8, B10, B11 en B13 staan open; de reden staat per bevinding.
- `check_mag_swap.py` en de rest van de quest-generator heb ik niet gewijzigd. In de quest-repo is alleen
  `check_mag_before_wave.py` gewijzigd.

## Stopadvies

**Niet stoppen na deze ronde: nog één koude ronde, op mijn fixes.**

- **Waarom.** Deze ronde vond een crash in de productiecode die elke beloningstrekking geraakt had (B1). Er waren ook
  eenentwintig mutanten op dragende doelpunten die groen bleven: zestien in newserv en vijf in de quest. Die zijn nu
  alle rood. Mijn eigen nieuwe tests en de nieuwe
  poortregels RB1-RB5 zijn alleen door mij getoetst.
- **Wat de volgende ronde moet doen.** Een reviewer die deze fixes niet schreef leest `464e19d` en `8509144f`. Hij
  toetst vooral:
  - de bridge-case;
  - de trede-hook;
  - RB2 en RB3, waar ik de ack `leti r155, 1` als doel nam.
- **Wat er openstaat.** B8, B10, B11 en B13 blokkeren geen avondrun:
  - B8, B11 en B13 zijn klein en geven geen verlies;
  - B10 is een ontwerpkeuze voor de bouwagent.
- **Uitrol.** Mijn advies: rol pas uit als die ronde schoon is. Een build van vóór `464e19d` mag nooit naar de pc,
  door B1.
