# Naad N3: rapport

Fase N3 van de naad (`docs/review-loop/goals/naad.md` punt 3, en van punt 2 de soorttabel en de schakelaars).
Basis `80c2046`, tak `feature/beloningen-b`, zes commits tot en met `4887824` (dit rapport komt in een zevende).
Gemeten op 2026-11-16 op de laptop.

## Dekkingsclaim

Deze fase dekt: dat het aanbod van de soorten 1-5 gelijk is (de vingerafdruk van N1, groen in elke commit), dat de
verhuisde tekst letterlijk is (script, bewijs 2), en de nieuwe regels van tabel en schakelaars (eigen tests, bewijs 3
en 4).

Deze fase dekt NIET: het gedrag van keuze ('Z' sub 6), resultaat ('Z' sub 7) en aantrekken op een live client. Daarvan
is alleen de letterlijke verhuizing getoond; geen test speelt een keuze met een client. Dat "geen aanbod" bij de speler
uitkomt op het eigen item van de regel is het bestaande pad (`spire_reward_make_offer` vangt elke throw, de keuze
antwoordt `own_item`) en is gelezen, niet gespeeld. Ook niet gedekt: de haken H1-H11 en de run-mag in de savestate
(volgende fase), en alles op de pc of de Wii.

## Wat er staat

- `src/SpireMechanics.hh/.cc`: de soorttabel (`spire_mech_register_kind`, `spire_mech_register_kind_first`,
  `spire_mech_kind`, `spire_mech_kinds`, `spire_mech_kind_conflicts`) en de schakelaars (`spire_mech_enabled(key,
  default_off)`, bestand `system/spire-schakelaars.json`). De aanmeldlijst is een static binnen een functie.
- `src/SpireRewardKinds.hh` (nieuw, intern): wat de soortbestanden en de kern delen. De drie structs die de kern een
  soort aanreikt (`SpireRewardLive` bij het aanbod, `SpireRewardPick`, `SpireRewardResult`), de structs
  `SpireGameRun`, `SpireRewardPending` en `SpireRewardOffers` (letterlijk uit `SpireFlagBridge.cc`), `TIER_NAMES`, en de
  declaraties van de gedeelde functies van de brug.
- `src/SpireRewards.cc`: het gedeelde deel van het rooster en de verdeling. `spire_rewards_parse` roept na het gedeelde
  deel van elke aanmelding de eigen sectielezer aan; `spire_reward_offer` doet het gedeelde voorwerk (klasse, lijn in de
  hand, W) en vraagt dan de tabel.
- `src/SpireFlagBridge.cc`: `spire_reward_make_offer`, `spire_reward_pick` en `spire_reward_result` doen het gedeelde
  deel en roepen de functie van de soort aan. Twee gedeelde hulpfuncties bleven hier: `spire_reward_pick_rule_b` en
  `spire_reward_result_rule_b` (regel B, gebruikt door wapen en schijf).
- `src/SpireRewardWeapon.cc` (soort 1 en 2, `aanval`), `SpireRewardMagFood.cc` (3, `mags`), `SpireRewardDisk.cc` (4,
  `force.schijf`), `SpireRewardUnit.cc` (5, `verdediging.units`), elk met zijn aanmelding.
- `src/SpireRewards.hh`: de enum met 6 `FOCUS`, 7 `MANA`, 8 `ARMOR`; `spire_reward_kind_known` tot 8;
  `SpireRewardTable::sections`; `SpireRewardOffer::first_route`; `SpireRewardOfferInput`; `spire_reward_offer` kreeg een
  tiende parameter met standaardwaarde (`SpireRewardLive* live = nullptr`), dus geen aanroep in een test veranderde.
- `src/SpireTestsMechanics.cc` (nieuw, naam `mechanics`).
- `src/StaticTests.cc`: twee verwachtingen (zie Beslissingen 7).

Niet gewijzigd: `src/SpireTestsSeam.cc`, de vier `SpireTests{Attack,Mags,Force,Defence}.cc`, `src/SpireFlagBridge.hh`,
`tests/*`, `system/*`, `CMakeLists.txt` (geteld: 0 bestanden buiten de schrijfset in `git diff 80c2046 --name-only`).

## De vorm van de aanmelding (wat een nieuwe soort schrijft)

Eén bestand `src/SpireReward<Naam>.cc` dat `SpireRewardKinds.hh` insluit, met vier functies en één regel:

```cpp
void my_parse(const phosg::JSON& json, SpireRewardTable& ret);   // eigen sectie -> ret.sections["force.focus"]
std::optional<SpireRewardOffer> my_offer(const SpireRewardOfferInput& in, SpireRewardOffer ret);
void my_pick(SpireRewardPick& pk);       // 'Z' sub 6
void my_result(SpireRewardResult& rs);   // 'Z' sub 7
[[maybe_unused]] const bool registered = spire_mech_register_kind(
    {SpireRewardKind::FOCUS, "force.focus", false, my_parse, my_offer, my_pick, my_result});
```

- Eis (a), eigen sectie zonder `SpireRewards.hh` of `SpireFlagBridge.*` te wijzigen: de lezer krijgt de JSON van het
  hele rooster en bewaart een eigen struct in `ret.sections[<schakelaarsleutel>]` (een `shared_ptr<const void>`, die
  alleen het eigen bestand terugcast).
- Eis (b), eigen invoer zonder nieuwe parameter in de kern: het aanbod krijgt `SpireRewardOfferInput` (rooster, klasse,
  kamer, niveau, trede, `rnd`) met in `in.live` de client, het spel en de getrokken regel; de keuze en het resultaat
  krijgen `SpireRewardPick` en `SpireRewardResult` met client, spel, ledger, aanbod en de lopende keuze. De schijf
  haalt zo zelf de stand van de kopie op (`spire_reward_disk_state(*in.live)`); de kern kent die stand niet meer.
- Twee lege aanmeldpunten: `spire_mech_register_kind_first` meldt een route aan die vóór de soort zelf gevraagd wordt
  (soort 4: de android-route; soort 5: "eerst kracht"). Zijn aanbodfunctie antwoordt `nullopt` als het aanbod niet
  van hem is; geeft hij wel een aanbod, dan draagt dat `first_route` en gaan keuze en resultaat naar hem terug.
  Niemand meldt zich daar aan.

## Beslissingen (zelf genomen, binnen de opdracht)

1. **"Geen aanbod" is een throw van `spire_reward_offer`**, met de reden als tekst (`reward kind 6 is not registered`,
   `reward kind 1 is switched off (aanval)`). Dat is het pad dat elke niet te maken aanbieding al nam.
2. **De route vóór een soort is algemeen gebouwd**, niet alleen voor 4 en 5: één regel in de verdeling, geen
   uitzondering per nummer. De tabel weigert niets; de documentatie noemt de twee punten.
3. **Volgorde bij de schijf.** Vóór de naad las de brug de stand van de kopie vóórdat `spire_reward_offer` de klasse
   opzocht; nu daarna. Verschil alleen als beide falen: dan staat een andere reden in het journaal. De uitkomst (eigen
   item) is gelijk.
4. **Volgorde bij het lezen van het rooster.** Eerst het gedeelde deel, dan de soorten op nummer (MagFood, Disks,
   Units). Vóór de naad stond MagFood tussen de algemene sleutels en Units vóór Disks. Een rooster met twee fouten kan
   dus een andere fout als eerste melden; een goed rooster leest gelijk (vingerafdruk, alle tests).
5. **De sectie van een soort die uit staat wordt wel gelezen.** Anders mist de sectie in het gecachete rooster als de
   schakelaar weer aan gaat zonder dat het rooster verandert.
6. **De keuze toetst de schakelaar opnieuw**, ook voor een aanbod dat bij de 'Q' gemaakt is toen de soort aan stond:
   uit betekent dat de code van de soort niet draait. Het resultaat toetst hem niet: het item bestaat dan al.
7. **Tweede verwachting in `StaticTests.cc`.** Naast de genoemde toets op `spire_reward_kind_known` verwachtte de
   B8-lader-test dat `srv 6` "geen soort" is. Dat hangt aan dezelfde verbreding. Het onbekende voorbeeld is nu `srv 9`,
   de bewering zelf (`!e[3].srv_kind && !e[3].has_own_bytes`) is gelijk gebleven.
8. **Punt 7 van de opdracht (commentaar):** alleen `SpireRewards.hh:23-24` noemde het JSON-bestand als bron van het
   rooster; dat noemt nu `quests/spire/data/grid/*.json`. In `SpireFlagBridge.cc` staat geen verwijzing naar dat
   JSON-bestand (alleen naar het ontwerp `beloningen-per-klasse.md`), dus daar is niets gewijzigd.
9. **Twee functies alleen voor tests:** `spire_mech_set_switches_file` en `spire_mech_forget_kind`.
10. **Mag-voer heeft geen resultaatfunctie**: zijn keuze antwoordt geen item en laat geen lopende keuze achter, zoals
    vóór de naad.
11. **Geen bovenliggende sleutels**: `aanval` uit zet `aanval.drops` niet uit. Het ontwerp zegt er niets over.

## Bewust niet verhuisd

- **Het eigen item met de vloek van een schijfregel** (`own_item` in `spire_reward_pick`): dat is het terugvalpad en
  moet werken als `force.schijf` uit staat.
- **Regel B bij keuze en resultaat** (basis `SpireFlagBridge.cc:4740-4752` en `4992-5002`): gebruikt door wapen en
  schijf, dus gedeeld gebleven als twee hulpfuncties. In de tweede staat nog `if (p.offer.kind ==
  SpireRewardKind::DISK)` rond `disk_mind`: letterlijk gelaten.
- **`spire_reward_mag_need`, `spire_reward_meets`, `spire_reward_need_with_disk`**: regel B, tussen mags en aanval in.
- **`spire_reward_held_weapon`, `spire_reward_run_mag`, `spire_reward_worn`, `spire_reward_mag_text`,
  `spire_reward_line_of`**: door meer dan één soort gebruikt.
- **De sleutels `TierDiskMind` en `RungDiskColumns`**: algemene sleutels van het rooster, gelezen met dezelfde lambda
  als `RungWeaponSteps`. Ze horen bij de force; verhuizen vraagt die lambda te delen.
- **`Classes`, `Items`, `LineGroups`, `LineOf`**: het voorwerk van elk aanbod leest ze (ook mag-voer krijgt een
  `from_line`), dus gedeeld.
- **De velden en declaraties van de soorten 1-5 in `SpireRewards.hh`** bleven waar ze stonden.

## Bewijs 1: de vingerafdruk is groen in elke commit

Per commit gebouwd en gedraaid vóór het committen (`build/n3-cycle.sh`: `make`, `run-static-tests`,
`spire-follow-city`). Elke regel is de letterlijke inhoud van het exit-bestand; in elk log staat de regel
`-- Spire seam: the reward offers are as before the seam` één keer en eindigt het log op `-- All static tests passed`.

```
1a931b1 (stap a)       build 0 static 0 city 0
481f673 (wapen)        build 0 static 0 city 0
5c33319 (mag-voer)     build 0 static 0 city 0
0ffde50 (schijf)       build 0 static 0 city 0
ea401af (unit)         build 0 static 0 city 0
4887824 (tests)        build 0 static 0 city 0
```

`tests/spire-seam/` en `src/SpireTestsSeam.cc` zijn niet gewijzigd (`git diff 80c2046 --stat` noemt ze niet).

## Bewijs 2: letterlijk verhuisd

Het script staat onderaan (het stond in de scratchmap, niet in de repo). Het doet drie dingen: (1) elke niet-lege
regel die uit `SpireRewards.cc` en `SpireFlagBridge.cc` verdween, op witruimte vooraan na geteld, moet minstens zo vaak
in de vier soortbestanden, `SpireMechanics.cc` of `SpireRewardKinds.hh` staan; wat dat niet haalt wordt opgesomd; (2)
elk verhuisd blok van de basis staat aaneengesloten en in volgorde op zijn nieuwe plek; (3) per soortbestand de regels
die niet uit de basis komen. `SpireRewardKinds.hh` telt mee als bestemming: daar staan de drie structs en `TIER_NAMES`.

**Verhuisd per soortbestand** (niet-lege regels): wapen 185, mag-voer 21, schijf 202, unit 126.

**De 31 regels die verdwenen zonder te verhuizen**, in vier groepen:

- 14 kopregels van gedeelde functies die op hun plek bleven en alleen `static` verloren, of hun standaardwaarde aan de
  declaratie in `SpireRewardKinds.hh` gaven: `FB` 393, 1183, 1240, 1519, 2212, 3551, 3840, 3841, 4295, 4324, 4371,
  4572 en `RW` 123, 130.
- 1 kopregel van `spire_reward_offer` (`RW` 471): kreeg de parameter `live`.
- 12 omhullende regels van de takken per soort: `if (kind == ...) {` (`RW` 508, 514, 528; `FB` 4480, 4692, 4709, 4985,
  5003), `bool is_disk`, twee keer `if (is_disk) {` en één `} else {` (`FB` 4723, 4724, 4753, 4736).
- 4 regels van `spire_reward_make_offer` die bij het vertrek van de schijfstand veranderden: de aanroep (`FB` 4505),
  `!disk ? std::string()` (4510), en de eerste en laatste regel van de journaaltekst (4511, 4514), die nu in
  `SpireRewardDisk.cc:189` en `:192` staan met een ander begin en einde.

Grens van het instrument: de sluitaccolades en de `return;` van die omhullende regels zijn te algemeen om te tellen;
de blokcontrole (2) dekt de inhoud ertussen.

**Wat een reviewer in de soortbestanden moet lezen** (niet uit de basis, naast includes, commentaar en de regels die
een oude naam binden aan wat de kern aanreikt, `auto& c = pk.c;`): 40 regels, hieronder letterlijk. Het zijn de koppen
van de functies, de aanroepen van de twee gedeelde hulpfuncties, de aanmeldingen, en in `SpireRewardDisk.cc` de zeven
regels waarmee de schijf zijn stand zelf ophaalt (`:184-189` en `:192`).

**Nieuwe regels in de kern** (niet-leeg, geteld tegen de basis): `SpireRewards.cc` 38, `SpireFlagBridge.cc` 52. Dat
zijn de verdeling, de twee aanroepen van de tabel, de twee hulpfuncties met hun naambindingen en de kopregels zonder
`static`.

```
$ python3 verify.py /home/m4rck/code/worktrees/newserv/beloningen-b; echo verify_exit=$?
lines gone from SpireRewards.cc + SpireFlagBridge.cc (stripped, non-blank): 419 distinct, 591 in all
of those NOT in a kind file, SpireMechanics.cc or SpireRewardKinds.hh often enough: 30 distinct, 31 in all
   gone 1x, found 0x  [FB:393]  static void spire_send_ticker(std::shared_ptr<Client> c, const std::string& text) {
   gone 1x, found 0x  [FB:1183]  static SpireGameRun* spire_game_run_for(std::shared_ptr<const Lobby> l) {
   gone 1x, found 0x  [FB:1240]  static bool spire_equip_item_available_for(std::shared_ptr<Client> c) {
   gone 1x, found 0x  [FB:1519]  static SpireRunMagLedger* spire_run_mag_ledger_for(std::shared_ptr<const Client> c, uint32_t run_id, bool create) {
   gone 1x, found 0x  [FB:2212]  static std::optional<uint32_t> spire_start_level_run_bonus(const Client* c, uint32_t lobby_id, uint32_t quest_number);
   gone 1x, found 0x  [FB:3551]  static std::optional<uint32_t> spire_start_level_run_bonus(const Client* c, uint32_t lobby_id, uint32_t quest_number) {
   gone 1x, found 0x  [FB:3840]  static asio::awaitable<void> spire_equip_item_send(std::shared_ptr<Client> c, std::weak_ptr<const Lobby> weak_l,
   gone 1x, found 0x  [FB:3841]  uint32_t item_id, std::string what, uint8_t point, uint32_t slot = 0) {
   gone 1x, found 0x  [FB:4295]  static SpireRewardOffers& spire_reward_offers_for(const std::shared_ptr<Client>& c, uint32_t lobby_id) {
   gone 1x, found 0x  [FB:4324]  static std::optional<SpireMagBytes> spire_reward_run_mag(const PlayerInventory& inventory, bool* equipped = nullptr) {
   gone 1x, found 0x  [FB:4371]  static std::string spire_reward_mag_text(const std::optional<SpireMagBytes>& mag) {
   gone 1x, found 0x  [FB:4480]  if (kind == SpireRewardKind::DISK) { // part B: what the run copy knows, may learn and can reach with its mag
   gone 1x, found 0x  [FB:4505]  spire_reward_held_weapon(overlay->inventory), room_index, tier, rnd, disk ? &*disk : nullptr, rung);
   gone 1x, found 0x  [FB:4510]  !disk ? std::string()
   gone 1x, found 0x  [FB:4511]  : std::format(" (the line's own technique: {}; MST {}, mag MIND {} worn and {} planned, room for {}; a curse "
   gone 1x, found 0x  [FB:4514]  disk->mind_now, disk->mind_plan, disk->mind_room, disk->extra_levels));
   gone 1x, found 0x  [FB:4572]  static void spire_reward_answer(std::shared_ptr<Client> c, const SpireMagBytes* item, bool mag_follows) {
   gone 1x, found 0x  [FB:4692]  if (offer->kind == SpireRewardKind::MAG_FOOD) {
   gone 1x, found 0x  [FB:4709]  if (offer->kind == SpireRewardKind::UNIT) { // part B: no requirement and no mag; newserv equips it after its RESULT
   gone 1x, found 0x  [FB:4723]  bool is_disk = offer->kind == SpireRewardKind::DISK;
   gone 2x, found 0x  [FB:4724,FB:4753]  if (is_disk) {
   gone 1x, found 0x  [FB:4985]  if (p.offer.kind == SpireRewardKind::UNIT) { // part B: no rule B need of its own; the weapon's need stays
   gone 1x, found 0x  [FB:5003]  if (p.offer.kind == SpireRewardKind::DISK) { // nothing to equip: the player uses the disk
   gone 1x, found 0x  [RW:123]  static uint32_t spire_code_of(const std::string& hex) {
   gone 1x, found 0x  [RW:130]  static uint32_t spire_get_u(const phosg::JSON& json, const std::string& key, uint32_t max, uint32_t def = 0xFFFFFFFF) {
   gone 2x, found 1x  [RW:386,RW:405,FB:379]  } else {
   gone 1x, found 0x  [RW:471]  const SpireRewardDiskState* disk, uint8_t rung) {
   gone 1x, found 0x  [RW:508]  if (kind == SpireRewardKind::MAG_FOOD) {
   gone 1x, found 0x  [RW:514]  if (kind == SpireRewardKind::UNIT) {
   gone 1x, found 0x  [RW:528]  if (kind == SpireRewardKind::DISK) {
ok   RW  414- 438 ( 25 lines) -> SpireRewardWeapon.cc:30-54  spire_reward_weapon_bytes, spire_reward_weapon_name
ok   RW  581- 610 ( 30 lines) -> SpireRewardWeapon.cc:67-96  offer: the weapon and new line branch
ok   FB 4737-4738 (  2 lines) -> SpireRewardWeapon.cc:112-113  pick: the weapon's need
ok   FB 4764-4772 (  9 lines) -> SpireRewardWeapon.cc:115-123  pick: the weapon's journal line
ok   FB 4775-4889 (115 lines) -> SpireRewardWeapon.cc:126-240  spire_reward_equip (coroutine, whole)
ok   FB 5008-5012 (  5 lines) -> SpireRewardWeapon.cc:253-257  result: equip_seq, journal line, co_spawn
ok   RW  157- 159 (  3 lines) -> SpireRewardMagFood.cc:33-35  parse: MagFood
ok   RW  509- 512 (  4 lines) -> SpireRewardMagFood.cc:43-46  offer: the mag food branch
ok   FB 4693-4706 ( 14 lines) -> SpireRewardMagFood.cc:63-76  pick: the mag food branch
ok   RW  285- 335 ( 51 lines) -> SpireRewardDisk.cc:62-112  parse: Disks
ok   RW  440- 467 ( 28 lines) -> SpireRewardDisk.cc:29-56  spire_reward_disk_level
ok   RW  529- 579 ( 51 lines) -> SpireRewardDisk.cc:194-244  offer: the disk branch
ok   FB 4434-4455 ( 22 lines) -> SpireRewardDisk.cc:115-136  spire_reward_mind_plan (whole)
ok   FB 4479-4479 (  1 lines) -> SpireRewardDisk.cc:146-146  make_offer: the disk state's declaration
ok   FB 4481-4503 ( 23 lines) -> SpireRewardDisk.cc:148-170  make_offer: the disk state, read from the run copy
ok   FB 4512-4513 (  2 lines) -> SpireRewardDisk.cc:190-191  make_offer: the journal detail (middle lines)
ok   FB 4720-4722 (  3 lines) -> SpireRewardDisk.cc:261-263  pick: the disk's comment
ok   FB 4725-4735 ( 11 lines) -> SpireRewardDisk.cc:264-274  pick: the class's highest level, disk_mind, need
ok   FB 4754-4761 (  8 lines) -> SpireRewardDisk.cc:276-283  pick: the disk's journal line
ok   FB 5004-5005 (  2 lines) -> SpireRewardDisk.cc:295-296  result: the disk's journal line
ok   RW  254- 284 ( 31 lines) -> SpireRewardUnit.cc:33-63  parse: Units
ok   RW  515- 526 ( 12 lines) -> SpireRewardUnit.cc:73-84  offer: the unit branch
ok   FB 4710-4717 (  8 lines) -> SpireRewardUnit.cc:97-104  pick: the unit branch
ok   FB 4891-4960 ( 70 lines) -> SpireRewardUnit.cc:107-176  spire_reward_equip_unit (coroutine, whole)
ok   FB 4986-4990 (  5 lines) -> SpireRewardUnit.cc:185-189  result: the unit branch
ok   RW   88-  88 (  1 lines) -> SpireRewardKinds.hh:33-33  TIER_NAMES
ok   FB 1171-1180 ( 10 lines) -> SpireRewardKinds.hh:41-50  struct SpireGameRun
ok   FB 4274-4292 ( 19 lines) -> SpireRewardKinds.hh:59-77  struct SpireRewardPending, struct SpireRewardOffers
ok   FB 4740-4752 ( 13 lines) -> SpireFlagBridge.cc:4637-4649  STAYS in the bridge: rule B at the pick (spire_reward_pick_rule_b)
ok   FB 4992-5002 ( 11 lines) -> SpireFlagBridge.cc:4693-4703  STAYS in the bridge: rule B at the result (spire_reward_result_rule_b)
src/SpireRewardWeapon.cc: 267 lines, 185 moved (non-blank); not from the base: 17 #include, 10 comment lines, 26 name bindings (auto& x = in.x; and the like), 5 closing braces and namespace lines, and these 10:
     59  std::optional<SpireRewardOffer> spire_reward_weapon_offer(const SpireRewardOfferInput& in, SpireRewardOffer ret) {
    100  void spire_reward_weapon_pick(SpireRewardPick& pk) {
    114    spire_reward_pick_rule_b(pk); // SpireFlagBridge.cc: p.mag_due, the pick in flight, the answer
    243  void spire_reward_weapon_result(SpireRewardResult& rs) {
    250    spire_reward_result_rule_b(rs); // SpireFlagBridge.cc: the ledger keeps the weapon's need
    261  [[maybe_unused]] const bool spire_reward_weapon_registered =
    262      spire_mech_register_kind({SpireRewardKind::WEAPON, "aanval", false, nullptr, spire_reward_weapon_offer,
    263          spire_reward_weapon_pick, spire_reward_weapon_result}) &&
    264      spire_mech_register_kind({SpireRewardKind::NEW_LINE, "aanval", false, nullptr, spire_reward_weapon_offer,
    265          spire_reward_weapon_pick, spire_reward_weapon_result});
src/SpireRewardMagFood.cc: 82 lines, 21 moved (non-blank); not from the base: 17 #include, 8 comment lines, 15 name bindings (auto& x = in.x; and the like), 5 closing braces and namespace lines, and these 5:
     32  void spire_reward_mag_food_parse(const phosg::JSON& json, SpireRewardTable& ret) {
     39  std::optional<SpireRewardOffer> spire_reward_mag_food_offer(const SpireRewardOfferInput& in, SpireRewardOffer ret) {
     50  void spire_reward_mag_food_pick(SpireRewardPick& pk) {
     79  [[maybe_unused]] const bool spire_reward_mag_food_registered = spire_mech_register_kind({SpireRewardKind::MAG_FOOD, "mags",
     80      false, spire_reward_mag_food_parse, spire_reward_mag_food_offer, spire_reward_mag_food_pick, nullptr});
src/SpireRewardDisk.cc: 302 lines, 202 moved (non-blank); not from the base: 17 #include, 12 comment lines, 30 name bindings (auto& x = in.x; and the like), 8 closing braces and namespace lines, and these 18:
     61  void spire_reward_disk_parse(const phosg::JSON& json, SpireRewardTable& ret) {
    139  SpireRewardDiskState spire_reward_disk_state(const SpireRewardLive& live) {
    147    { // part B: what the run copy knows, may learn and can reach with its mag
    171    return *disk;
    175  std::optional<SpireRewardOffer> spire_reward_disk_offer(const SpireRewardOfferInput& in, SpireRewardOffer ret) {
    184    const SpireRewardDiskState* disk = in.disk;
    185    std::optional<SpireRewardDiskState> live_disk;
    186    if (!disk && in.live) {
    187      live_disk = spire_reward_disk_state(*in.live);
    188      disk = &*live_disk;
    189      in.live->log_detail = std::format(" (the line's own technique: {}; MST {}, mag MIND {} worn and {} planned, room for {}; a curse "
    192              disk->mind_now, disk->mind_plan, disk->mind_room, disk->extra_levels);
    248  void spire_reward_disk_pick(SpireRewardPick& pk) {
    275    spire_reward_pick_rule_b(pk); // SpireFlagBridge.cc: p.mag_due, the pick in flight, the answer
    287  void spire_reward_disk_result(SpireRewardResult& rs) {
    292    spire_reward_result_rule_b(rs); // SpireFlagBridge.cc: the ledger keeps the disk's MIND
    299  [[maybe_unused]] const bool spire_reward_disk_registered = spire_mech_register_kind({SpireRewardKind::DISK, "force.schijf",
    300      false, spire_reward_disk_parse, spire_reward_disk_offer, spire_reward_disk_pick, spire_reward_disk_result});
src/SpireRewardUnit.cc: 196 lines, 126 moved (non-blank); not from the base: 17 #include, 9 comment lines, 18 name bindings (auto& x = in.x; and the like), 6 closing braces and namespace lines, and these 7:
     32  void spire_reward_unit_parse(const phosg::JSON& json, SpireRewardTable& ret) {
     67  std::optional<SpireRewardOffer> spire_reward_unit_offer(const SpireRewardOfferInput& in, SpireRewardOffer ret) {
     88  void spire_reward_unit_pick(SpireRewardPick& pk) {
    179  void spire_reward_unit_result(SpireRewardResult& rs) {
    192  [[maybe_unused]] const bool spire_reward_unit_registered = spire_mech_register_kind({SpireRewardKind::UNIT,
    193      "verdediging.units", false, spire_reward_unit_parse, spire_reward_unit_offer, spire_reward_unit_pick,
    194      spire_reward_unit_result});
VERDICT: all checks ok
verify_exit=0
```

## Bewijs 3: bouw en tests

Laatste regels van de bouw (exit 0, 0 regels met `warning` in het log):

```
[  2%] Building CXX object CMakeFiles/newserv.dir/src/Revision.cc.o
[  3%] Linking CXX executable newserv
[100%] Built target newserv
make: Leaving directory '/home/m4rck/code/worktrees/newserv/beloningen-b/build'
```

`build/newserv run-static-tests` (exit 0), de regels van de aangemelde testbestanden en de staart:

```
I 1600004 2026-11-16 23:14:27 - -- Spire test file: attack
I 1600004 2026-11-16 23:14:27 - -- Spire test file: defence
I 1600004 2026-11-16 23:14:27 - -- Spire test file: force
I 1600004 2026-11-16 23:14:28 - -- Spire test file: mags
I 1600004 2026-11-16 23:14:28 - -- Spire test file: mechanics
W 1600004 2026-11-16 23:14:28 - Spire switches: /tmp/newserv-static-spire-mechanics-1600004/spire-schakelaars.json is unreadable ((position 4) dictionary does not contain key/value pairs); the last good state stays
W 1600004 2026-11-16 23:14:28 - Spire switches: /tmp/newserv-static-spire-mechanics-1600004/spire-schakelaars.json is unreadable (JSON value cannot be accessed as a dict); the last good state stays
W 1600004 2026-11-16 23:14:28 - Spire switches: /tmp/newserv-static-spire-mechanics-1600004/spire-schakelaars.json is unreadable (JSON value cannot be accessed as a bool); the last good state stays
I 1600004 2026-11-16 23:14:28 - -- Spire test file: seam
I 1600004 2026-11-16 23:14:28 - -- Spire seam: the reward offers are as before the seam
I 1600004 2026-11-16 23:14:29 - -- Spire seam: a savestate with the run mag loads and is written back the same
...
I 1600004 2026-11-16 23:15:36 - -- All static tests passed
```

De drie `W`-regels zijn de drie onleesbare bestanden van de test: één regel per bestand, terwijl de test de
schakelaars na elk ervan vier keer of vaker vraagt.

`sh tests/spire-follow-city.test.sh build/newserv` (exit 0):

```
ok: partner floor 15, leader floor 14 -> warp
ok: spire-lives-check
```

Wat `SpireTestsMechanics.cc` toetst: de soorten 1-5 zijn aangemeld onder de sleutel van hun eigenaar; 6-8 zijn bekend
en niet aangemeld en geven geen aanbod; 9 is onbekend; zonder schakelaarbestand staat alles aan, behalve een sleutel
die "standaard uit" is; een soort met zijn schakelaar uit geeft geen aanbod en de andere soorten geven wat ze gaven;
met de schakelaar weer aan komt hetzelfde aanbod terug; drie soorten onleesbaar bestand laten de laatste goede stand
staan (ook een bestand dat half klopt); het bestand wordt opnieuw gelezen als het verandert; een verdwenen bestand zet
alles aan; een nieuwe soort uit het testbestand zelf leest en bewaart zijn eigen sectie, staat "standaard uit", en
wordt gevraagd ná de route die zich vóór hem aanmeldde; een dubbele aanmelding wordt genoemd en de eerste blijft.

## Bewijs 4: een test van `SpireTestsMechanics.cc` bijt

In `src/SpireRewards.cc` de schakelaartoets van de verdeling uitgezet (`if (false && !spire_mech_enabled(...))`),
gebouwd, gedraaid: `build 0 static 1 city 0`.

```
I 1599505 2026-11-16 23:13:32 - -- Spire test file: mechanics
E 1599505 2026-11-16 23:13:32 - run-static-tests: failure at /home/m4rck/code/worktrees/newserv/beloningen-b/src/SpireTestsMechanics.cc:139: std::string("reward kind 1 is switched off (aanval)") != no_offer([&]() { offer_of(K::WEAPON); })
```

Teruggezet uit een kopie (`cmp` gelijk, `git diff --quiet -- src/SpireRewards.cc` gaf 0), herbouwd; daarna de groene
run van bewijs 3.

## Gezien, niet gebouwd

- Datakwaliteit: een kapotte sectie van een soort die uit staat weigert nog steeds het hele rooster (beslissing 5).
  Waar: `src/SpireRewards.cc`, de lus over `spire_mech_kinds()`. Wat het kost: per soort een eigen try, en een keuze
  wat een half gelezen rooster dan betekent.
- Gebruiksvriendelijkheid: gaat een schakelaar uit tussen de 'Q' en de keuze, dan noemt de beloningsregel het item van
  de server en krijgt de speler het eigen item van de regel (beslissing 6). Uitrol gaat alleen bij nul spelers, dus
  dit treft alleen wie het bestand op een draaiende server wijzigt.
- Performance: `spire_mech_enabled` doet per vraag twee bestandsaanroepen (bestaat het, tijdstempel), zoals het rooster.
  De vingerafdruk vraagt het ruim 350 duizend keer (356447 regels, telling van N1) en de hele testrun duurde 69 s,
  tegen 67 s in het log van N2.
- Dynamiek: geen bovenliggende sleutels (beslissing 11). Wat het kost: een regel in `spire_mech_enabled` en een
  uitspraak in het ontwerp.
- Codekwaliteit: tien functies van de brug en twee van `SpireRewards.cc` hebben nu externe binding (de 14 kopregels
  van bewijs 2) en staan in `SpireRewardKinds.hh`. Een soortbestand kan ze allemaal aanroepen; de header zegt niet welke voor wie bedoeld is.
- Codekwaliteit: `clang-format` is niet gedraaid op de soortbestanden; letterlijk ging voor, zoals in N2.
- Toekomstbestendigheid: geen test speelt een keuze of een resultaat met een client, ook vóór de naad niet. De
  eerste soort die daar iets nieuws doet heeft die test nodig.

## Bijlage: het script

`verify.py` (aanroepen met het pad van de werkplek):

```python
#!/usr/bin/env python3
# Proof for the seam N3: every line that left SpireRewards.cc and SpireFlagBridge.cc stands, leading white space aside,
# in a kind file, in SpireMechanics.cc or in the shared internal header; which lines did not; and which lines of the
# kind files do not come from the base. Reads the base from git and the working tree. usage: verify.py <repo>
import re, subprocess, sys, os
from collections import Counter
repo, BASE = sys.argv[1], '80c2046'
def base(p):
    return subprocess.check_output(['git', '-C', repo, 'show', BASE + ':' + p], text=True).split('\n')[:-1]
def now(p):
    return open(os.path.join(repo, p)).read().split('\n')[:-1]
SRC = {'RW': 'src/SpireRewards.cc', 'FB': 'src/SpireFlagBridge.cc'}
KINDS = ['src/SpireRewardWeapon.cc', 'src/SpireRewardMagFood.cc', 'src/SpireRewardDisk.cc', 'src/SpireRewardUnit.cc']
DESTS = KINDS + ['src/SpireMechanics.cc', 'src/SpireRewardKinds.hh']
B = {k: base(p) for k, p in SRC.items()}
W = {k: now(p) for k, p in SRC.items()}
D = {p: now(p) for p in DESTS}
ok = True
def check(cond, text):
    global ok
    ok &= bool(cond)
    print(('ok   ' if cond else 'FAIL ') + text)
strip = lambda ls: [l.strip() for l in ls if l.strip()]

# 1. the multiset: lines gone from the two sources against the lines of the destinations
cb, cw, cd = Counter(), Counter(), Counter()
for k in SRC:
    cb.update(strip(B[k])); cw.update(strip(W[k]))
for p in DESTS:
    cd.update(strip(D[p]))
gone = Counter({l: cb[l] - cw[l] for l in cb if cb[l] > cw[l]})
missing = {l: (n, cd[l]) for l, n in gone.items() if cd[l] < n}
trivial = lambda l: l in ('}', '{', '};', 'return;', '} else {')
print('lines gone from SpireRewards.cc + SpireFlagBridge.cc (stripped, non-blank): %d distinct, %d in all' % (len(gone), sum(gone.values())))
print('of those NOT in a kind file, SpireMechanics.cc or SpireRewardKinds.hh often enough: %d distinct, %d in all'
      % (len(missing), sum(n - h for n, h in missing.values())))
where = {}
for k in SRC:
    for i, l in enumerate(B[k], 1):
        where.setdefault(l.strip(), []).append('%s:%d' % (k, i))
for l, (n, h) in sorted(missing.items(), key=lambda x: where[x[0]][0].split(':')[0] + '%06d' % int(where[x[0]][0].split(':')[1])):
    print('   gone %dx, found %dx  [%s]  %s' % (n, h, ','.join(where[l][:3]), l))

# 2. the blocks: each range of the base stands contiguously, in order, in its destination
RANGES = [
    ('RW', 414, 438, KINDS[0], 'spire_reward_weapon_bytes, spire_reward_weapon_name'),
    ('RW', 581, 610, KINDS[0], 'offer: the weapon and new line branch'),
    ('FB', 4737, 4738, KINDS[0], 'pick: the weapon\'s need'),
    ('FB', 4764, 4772, KINDS[0], 'pick: the weapon\'s journal line'),
    ('FB', 4775, 4889, KINDS[0], 'spire_reward_equip (coroutine, whole)'),
    ('FB', 5008, 5012, KINDS[0], 'result: equip_seq, journal line, co_spawn'),
    ('RW', 157, 159, KINDS[1], 'parse: MagFood'),
    ('RW', 509, 512, KINDS[1], 'offer: the mag food branch'),
    ('FB', 4693, 4706, KINDS[1], 'pick: the mag food branch'),
    ('RW', 285, 335, KINDS[2], 'parse: Disks'),
    ('RW', 440, 467, KINDS[2], 'spire_reward_disk_level'),
    ('RW', 529, 579, KINDS[2], 'offer: the disk branch'),
    ('FB', 4434, 4455, KINDS[2], 'spire_reward_mind_plan (whole)'),
    ('FB', 4479, 4479, KINDS[2], 'make_offer: the disk state\'s declaration'),
    ('FB', 4481, 4503, KINDS[2], 'make_offer: the disk state, read from the run copy'),
    ('FB', 4512, 4513, KINDS[2], 'make_offer: the journal detail (middle lines)'),
    ('FB', 4720, 4722, KINDS[2], 'pick: the disk\'s comment'),
    ('FB', 4725, 4735, KINDS[2], 'pick: the class\'s highest level, disk_mind, need'),
    ('FB', 4754, 4761, KINDS[2], 'pick: the disk\'s journal line'),
    ('FB', 5004, 5005, KINDS[2], 'result: the disk\'s journal line'),
    ('RW', 254, 284, KINDS[3], 'parse: Units'),
    ('RW', 515, 526, KINDS[3], 'offer: the unit branch'),
    ('FB', 4710, 4717, KINDS[3], 'pick: the unit branch'),
    ('FB', 4891, 4960, KINDS[3], 'spire_reward_equip_unit (coroutine, whole)'),
    ('FB', 4986, 4990, KINDS[3], 'result: the unit branch'),
    ('RW', 88, 88, DESTS[5], 'TIER_NAMES'),
    ('FB', 1171, 1180, DESTS[5], 'struct SpireGameRun'),
    ('FB', 4274, 4292, DESTS[5], 'struct SpireRewardPending, struct SpireRewardOffers'),
    ('FB', 4740, 4752, SRC['FB'], 'STAYS in the bridge: rule B at the pick (spire_reward_pick_rule_b)'),
    ('FB', 4992, 5002, SRC['FB'], 'STAYS in the bridge: rule B at the result (spire_reward_result_rule_b)'),
]
covered = {p: set() for p in DESTS + [SRC['FB']]}
moved_count = Counter()
for src, a, b, dest, what in RANGES:
    want = [l.strip() for l in B[src][a - 1:b]]
    have = [l.strip() for l in (D[dest] if dest in D else W['FB'])]
    hits = [i for i in range(len(have) - len(want) + 1) if have[i:i + len(want)] == want and not (covered[dest] & set(range(i, i + len(want))))]
    check(len(hits) >= 1, '%s %4d-%4d (%3d lines) -> %s:%s  %s' % (src, a, b, b - a + 1, dest.replace('src/', ''),
          ('%d-%d' % (hits[0] + 1, hits[0] + len(want))) if hits else '?', what))
    if hits:
        covered[dest] |= set(range(hits[0], hits[0] + len(want)))
        moved_count[dest] += sum(1 for l in want if l)

# 3. the kind files: what does not come from the base
alias = re.compile(r'^(const )?auto& \w+ = (in|pk|rs|live)\.\w+;$|^using R = SpireRunMagRefusal;$')
for p in KINDS:
    lines = D[p]
    rest, n_inc, n_com, n_alias, n_triv = [], 0, 0, 0, 0
    for i, l in enumerate(lines):
        s = l.strip()
        if i in covered[p] or not s:
            continue
        if s.startswith('#include'):
            n_inc += 1
        elif s.startswith('//'):
            n_com += 1
        elif alias.match(s):
            n_alias += 1
        elif s in ('}', 'namespace {', '} // namespace'):
            n_triv += 1
        else:
            rest.append((i + 1, l))
    print('%s: %d lines, %d moved (non-blank); not from the base: %d #include, %d comment lines, %d name bindings '
          '(auto& x = in.x; and the like), %d closing braces and namespace lines, and these %d:'
          % (p, len(lines), moved_count[p], n_inc, n_com, n_alias, n_triv, len(rest)))
    for n, l in rest:
        print('   %4d  %s' % (n, l))
print('VERDICT: ' + ('all checks ok' if ok else 'A CHECK FAILED'))
sys.exit(0 if ok else 1)
```
