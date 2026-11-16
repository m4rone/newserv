# Review-loop beloningen-b-schijven, ronde 1 (opus, invalshoek doel)

## Maatstaf en scope

Maatstaf: het doel in `docs/review-loop/goals/beloningen-b-schijven.md` (de techniekschijf als beloning, B-E02-23
punt 3 en B-E02-30). Getoetst is wat de wijziging toevoegt of verandert:

- newserv `git diff b8da856 HEAD` (c36a8d2): `src/SpireRewards.hh/.cc` (soort DISK 4, de keuze van het level, de
  MST-regel, de grid-lader `Disks`), `src/SpireFlagBridge.cc` (de trekking, de pick, de RESULT, `DiskMind` in de
  savestate), `src/SpireMag.hh` (`disk_mind` in de ledger), `system/spire-rewards.json` (gegenereerd),
  `src/StaticTests.cc`.
- quest-repo `git diff b1e16776 HEAD` (cd7e5ecb): `data/rewards.json` (F39), `rewards.py` (`SRV_KINDS`),
  `make_spire_rewards.py` (`TECH_MST`, `build_disks`), en de teksten in `decisions.md` en het ontwerp.

Meegelezen omdat de wijziging ze aanroept: `Items.cc:27-35` (de server bij gebruik van een schijf),
`SaveFileFormats.cc:1282` (`get_technique_level`), `SpireMag.cc:185-276` (regel B en `spire_mag_fit_200`),
`spire_run_mag_ledger_for` (het laden uit de savestate), `make_spire.py:2146-2204` (de pick in de quest),
`data/check_pool_order.py` en `research/schijf-mst-eis.md`.

## Per punt van het doel: gehaald of niet, met bewijs

**1. De schijf (Schijf, serversoort 4): de server kiest bij de trekking een techniek en een level dat de kopie kan
leren. Gehaald.**

- Bij de trekking: `spire_reward_make_offer` (SpireFlagBridge.cc:4449-4467) vult voor DISK wat de kopie kent
  (`get_technique_level`), het klassemaximum per techniek, de MST van de kopie met gedragen units, de MST-cap van
  de klasse en de MIND-ruimte van de mag; `spire_reward_offer` (SpireRewards.cc:478-514) kiest daaruit.
- Sega's level voor kamer en tier: de rij `disk_levels[kamer][techniek][tier]`, gegenereerd uit het grid
  (`build_disks`); de grid-controle meldt dat het bestand klopt (zie Verificatie).
- Hoogstens het klassemaximum: `max_level = min(class_max, 29) + 1` in `spire_reward_disk_level`
  (SpireRewards.cc:415-434); `class_max` komt uit `get_max_tech_level` van de GC_V3-tabel, dezelfde waarde waartegen
  `Items.cc:30-35` een gebruikte schijf toetst. De pick toetst het nog eens (SpireFlagBridge.cc:4664-4672) en geeft
  anders het eigen item.
- Een bekende techniek minstens twee hoger, één als dat het maximum is:
  `floor = known_level ? min(known_level + 2, max_level) : 1`; zit de kopie al op het maximum, dan 0 (geen schijf).
- Omlaag, nooit tot wat de kopie kent: de lus `for (; level > known_level; level--)`; dat klopt met de clientregel
  in `research/schijf-mst-eis.md:21` (een gelijke of lagere schijf weigert de client).
- De MST-regel: `spire_reward_disk_mst = Base + Step x (level - 1)`; de negentien paren in `TECH_MST`
  (make_spire_rewards.py:45-48) heb ik naast de tabel van `research/schijf-mst-eis.md:27-45` gelegd: alle gelijk,
  Ryuker en Reverser vast 150. De mag telt 2 MST per MIND-level, naar boven afgerond (`spire_reward_disk_mind`),
  binnen `mind_room`; de eis hoogstens de MST-cap van de klasse.
- Tests: de gevallen van `spire_reward_disk_level` (StaticTests.cc, blok "part B, the disk") en een aanbieding voor
  elke klasse die technieken leert, in elke kamer en tier, nooit boven het klassemaximum.

**2. De mag draagt de eis, per stat het hoogste van wapen en schijf, ook na een hervatte run. Gehaald.**

- Bij de pick van een schijf: `p.need = need_with_disk(led->need, max(led->disk_mind, p.disk_mind))`
  (SpireFlagBridge.cc:4673-4675): POW en DEX van het wapen blijven staan. Bij een later wapen:
  `need_with_disk(weapon_need, led->disk_mind)` (4677-4678): de MIND van de schijf blijft staan.
- De MIND-ruimte bij de trekking is `200 - (need.pow + need.dex)` (4463-4464), dus POW + DEX + schijf-MIND past in
  200; `spire_mag_fit_200` snijdt dan alleen in stats die niet gevraagd zijn (SpireMag.cc:208-222).
- Na de RESULT: `led->disk_mind = max(...)`, `led->need = p.need`, `boost_due` als de mag nog moet komen
  (4934-4942); de mag van regel B komt in pauze B (de quest zet MAG_PENDING op de vlag MAG_FOLLOWS,
  make_spire.py:2199-2203).
- Hervatte run: `DiskMind` gaat de savestate in en komt terug, afgekapt op 0-200 (SpireFlagBridge.cc:883-885, 997);
  `spire_run_mag_ledger_for` neemt de hele ledger uit de savestate over (1542-1543). Tests: de savestate-roundtrip
  met `DiskMind` en de e2e "part B: a Disk reward through the bridge; rule B's mag carries its MST, also past a reward
  weapon".

**3. In de quest één nieuwe regel "Disk" aan het eind van de force-pool; zonder iets te geven het eigen item.
Gehaald.**

- F39 staat als laatste van de FO-pool (36 regels, onder de grens van 38 uit `EXTRA_WAIT_MAX_ENTRIES`),
  `srv: schijf` = 4 in `SRV_KINDS`, eigen item `03 01 02 00 00 02` (twee Trifluids). De server kent soort 4
  (`spire_reward_kind_known`, met test).
- Niets te geven: geen `Disks` in het bestand, geen techniek met een level, een weigering van regel B, een uitgezette
  tabel of een schijf boven het klassemaximum leiden alle naar `own_item` (SpireFlagBridge.cc:4558-4607, 4670).

**Vaste grenzen. Gehaald.**

- Geen itemverlies of dubbele gift: één antwoord per pick; RESULT 1 haalt het slot weg, RESULT 0 (vol pak) laat de
  aanbieding staan en schrijft geen eis.
- Geen vastloper: elke tak antwoordt; een uitzondering zonder antwoord laat de quest na RS_POLLS stoppen met wachten
  (make_spire.py:2179).
- Nooit een schijf die de server bij gebruik weigert: zie punt 1 (keuze en controle bij de pick).
- Tellers per poolregel: F39 staat aan het eind; `check_pool_order.py` draait vooraan in `build.sh` (50-52) en de
  questbouw slaagt.

## Bevindingen

Geen. Ik heb geen punt van het doel gevonden dat niet gehaald is, en niets dat de wijziging stukmaakt.

## Mutanten (alleen als je er draaide, letterlijk)

Geen gedraaid: er was geen eigen fix om te testen.

## Verificatie na de fixes (letterlijk)

Er waren geen fixes; dit is de stand c36a8d2 (newserv) en cd7e5ecb (quest).

Server op de pc (`rsync ... && ssh pc '... make ... run-static-tests ... spire-follow-city.test.sh ...'`):

```
[  1%] Built target newserv-Revision-cc
[  2%] Building CXX object CMakeFiles/newserv.dir/src/Revision.cc.o
[  3%] Linking CXX executable newserv
[100%] Built target newserv
make: Leaving directory '/home/m4rck/code/newserv-review-b/build'
I 784071 2026-11-16 18:21:33 - [C-BB] Deleted
I 784071 2026-11-16 18:21:33 - [Game:7A03] Deleted
I 784071 2026-11-16 18:21:33 - -- All static tests passed
ok: partner floor 1, leader floor 2 -> skip
ok: partner floor 15, leader floor 14 -> warp
ok: spire-lives-check
exit=0
```

Make bouwde alleen `Revision.cc` opnieuw: de objecten in `~/code/newserv-review-b` waren al bijgewerkt voor deze
bronnen (rsync -a houdt de tijdstempels), dus de geteste binary is die van deze stand.

Questbouw op de laptop (exit 0, slot van de uitvoer):

```
--- q920 labels used: engine 239 of 239, rewards 768 of 768, economy 486 of 512, score 90 of 256, coop 256 of 256, path 256 of 256, e08 716 of 768, switch 45 of 256, overflow 596 of 768
--- build: .bin + .dat 242922 of 245000 bytes decompressed (.bin 168136)
--- q920: .bin 168136 of 178176 bytes decompressed, heat patch 359 fields, H 0..6 -> build/q920.heat.json
--- selftests: 35 gates caught every break of their own input (129 s)
```

Grid:

```
OK: /home/m4rck/code/worktrees/newserv/beloningen-b/system/spire-rewards.json matches the design grid
exit=0
```

## Wat ik niet heb aangeraakt

Geen code gewijzigd. Wat ik zag en wat niet tegen het doel ingaat. Ik noem het, maar het gaat niet naar een backlog:

- **Wapen-eis plus schijf-MIND boven 200.** Vraagt een later beloningswapen zoveel POW en DEX dat dat samen met de
  schijf-MIND boven 200 komt, dan beslist `spire_mag_fit_200` van deel A. Bij een force snijdt die eerst in POW, dus
  dan mist dat wapen POW. Het doel geeft voor die overloop geen regel, en de wapenaanbieding van deel A houdt geen
  rekening met de schijf-MIND.
- **Een schijf zonder regel B.** Weigert regel B (DISABLED, NO_TABLE, TWO_MAGS) of is er geen ledger, dan krijgt de
  speler de schijf wel, maar gaat haar MIND niet in de ledger. Een wapen gaat in die stand net zo. Bij de trekking
  rekent de server dan alsof de mag er is.
- **De schijf-MIND blijft de hele run staan**, ook als de schijf al geleerd is. Zo vraagt het doel het.
- **Het "offer now"-pad van de pick.** Gaf de trekking geen schijf (de regel toont dan "Disk"), dan kan de pick met
  een nieuwe tier-worp toch een schijf geven. Dat geldt voor elk soort beloning en is cosmetisch.
- **Episode 2.** `rewards.py` leest `data/rewards.json` voor elke quest (`DATA_PATH`, regel 52, tenzij
  `SPIRE_REWARDS` een treebestand noemt), ook voor `spire-ep2.json`. Daardoor komt F39 ook in een Episode 2-quest. Kamerindex k-15 gebruikt daar de rijen van Episode 1. Voor de
  unitregel F38 was dat al zo.
- **De pin van `pc-build.sh`** staat nog op 3c6f250. De handover zet hem na deze review om (stap c).
- Uitrol, pc-dienst en lab, de bouwscripts en poorten, geheugenkaarten: niet aangeraakt.

## Stopadvies

Stoppen. Deze ronde vond geen bevinding en de drie controles zijn groen. Volgens de handover komen nu de pin, het
q929-artefact en de uitrol.
