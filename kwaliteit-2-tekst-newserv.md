# Kwaliteit 2 (tekst en ux), newserv-kant: regels 145, 197, 202, 258, 275, 276, 51

Tak spike/kwaliteit-2, start 4f63e70. Bron: kwaliteit-1-inventaris-{1,3,4}.md (koppen `### <nr> ·`) en observaties.md.
Questkant gelezen op pso-roguelike spike/kwaliteit-2 53ef638 (alleen lezen).

**Dekking.** Dit dekt de zeven genoemde regels aan de serverkant, elk met een StaticTest rood naar groen op de laptop.
Dit dekt niet: questteksten (regels 35, 58, 160, 165, 189, 194, 253, 259, 261 lopen parallel), balans, coop, en wat
alleen de Wii beslist (of een tickerregel op de echte client zichtbaar blijft). De handlers en coroutines zelf draaien
niet in de statische tests: getoetst zijn de pure helpers die de volgorde en de tekst bepalen; de bedrading is gelezen
en met grep nageteld. Niets gemeten in een draaiende server, in het lab of op de Wii.

**Telling.** Gebouwd 6 (145, 197, 202, 258, 275, 276) · al opgelost 0 · geblokkeerd 1 (51, questkant).

## Werkwijze rood/groen

Per regel eerst de test (en waar een nieuwe helper nodig is: die helper met een `// MUTATION` die het oude gedrag
nabootst), build, `build/newserv run-static-tests` rood; dan de fix, build, groen; dan de commit. Bij 197/202 en bij de
tekstassertie van 258 is het rood gemeten op de ongewijzigde oude code, zonder mutatie. De sectie
"Spire kwaliteit-2" in StaticTests.cc draait elk geval en logt elk falend geval apart. Geen `MUTATION` meer in de bron:
`/usr/bin/grep -n "MUTATION" src/*.cc src/*.hh` geeft niets (exit 1).

## 197 en 202 — wachtpunt 6 bij Sega's teleporter (commit d7e5e17)

- Hermeting (testlog vóór de fix, build/g1-red.log):
  `K2: 'C' 6 of k18 reads "The way is open. Walk to the warp: Room 19 normal."`
  `K2: 'C' 6 of k21 reads "The way is open. Walk to the warp: Room 22 normal."`
  `K2: 'C' 6 of k27 reads "The way is open. Walk to the warp: Room 28 normal."`
  Er staat geen "pipe" meer, maar wel de woorden van een eigen pijp (de code noemde het zelf "the two-pipe words").
  De regel gold dus nog: geen woord over de teleporter, en een kamertype waar niets te kiezen valt.
- Welke kamers: de formule van de quest (make_spire.py:3434-3436 op 53ef638) over beide specs geeft 0xF0 precies voor
  de exit_warp-kamers: `k18 exit_warp -> 0xF0, low byte 19; k21 exit_warp -> 0xF0, low byte 22; k27 exit_warp -> 0xF0,
  low byte 28`; elke exit_pipe (k3, k7, k10, k14, k17, k20, k26, k29) geeft 0xF3.
- Gewijzigd: src/SpireFlagBridge.cc:2207-2212 (`spire_text_way_open`, tak `type_a == 0`): "The way is open. Take the
  teleporter: Room <k>." · src/SpireFlagBridge.hh:111-112 (contractcommentaar) · geen questwijziging nodig.
- Bestaande assertie aangepast: StaticTests.cc:6262 toetste de vervangen tekst ("Walk to the warp: Room 3 normal.");
  die regel toetst nu de nieuwe tekst, plus een nieuwe assertie dat één pijp van een ander type (0xF2) zijn woorden houdt.
- Rood (exit 1):
  `K2 (197/202: 'C' 6 at Sega's own teleporter out of a boss arena names the teleporter) FAILED: failure at .../src/StaticTests.cc:6942: std::string("The way is open. Take the teleporter: Room 19.") != spire_bridge_message_text(0x43F00613)`
- Groen (exit 0): `K2: 'C' 6 of k18 reads "The way is open. Take the teleporter: Room 19."` (k21: Room 22, k27: Room 28),
  `-- All static tests passed`. De test loopt ook alle 255 lage bytes af: geen "warp", geen "normal", hooguit 90 tekens.

## 258 — "it waits" op de laatste clear (commit a9a50d0)

- Hermeting, wat er werkelijk met de gift gebeurt (pso-roguelike 53ef638):
  `git show 53ef638:quests/spire/rewards.py | grep -n ...` → `1695: s.call(gw_retry)` (rs_retries, onderdeel van
  room_start), `1715: s.call(gw_retry)` (room_start), en de run_end-hook `1701-1704`: alleen `s.call(ex_retry)` en
  `s.call(rr_retry)`. De room_clear-hook draait op elke clear, ook de baas (make_spire.py:2755, vóór `if room.get('boss')`).
  Gevolg: een relic-gift die op de laatste clear niet past telt zijn teller op, meldt 'C' 12 "it waits", en wordt
  daarna nooit meer gemaakt; er komt geen kamerstart meer en run_end probeert alleen de exam-rare en de exam-extra.
- De server weet de laatste kamer zonder questwijziging: elke run- en kamerquest speelt door tot de eindbaas van zijn
  episode (spire.json k1-k15, spire-ep2.json k16-k30; `spire_meta_final_boss_room`), en de kamer van de schrijver
  staat in zijn laatste 'V' (kamerstart).
- Gewijzigd: src/SpireFlagBridge.cc:1019 (`SpireClientRun::v_room`), :1869 (gezet bij de 'V'), :2368-2376 (tekst,
  'C' 12 met bit 23 uit), :2413-2415 (`spire_gift_last_clear`), :3814-3818 (context bij het bericht) ·
  src/SpireFlagBridge.hh:961 (`SpireBridgeTextContext::last_clear`), :964-970.
  Tekst: "Hoarder: inventory full. This was the last room: the gift is not given." (zonder naam: "Inventory full. This
  was the last room: the gift is not given."). Elke andere clear houdt "Make room, it waits."
- Rood (exit 1; tekst op de oude code, helper met mutatie `return false`):
  `K2 (258: a relic gift that does not fit at the quest's last clear does not say it waits) FAILED: failure at .../src/StaticTests.cc:6961: std::string("Hoarder: inventory full. This was the last room: the gift is not given.") != spire_bridge_message_text(0x43000C01, last)`
- Groen (exit 0). De test toetst ook: geen "waits" voor elke lage byte, hooguit 90 tekens, de exam-regels ('C' 10 en
  'C' 12 met bit 23) ongewijzigd, en `spire_gift_last_clear` alleen waar voor (1, 15) en (2, 30).
- Grens: een quest die eerder eindigt dan de eindbaas van de episode (een labvariant), of een laatste kamer waarvan de
  'V' niet aankwam, houdt "it waits". Exact wordt het pas met een bit van de quest; dat is geen blocker voor deze regel.

## 145 — de mag-herinnering duwt de baasdeurregel niet meer weg (commit b73a95d)

- Hermeting op 4f63e70: beide herinneringen gingen direct:
  `:3406 spire_send_ticker(c, SPIRE_RUN_MAG_REMINDER_TEXT);` (NO_MAG) en
  `:3443 spire_send_ticker(c, spire_run_mag_reminder_text(offered_shell));` (eerste mag). De botsing uit de critic
  (routeb2-S-critic.md:89-91) is de NO_MAG-herinnering: de quest stuurt bij een baaskamerstart 'V' (server:
  "Boss ahead: you are buffed") en in dezelfde frame de 'Z' NO_MAG van zijn room_start-hook (make_spire.py:1962).
- Gewijzigd: src/SpireFlagBridge.cc:3521-3530 (NO_MAG) en de eerste-mag-tak (:3565) gaan via `spire_send_ticker_later` met
  `spire_run_mag_reminder_delay` (:2429, de afstand van een pack line: 4 s na aankomst, 4 s na de vorige wachtende
  regel) · :1925-1926 de baasdeurregel houdt die volgorde 4 s vast (`spire_ticker_hold`, :2441) ·
  src/SpireFlagBridge.hh:981-994 · src/Client.hh:232-234 (commentaar bij `spire_pack_line_free`).
- De volgorde in de test (StaticTests.cc:6988): baasdeurregel op t0, herinnering komt 20 ms later binnen en gaat 4 s
  na aankomst; een 'C' 16 van dezelfde kamerstart volgt 4 s daarna; twee herinneringen na elkaar 4 s uit elkaar;
  `spire_ticker_hold` schuift nooit terug.
- Rood (exit 1, mutatie: vertraging 0 en geen hold):
  `K2 (145: the mag reminder waits its turn after the boss door line) FAILED: failure at .../src/StaticTests.cc:6997: !(arrives + reminder >= t0 + gap)`
- Groen (exit 0). Bedrading nageteld:
  `/usr/bin/grep -n "spire_send_ticker(c, SPIRE_RUN_MAG_REMINDER_TEXT\|spire_send_ticker(c, spire_run_mag_reminder_text" src/SpireFlagBridge.cc` → geen uitvoer (exit 1).

## 276 — geen eerste-mag-herinnering als newserv de mag uitrust (commit 2bbccaf)

- Hermeting op 4f63e70: de eerste-mag-tak keek niet naar `mag_c14_seen` (alleen de evolutietak deed dat), terwijl de
  quest na élke RESULT 1 een 'C' 14 stuurt (make_spire.py:1936-1939), ook na de eerste mag.
- Gewijzigd: src/SpireMag.cc:540-542 en SpireMag.hh:336-339 (`spire_run_mag_reminder_text_take`: leeg als de 'C' 14
  kwam en newserv SpireEquipItem stuurde) · src/SpireFlagBridge.cc:3485-3503 (`spire_run_mag_reminder_later`: wacht
  `SPIRE_RUN_MAG_EVOLVE_TEXT_DELAY` (500 ms) op de 'C' 14, zoals de evolutietekst; daarna de 145-route) · :3563-3573 (de
  markering wordt nu bij elke RESULT 1 gewist, de eerste mag start de coroutine).
  Weigert het spel de equip stil, dan zegt de bestaande controle na 1 s "Equip your mag: Item Pack > Mag."
- Rood (exit 1, mutatie: de 'C' 14 niet bekeken):
  `K2 (276: the first mag's reminder is not sent when newserv equips that mag ('C' 14)) FAILED: failure at .../src/StaticTests.cc:7024: !(spire_run_mag_reminder_text_take(equipped, false).empty())`
- Groen (exit 0). Ook getoetst: 'C' 14 zonder equip (r143 = 0 of geen mag in de kopie), een oude quest zonder 'C' 14 en
  een 'C' 13 laten de herinnering staan.

## 275 — kitregel eerst, de uitrust-hint vier seconden erna (commit be19ed9)

- Hermeting op 4f63e70: de hint ging direct uit de controle (`:3225 spire_send_ticker(c, *text);`, 1 s na de
  SpireEquipItem van de 'C' 13), de kitregel 3 s na het 'M' START-antwoord (`:3049`) of direct bij een late 'C' 13
  (`:3328`). Geen van de drie keek naar de ander, dus ze konden in beide volgordes binnen een à twee seconden vallen.
- Vaste volgorde: kitregel eerst, hint daarna. Komt de hint terwijl de kit-ticker nog onderweg is, dan wordt hij
  vastgehouden tot die ticker eindigt (met of zonder regel). Kitregel, late kitregel en hint nemen elk hun beurt in
  dezelfde volgorde per client als de mag-herinneringen en pack lines (4 s tussen twee regels; direct als er niets
  wacht). De hint kijkt op het moment van verzenden opnieuw of het wapen nog onuitgerust is.
- Gewijzigd: src/SpireMag.hh:303-305 (staat), :317-329 en src/SpireMag.cc:511-528 (`spire_kit_ticker_starts`,
  `spire_kit_equip_hint_due`, `spire_kit_ticker_ends`) · src/SpireFlagBridge.cc:2434-2439 (`spire_ticker_turn_delay`),
  :3054-3084 (`spire_equip_kit_hint_later`, `spire_equip_kit_hint_in_turn`), :3112-3142 (kit-ticker neemt zijn beurt en
  geeft de vastgehouden hint vrij), :3273 (start bij 'M' START), :3310-3325 (controle), :3423-3428 (late kitregel) ·
  src/SpireFlagBridge.hh:988-991.
- Rood (exit 1, mutatie: hint altijd meteen, niets vastgehouden, beurt 0 ms):
  `K2 (275: the kit line first, the kit weapon's equip hint a gap after it) FAILED: failure at .../src/StaticTests.cc:7052: !(!spire_kit_equip_hint_due(st, 0x00210017))`
  Het geval stopt op zijn eerste falende assertie; de latere asserties zijn in de rode run dus niet bereikt.
- Groen (exit 0). Zes scenario's: hint vóór de kitregel (vastgehouden, 4 s na de kitregel), hint 200 ms na de kitregel
  (3,8 s wachten), trage leider (oude kitregel, late regel, hint: telkens 4 s), geen kit-ticker (meteen), kit-ticker
  zonder regel (meteen vrij), en de eerste-mag-herinnering in dezelfde volgorde (herinnering, kitregel, hint).
- Gevolg om te kennen: wacht er al een mag-herinnering, dan komt de kitregel later dan 3 s na de start (in de test
  6 s na zijn eigen moment). Dat is de prijs van "geen regel duwt een andere weg".

## 51 — drie bedragen zonder bron: niets gebouwd, questkant

- Meting, welke serverregels een bedrag of een van de drie bronnen noemen:
  `/usr/bin/grep -n -i '"[^"]*\(meseta\|purse\|clean room\|room bonus\)[^"]*"' src/SpireFlagBridge.cc ... | grep -v log`
  → alleen `Exam reward: {} meseta` (twee vormen) en opslagvelden; geen regel voor kamerbonus, Purse of Clean Room.
- De drie betalingen zijn alle drie `pl_add_meseta` in de quest, zonder code naar de server op dat punt
  (53ef638): Clean Room rewards.py:1764 (room_clear), Purse rewards.py:1323 (de gekozen beloning), kamerbonus
  economy.py:881 (pauze B; rente :838). In economy.py staat geen enkele `sync_register`.
  Of de client zelf een meseta-subcommando stuurt bij `pl_add_meseta` is niet gemeten.
- De server kan de bedragen dus niet benoemen zonder dat de quest ze meldt. **Blocker (questkant):** de quest moet
  per bron of één keer bij pauze B een code met de bedragen sturen; pas dan kan newserv één regel maken met de drie
  bronnen bij naam. Dat is een nieuwe code in het contract, dus een keuze voor de regie; hier niets gebouwd.

## Eindbuild en tests

- `unlink src/Revision.cc`, `make -C build -j6` (nohup + wacht-op.js): exit 0, 0 compilerwarnings
  (`/usr/bin/grep -c -i warning build/eind.make.log` → 0).
- `build/newserv help 2>&1 | head -1` → `newserv-4ed85f2 built 2026-11-11 08:20:57.000000 UTC`;
  `git rev-parse HEAD` → `4ed85f23adbe3a5a5eb4ceec1fc924f7a8fe46bc` (laatste codecommit; de commit met dit rapport
  volgt erna en raakt alleen de twee .md-bestanden, zie het antwoord aan de orchestrator voor de hermeting op die sha).
- `build/newserv run-static-tests` → exit 0, 13894 regels, laatste regel `-- All static tests passed`, 0 E-regels.
  10 W-regels, dezelfde tien als op de basis 4f63e70 en in kwaliteit-1: negatieve gevallen van de guard- en
  savestate-tests (onleesbare Overlay/RunCopyPrint, lean 258/3/-1). De 152 regels met "skipping" zijn de
  ItemCreator-melding "Special multiplier is zero", geen overgeslagen tests.
- src/SpireDrops.cc en src/SpireDrops.hh niet aangeraakt (`git diff 4f63e70 HEAD --stat -- src/SpireDrops.cc
  src/SpireDrops.hh` → leeg).
- Geen ssh naar de pc, geen draaiende newserv aangeraakt, geen labrun, geen Dolphin, geen push.

## Commits (4f63e70..)

- d7e5e17 fix: name Sega's teleporter in the way-open line of a boss arena exit (197, 202)
- a9a50d0 fix: a relic gift at a quest's last clear no longer says it waits (258)
- b73a95d fix: the mag reminder waits its turn after the boss door line (145)
- 2bbccaf fix: no first-mag reminder when newserv equips that mag (276)
- be19ed9 fix: the kit line first, the kit weapon's equip hint a gap after it (275)
- 4ed85f2 docs: name every line that waits in the per-client ticker order

## Beslissingen (voor de beslis-log van de orchestrator)

- 145: ook de NO_MAG-herinnering omgeleid, niet alleen de in de opdracht genoemde eerste-mag-plek. De botsing uit de
  critic is juist de NO_MAG (zelfde frame als de 'V'); alleen de genoemde plek omleiden had de baasdeurregel niet
  beschermd.
- 145: de baasdeurregel houdt de volgorde 4 s vast, zodat ook een regel die zijn beurt direct mag nemen (kitregel,
  hint) hem niet wegduwt.
- 197/202: tekst "The way is open. Take the teleporter: Room <k>." op bits 0xF0; "teleporter" is het woord dat de
  Ep2-questteksten al gebruiken, het kamertype valt weg omdat er niets te kiezen is.
- 258: serverkant via de laatste 'V' en de eindbaaskamer van de episode, geen questwijziging; tekst zoals hierboven.
- 275: kitregel vóór hint; één volgorde per client voor alle wachtende serverregels (het bestaande
  `Client::spire_pack_line_free`), geen tweede mechanisme ernaast.
- 276: 500 ms wachten op de 'C' 14, dezelfde wachttijd als de evolutietekst.
- 51: niets gebouwd.

## Gezien, niet gebouwd

- ux · de andere uitrust-controles ("Equip your mag: Item Pack > Mag.", "Equip your weapon: ...", en "Equip your
  gear: ..." bij hervatten) gaan nog direct en kunnen de kitregel of een andere regel wegduwen of zelf weggeduwd
  worden · src/SpireFlagBridge.cc:3319-3320 en :3372 · klein: dezelfde beurt-route.
- ux · een wachtende regel die al ingepland stond kan nog op een baasdeurregel vallen die daarna binnen 4 s komt
  (eerste-mag-herinnering bij de start van een baaskamerquest, dan de 'V'); `spire_ticker_hold` ordent alleen wat
  ná hem gepland wordt · src/SpireFlagBridge.cc:1926 · middel: de uitgestelde verzender bij het wakker worden
  opnieuw laten kijken.
- ux · directe regels (golf, room clear, overzicht, 'C' 10) kennen de volgorde niet en kunnen een wachtende regel
  nog wegduwen; staat al als regels 299 en 302 in observaties.md · src/SpireFlagBridge.cc:3831 en :3840 · middel.
- tekst · vermoeden, niet gemeten tegen de gebouwde kaart: 'C' 6 met 0xF3 zegt "Walk to the warp: Exam <boss>"
  terwijl daar volgens make_spire.py:796-800 (boss_tp) bij de meeste baaskamers Sega's baasteleporter staat ·
  src/SpireFlagBridge.cc:2204 · klein, na een telling in build/spire-map.json.
- tekst · op het laatste examen zegt 'C' 10 "Inventory full: make room, it waits." en 'C' 12 (bit 23) "Still
  waiting"; dat klopt tot run_end, waar het item nog één keer geprobeerd wordt op het moment dat de run eindigt ·
  src/SpireFlagBridge.cc:2279 en :2301 · klein.
- datakwaliteit · 258 leunt op "de laatste kamer is de eindbaas van de episode"; een bit van de quest in 'C' 12 zou
  het voor elke questvorm exact maken · quests/spire/rewards.py gw_tell (53ef638:1882) · klein, questkant.
- git · de commit-hook meldt dat voor deze newserv-worktree geen integratiebranch en geen uitrol zijn vastgelegd
  (`.claude/omgeving.json` of `CLAUDE.md § Git flow`); alleen een waarschuwing, niets geweigerd, niet door mij ingevuld.
