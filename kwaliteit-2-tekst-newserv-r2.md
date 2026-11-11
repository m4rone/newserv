# Kwaliteit 2 (tekst en ux), newserv-kant, ronde 2: de serverbezwaren van de critic

Tak spike/kwaliteit-2, start b2b38f2, codecommits fbd40a4 en 0626572. Bron: kwaliteit-2-bundel2-critic.md (K2, K3, K5, K7) en
kwaliteit-2-tekst-newserv.md § "Gezien, niet gebouwd". Questkant gelezen op pso-roguelike spike/kwaliteit-2 25c3a0c
(alleen lezen). Alle metingen in dit rapport: 2026-11-11.

**Dekking.** Dit dekt de genoemde serverpunten, elk met een statische test die rood is op een mutatie die het oude
gedrag nabootst. Dit dekt niet: questteksten (lopen parallel), balans, en wat alleen de Wii beslist (of een tickerregel
op de echte client zichtbaar blijft, en of 4 s tussen twee regels daar genoeg is). Niets gemeten in een draaiende
server, in het lab of op de Wii. Geen ssh naar de pc, geen newserv gestopt of herstart, geen labrun, geen Dolphin, geen
push. src/SpireDrops.cc en .hh niet aangeraakt.

**Telling.** Gebouwd 7 (K2, K3, uitrust-controles, wakker worden binnen een houdtijd, K5 voor de baasteleporter, K7,
en één gevonden levensduurpad). Niet gebouwd 1: K5 voor k11 kant A, omdat de server dat verschil niet uit zijn eigen
gegevens kan afleiden (zie daar wat de quest moet meesturen).

## Werkwijze rood/groen

Alles is eerst groen gebouwd en daarna per punt rood gemeten: een mutatie die het oude gedrag terugzet, bouwen,
`build/newserv run-static-tests`, log bewaren, bron terugzetten. De mutaties zijn nooit gecommit;
`/usr/bin/grep -n "MUTATION" src/*.cc src/*.hh` geeft niets (exit 1). Logs: `build/r2-red-<naam>.log` (acht stuks,
elk exit 1), groen op dezelfde bron: `build/r2-4-tests.log` (exit 0). Een geval stopt op zijn eerste falende assertie.

## De gedeelde route: één coroutine voor elke wachtende regel

`spire_ticker_line_co` (src/SpireFlagBridge.cc:409) draagt nu elke regel die een beurt heeft in de volgorde per client
(`Client::spire_pack_line_free`): hij slaapt zijn geplande wachttijd, vraagt bij het wakker worden of de volgorde
intussen verschoven is, en vraagt pas daarna om zijn tekst. De tekst komt uit een functie die de client ziet zoals hij
op dat moment is en "" teruggeeft als de regel niet meer waar is. De coroutine houdt alleen een `weak_ptr` op de client
en op de game.

## Een regel die al wachtte en wakker wordt binnen de houdtijd van een baasdeurregel (ronde 1 "Gezien")

- Wat er was: `spire_ticker_hold` schoof alleen `next_free` op, dus alleen regels die ná de baasdeurregel gepland
  werden wachtten. Een eerste-mag-herinnering die al gepland stond ging op zijn oude moment, bovenop de deurregel.
- Nu: de hold onthoudt zijn einde per client (`spire_ticker_held_until`, src/Client.hh:238). De eerste wachtende regel
  die binnen de hold wakker wordt wacht tot de hold om is; elke regel achter hem schuift even ver mee
  (`spire_ticker_shift`, :239), zodat de volgorde en de afstanden blijven zoals gepland. `next_free` schuift mee voor
  wat later gepland wordt. Functie: `spire_ticker_wake_delay` (SpireFlagBridge.cc:2509, .hh:1042).
- De kitregel heeft zijn eigen coroutine en doet hetzelfde (:3202).
- Rood (mutatie: een wachtende regel gaat als zijn geplande wachttijd om is):
  `K2 (r2: a waiting line that wakes inside a later boss door line's hold waits on, the lines behind it follow) FAILED: failure at src/StaticTests.cc:7139: int64_t(1000) != spire_ticker_wake_delay(lane, held, shift, seen1, at(gap)).count()`
  en in de test met een echte client: `failure at src/StaticTests.cc:7548: !(*a_line >= *a_door + gap)` (de herinnering
  ging 4 s na zijn aankomst, dus 3 s na de deurregel).
- Groen: de herinnering komt als de deurregel zijn 4 s gehad heeft (`*a_line >= *a_door + gap`).

## K2: "This was the last room: the gift is not given." gaat via de wachtrij

- `spire_bridge_line_waits` (SpireFlagBridge.cc:2527) zegt welke regel wacht: 'C' 15 en 'C' 16 als voorheen, en nu ook
  de relic-gift 'C' 12 (bit 23 uit) als de schrijver in de laatste kamer is. `spire_bridge_handle_register` gebruikt
  hem (:4010). De regel gaat 4 s na aankomst en 4 s na de vorige wachtende regel; de clear-tekst (1 s na de r251)
  volgt hem dus niet meer op de hielen.
- `spire_bridge_is_pack_line` is niet veranderd: zijn bestaande assertie (StaticTests.cc, "'C' 15 and 'C' 16 come
  spaced") blijft staan.
- Rood (mutatie: alleen 'C' 15/16 wachten): `failure at src/StaticTests.cc:7189: !(spire_bridge_line_waits(0x43000C00 | gift, last))`
  en met een echte client: `failure at src/StaticTests.cc:7480: 0u != e.ch->count(not_given)` (de regel ging direct).
- Groen: niets direct, de regel komt minstens 4 s na aankomst en na de deurregel, precies één keer.
- Grens: een regel die ná die 4 s direct verstuurd wordt (de 'X'-regel van het run-einde, records) kent de volgorde
  nog niet en kan hem nog wegduwen; dat is de bestaande observatie over directe regels (299/302).
- De logregel voor een wachtende 'C'-regel zegt nu "waits its turn, N ms" in plaats van "sent" op het moment van
  plannen. lab/e2e_lab.py zoekt alleen op `message 50000000` en wordt niet geraakt.

## K3: de NO_MAG-herinnering kijkt opnieuw bij versturen

- `spire_run_mag_reminder_in_turn` (SpireFlagBridge.cc:3586) plant beide mag-herinneringen (NO_MAG :3641, eerste mag
  :3615) en kijkt bij versturen in de run-kopie: toont die een uitgeruste mag, dan gaat er niets
  (`spire_run_mag_reminder_text_now`, src/SpireEquipItem.hh:82). Zonder mag in de kopie blijft de regel staan: de
  kopie toont dan geen uitgeruste mag, en het NO_MAG van de quest is de enige bron.
- Rood (mutatie: niet opnieuw kijken): `failure at src/StaticTests.cc:7219: std::string("") != spire_run_mag_reminder_text_now(text, k2_inventory({{0x00, true}, {0x02, true}}))`
  en met een echte client: `failure at src/StaticTests.cc:7551: 0u != b.ch->count(reminder)`.
- Groen: client b rust zijn mag 1 s na het NO_MAG uit en krijgt geen regel; client a (niet uitgerust) krijgt hem wel.
  Log: `[C-3] Spire run mag: a mag was equipped while the reminder waited: no line`.

## De overige uitrust-controles via dezelfde route (ronde 1 "Gezien")

- "Equip your mag: Item Pack > Mag.", "Equip your weapon: Item Pack > Equip." en de kitwapen-hint gaan via
  `spire_equip_missing_line_in_turn` (SpireFlagBridge.cc:3146): een beurt in de volgorde, en bij versturen opnieuw
  `spire_equip_missing_text` op de kopie van dat moment.
- "Equip your gear: Item Pack > Equip." bij hervatten (:3463) neemt ook zijn beurt en kijkt opnieuw met
  `spire_resume_equip_missing_due` (:625): waar zolang er een item van het opslagpunt nog niet uitgerust is waarvoor
  newserv geen SpireEquipItem stuurt. Bij het plannen is dat dezelfde voorwaarde als voorheen.
- Rood (mutatie: direct sturen, als voor ronde 2): `failure at src/StaticTests.cc:7505: 0u != p->ch->count(equip_weapon)`
  (de regel lag bovenop de baasdeurregel). Rood (mutatie: gear-regel niet opnieuw bekijken):
  `failure at src/StaticTests.cc:7258: !(!spire_resume_equip_missing_due(ss, k2_inventory({{0x00, false}, {0x02, true}}), sent))`.
- Groen: client g krijgt de regel 4 s na de deurregel; client h rust het wapen intussen uit en krijgt niets
  (`[C-7] Spire equip: item 00010000 equipped (or gone) while its line waited: no line`).

## K5: "teleporter" waar de uitgang Sega's baasteleporter is

**Telling** (questkant 25c3a0c, script dat make_spire.py:828-832 `boss_tp` en :3471-3478 `way_bits` naloopt over
spire.json + build/spire-map.json en spire-ep2.json + build-ep2/spire-map.json):

| kamer | soort | bits | naar | Sega's TObjWarpBoss op de plek van de pijp |
|---|---|---|---|---|
| k3 | exit_pipe | 0xF3 | k4 Dragon | ja |
| k7 | exit_pipe | 0xF3 | k8 De Rol Le | ja |
| k10 | exit_pipe | 0xF3 | k11 Vol Opt | ja |
| k14 | exit_pipe | 0xF3 | k15 Dark Falz | ja |
| k17 | exit_pipe | 0xF3 | k18 Barba Ray | ja |
| k20 | exit_pipe | 0xF3 | k21 Gol Dragon | ja |
| k26 | exit_pipe | 0xF3 | k27 Gal Gryphon | nee, onze pijp (Seaside) |
| k29 | exit_pipe | 0xF3 | k30 Olga Flow | ja |
| k18, k21, k27 | exit_warp | 0xF0 | k19, k22, k28 | Sega's eigen uitgang (ronde 1) |

0xF3 gaat dus uit 8 kamers; 7 daarvan zijn de lijst van de critic, k26 is de uitzondering. De code 'C' 6 zelf zegt
het verschil niet: alle acht sturen 0xF3 met de baaskamer in de lage byte.

**De server kan het uit zijn eigen gegevens afleiden**: de kaart van de game, zoals newserv de .dat van de quest laadt.
Gemeten met `build/newserv disassemble-quest-map --gc` op de gebouwde .dat's: precies één
`ObjectSetEntry type=0019 "TObjWarpBoss"` op Ep1 vloer 2, 5, 7 en 10 (k3, k7, k10, k14) en op Ep2 vloer 2, 4 en 11
(k17, k20, k29); geen op Ep2 vloer 9 (k26). Elke kamer heeft daar zijn eigen vloer.

- Gebouwd: `spire_floor_has_boss_teleporter` (SpireFlagBridge.cc:3808) zoekt een object van type 0x0019 op de vloer
  van de schrijver; `SpireBridgeTextContext::boss_teleporter` (.hh:973) draagt het naar de tekst (:2262):
  "The way is open. Take the teleporter: Exam <boss>." (te lang of geen naam: "Exam Room <k>"). Een vloer zonder
  baasteleporter, of een game zonder kaart, houdt "Walk to the warp: Exam <boss>.".
- Rood (mutatie: de vlag niet bekijken): `failure at src/StaticTests.cc:7274: std::string("The way is open. Take the teleporter: Exam Dragon.") != spire_bridge_message_text(0x43F30604, tp_named)`.
  Groen; de test loopt alle 255 lage bytes af (geen "warp", wel "teleporter", hooguit 90 tekens) en toetst dat de
  vlag geen andere regel verandert (keuze van twee, 0xF0, één pijp van een ander type).
- Niet in een statische test: het zoeken in de kaart zelf; dat vraagt een MapState uit een quest-.dat. De test met
  een echte client toetst alleen dat een game zonder kaart "warp" houdt. De gegevens waar het op leunt zijn hierboven
  gemeten, het pad op de server niet.
- Grens: de regel leunt op "een TObjWarpBoss op de vloer is de uitgang van die kamer". Dat klopt voor alle acht
  kamers van vandaag; een toekomstige vloer met een ongebruikte TObjWarpBoss zou "teleporter" zeggen bij onze pijp.

**k11 kant A: niets gebouwd.** k11 stuurt bits 0x10 (elite << 4 | normal), dezelfde code als elke gewone keuze
normal/elite. Dat kant A daar Sega's teleporter zonder bord is (spire.json k11 `"a": {"sega_warp": "0x0160"}`) staat
niet in de code, en de kaart zegt wel dat er een object 0x0160 op de vloer staat maar niet welke kant van de keuze dat
is en dat er geen bord bij hoort.

**Wat de quest moet meesturen** (voorstel, nieuwe contractregel, dus voor de regie): bit 2 van het type-nibble van een
kant in 'C' 6 = "deze kant is Sega's eigen teleporter, zonder bord". k11 wordt dan 0x14 in plaats van 0x10; newserv
laat dan "Walk close to a warp to read it." weg voor die kant. Dezelfde bit maakt de baasuitgang exact (0xF7 in
plaats van 0xF3) en maakt de kaart-afleiding overbodig. Volgorde: eerst newserv, dan de quest; de newserv van nu
beantwoordt een type boven 3 met "The way is open. Walk to a warp.".

## K7: 'V' en 'C' 12 door spire_bridge_handle_register

De test `r2 K7` (StaticTests.cc:7298) bouwt een echte `Client` (GC, serienummer, eigen run-kopie met een wapen en een
mag) en een echte `Lobby` met een quest, en stuurt de registerwaarden door `spire_bridge_handle_register`. Wat de
client krijgt wordt per commando met tijdstip opgevangen. De test draait in een tijdelijke map (met de echte
system/spire-mag.json erin gekopieerd), zodat de savestate van de 'V' niet in system/spire-bridge van de server landt.
De toestand die een 'B' en een beantwoorde mag-ASK achterlaten wordt gezet met `spire_static_test_run_state`
(SpireFlagBridge.cc:3859), omdat die twee zelf records- en guardbestanden lezen en schrijven.

- **v_room pas na spire_savestate_write_allowed**: een 'V' zonder 'B', een 'V' met 18 van 18 waarden en een met 18
  van 19 laten kamer 0 staan; de toegestane 'V' van kamer 14 zet 14, die van kamer 15 zet 15.
  Rood (mutatie: de kamer onthouden vóór de controle): `failure at src/StaticTests.cc:7437: 0u != spire_static_test_v_room(e.c, cl)`.
- **last_clear alleen bij bit 23 = 0**: in kamer 15 geeft `spire_bridge_text_context_for` (de functie waarmee
  handle_register de context bouwt, :3828) last_clear voor 0x43000C01 en niet voor 0x43800C40, 'C' 10 en 'C' 15.
  Rood (mutatie: bit 23 niet bekijken): `failure at src/StaticTests.cc:7469: !(!spire_bridge_text_context_for(e.c, cl, QUEST, 0x43800C40, table).last_clear)`.
  Door handle_register zelf: de exam-regel van 'C' 12 gaat in de laatste kamer direct, de gift-regel wacht.
- **spire_ticker_hold vóór de NO_MAG-afhandeling**: direct na de 'V' van een baaskamer, vóór de volgende
  registerwaarde, staat de volgorde van die client al 4 s vast.
  Rood (mutatie: geen hold): `failure at src/StaticTests.cc:7463: !(e.c->spire_pack_line_free >= e_before_v + gap)`.
  Een 'V' van een gewone kamer (14) stuurt geen deurregel en houdt niets vast.

Aanroepers die niet in een statische test draaien, met de reden:

| aanroeper | waarom niet | wat wel getoetst is |
|---|---|---|
| `spire_equip_item_send` (:3406) | wacht op het B3-antwoord van de client op een functie-aanroep en heeft de client-functie-index nodig | zijn controle erna, `spire_equip_check_later` (:3367), draait met een echte client |
| `spire_resume_equip` (:3431) | pollt op de items van de questlus en roept dan `spire_equip_item_send` | de voorwaarde van zijn gear-regel (`spire_resume_equip_missing_due`), puur |
| `spire_kit_use_text_later` (:3165) | start uit het 'M' START-antwoord, dat de winkelcatalogus, meta- en recordsbestanden leest | de functie waarmee hij bij het wakker worden opnieuw kijkt, puur |
| `spire_run_mag_reminder_later` (:3602) | vraagt een beantwoorde ASK en RESULT 1 (magtabel, guardbestand) | deelt `spire_run_mag_reminder_in_turn` met het NO_MAG, dat wel draait |
| `spire_floor_has_boss_teleporter` (:3808) | vraagt een MapState uit een quest-.dat | de tekst met en zonder de vlag, puur; de gegevens gemeten op de .dat |

**Prijs:** de statische tests duren nu 5,6 s (basis b2b38f2: 0,36 s), omdat de test met de echte client de echte
4 s tussen twee regels afwacht. De test hangt niet aan ijkpunten van de wandklok: hij draait tot de laatste verwachte
regel er is (uiterlijk 12 s) en vergelijkt daarna de tijdstippen waarop de regels verstuurd zijn (commit 0626572).
Eén aanname blijft: de 'V' van client a komt binnen 4 s na zijn NO_MAG; een machine die langer dan 3 s stilvalt
maakt dat geval rood.

## Levensduur: wat er met een uitgestelde regel gebeurt

Elke uitgestelde regel houdt tijdens het wachten alleen een `weak_ptr` op de client en op de game. Bij het wakker
worden geldt overal dezelfde regel: `if (!c || !l || (c->lobby.lock() != l))` stopt. Een speler die de game verlaat
verliest zijn `c->lobby` in `Lobby::remove_client` (src/Lobby.cc:509); een verbroken verbinding loopt via
`GameServer::destroy_client` en dezelfde `remove_client`, waarna het Client-object vrijkomt.

| uitgestelde regel | houdt vast | laat los / stopt | speler verlaat de game | verbinding weg | run eindigt eerst |
|---|---|---|---|---|---|
| wachtende regel (`spire_ticker_line_co` :409): 'C' 12 laatste clear, 'C' 15, 'C' 16, mag-herinneringen, uitrust-regels, late kitregel | alleen weak_ptr (parameters :409) | :417 | niets verstuurd | niets verstuurd, client niet aangeraakt | wordt nog verstuurd zolang de speler in die game staat |
| kitregel (`spire_kit_use_text_later` :3165) | weak_ptr; `c.reset()` vóór elke wachttijd | :3170, :3182, :3197 | niets | niets | wordt nog verstuurd |
| controle na SpireEquipItem (`spire_equip_check_later` :3367) | weak_ptr | :3374 | geen controle, geen regel (zie hieronder) | idem | controle loopt, regel in zijn beurt |
| `spire_equip_item_send` (:3406) | de client zelf, zolang de functie-aanroep loopt | `c.reset()` :3419 vóór de controle | antwoord komt nog, daarna stopt de controle | de wachtende aanroep wordt afgebroken (src/GameServer.cc:152), één W-regel, de controle stopt op :3374 | loopt door |
| hervat-uitrusten (`spire_resume_equip` :3431) | weak_ptr tussen de polls | :3442, :3456, :3490 | stopt | stopt | loopt door |
| vaste vertraging buiten de volgorde (`spire_send_ticker_later_co` :388: room clear, hervat-magregel, tweede savestate-regel) | weak_ptr | :393 | niets | niets | wordt nog verstuurd |
| evolutietekst (`spire_run_mag_evolve_text_later` :3568) | weak_ptr | :3573 | niets | niets | wordt nog verstuurd |

"Run eindigt eerst": na het 'X' van het run-einde staat de speler nog 6 s in de game (`SPIRE_RUN_OVER_LOBBY_DELAY`).
Voor de gift-regels is dat de bedoeling; voor een uitrust-hint of mag-herinnering is het een regel die niets meer
uithaalt (zie "Gezien").

**Gevonden pad, gefixt met een test.** `spire_equip_item_send` las de game van de client pas ná het antwoord op de
functie-aanroep (`std::weak_ptr<Lobby> weak_l = c->lobby;`, b2b38f2 :3300). Een speler die tijdens die aanroep de game
verliet stond dan in de lobby of in een andere game, en de controle van 1 s later liep dáár: tegen het personage van
die plek, met een nieuwe run-toestand voor die plek, en met een uitrust-regel als daar toevallig een niet-uitgerust
item met hetzelfde item-id lag. Nu krijgt de coroutine de game mee waarin het uitrusten gevraagd is (:3406, uit
`spire_equip_prompt_handle` en `spire_resume_equip`), en `spire_equip_check_later` stopt als de client daar niet meer
staat.
Rood (mutatie: de game lezen als het antwoord binnen is): `failure at src/StaticTests.cc:7569: 0u != moved.ch->count(equip_weapon)`.
Groen: de client die bleef krijgt zijn regel, de client die in een andere game staat krijgt geen controle, geen regel
en geen beurt in zijn volgorde. De test dekt ook: een client die de game verlaat terwijl zijn herinnering wacht krijgt
niets, en een client die verdwijnt (laatste eigenaar los, `weak_gone.expired()`) wordt niet meer aangeraakt.

## Tests en W-regels

- `build/newserv run-static-tests` op de bron van 0626572 (build/r2-4-tests.log): exit 0, laatste regel
  `-- All static tests passed`, 0 E-regels, **10 W-regels, dezelfde tien als de basis** (negatieve gevallen van de
  guard- en savestate-tests). Geen verschil. `make`: exit 0, 0 compilerwarnings.
- Zes nieuwe gevallen in de sectie "Spire kwaliteit-2" (StaticTests.cc:7123-7578); het bestaande geval 145 kreeg de
  extra parameter van `spire_ticker_hold` en twee asserties erbij, geen assertie verwijderd of verzwakt.
- De eindbuild en de testuitvoer op de laatste commit staan in het antwoord aan de orchestrator, niet in een commit.

## Beslissingen (voor de beslis-log van de orchestrator)

- Wakker worden binnen een houdtijd: de eerste wachtende regel wacht de hold uit en alle regels achter hem schuiven
  even ver mee, in plaats van een wachtrij per client met één verzender. Reden: het bestaande mechanisme
  (`spire_pack_line_free`) en zijn tests blijven, en de opdracht vroeg "opnieuw kijken en verder wachten".
- K2: alleen de "not given"-regel van de laatste clear wacht; de gewone "Make room, it waits." blijft direct. De
  opdracht noemt die ene regel, en `spire_bridge_is_pack_line` heeft een bestaande assertie dat 'C' 12 geen
  pakketregel is.
- K3: de herinnering vervalt alleen als de kopie een uitgeruste mag toont; geen mag in de kopie laat hem staan.
- Gear-regel bij hervatten: waar zolang er een item is dat nog uitgerust moet worden en waarvoor newserv niets stuurt.
- K5: de baasteleporter uit de kaart van de game afgeleid (TObjWarpBoss op de vloer van de schrijver), geen
  questwijziging; k11 kant A niet gebouwd.
- K7: de toestand van 'B' en mag-ASK via drie functies "for StaticTests only" in SpireFlagBridge, en de test in een
  tijdelijke werkmap; de context van een bericht is een eigen functie geworden (`spire_bridge_text_context_for`).
- Levensduur: `spire_equip_item_send` krijgt de game mee waarin gevraagd is.
- De logregel van een wachtende 'C'-regel zegt "waits its turn" in plaats van "sent".

## Gezien, niet gebouwd

- ux · de gewone gift-regel "Make room, it waits." ('C' 12, niet de laatste clear) gaat nog direct en kan door de
  clear-tekst weggeduwd worden · src/SpireFlagBridge.cc:4010 · klein: `spire_bridge_line_waits` zonder `last_clear`.
- ux · een mag-herinnering of uitrust-regel die nog wacht als de run eindigt gaat nog uit zolang de speler in de game
  staat en kan de "Run over"-regel wegduwen · src/SpireFlagBridge.cc:409 · klein: het run-einde bekijken in de
  tekstfunctie van die regels.
- ux · directe regels (golf, room clear, 'C' 10, overzicht, evolutietekst na 500 ms, pack-full-melding, 'X') kennen de
  volgorde niet en kunnen een wachtende regel nog wegduwen; staat als 299 en 302 in observaties.md ·
  src/SpireFlagBridge.cc:3568 en :4015 · middel.
- tekst · k11 kant A zegt nog "Walk close to a warp to read it." voor Sega's teleporter zonder bord; vraagt een bit
  van de quest (zie K5) · quests/spire/make_spire.py:3475 · klein aan beide kanten.
- datakwaliteit · K5 leunt op "een TObjWarpBoss op de vloer is de uitgang"; dezelfde questbit (0xF7) maakt het exact ·
  src/SpireFlagBridge.cc:3808 · klein.
- test · de statische tests duren 5,6 s in plaats van 0,4 s door de echte wachttijden · src/StaticTests.cc:7298 ·
  middel: een klok die de test kan vooruitzetten.
- log · een wachtende regel die verstuurd wordt logt dat niet zelf (wel zijn planning, zijn vervallen, en het
  B0-commando op datalog-niveau); alleen de uitrust-regels loggen "sent" · src/SpireFlagBridge.cc:409 · klein.

## Commits (b2b38f2..)

- fbd40a4 fix: waiting ticker lines look again when they go
- 0626572 test: the real-client ticker test waits for its last line
- de commit met dit rapport en het voortgangsbestand (alleen de twee .md-bestanden); daarop is de eindbinary gebouwd.
