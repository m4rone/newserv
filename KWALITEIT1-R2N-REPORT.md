# Kwaliteit-1 ronde 2, newserv-fixes (critic bundel 1: B-3a, B-3b, B-6, hervat-vraag)

Tak `spike/kwaliteit-1`, worktree /home/m4rck/code/worktrees/newserv/kwaliteit-1, start 31cf3e8. Datum 2026-11-03.
Status: DONE. Alle vier punten gebouwd, elk met een StaticTest rood -> groen. Quest-worktree niet gewijzigd.

Dekking: dit dekt B-3 (a en b), B-6 en de savestate-met-open-hervat-items-vraag, elk met een test rood->groen op de
laptop. Dit dekt niet: of de GC-client op de Wii het uitrusten werkelijk overneemt, de quest-punten van ronde 2 (al
gedaan), en andere problemen buiten deze punten.

Rood is telkens een mutatie van de nieuwe functie naar het oude gedrag (bron gebouwd met de mutatie, daarna de bron
teruggezet en vergeleken met `cmp`); B-3a is rood gemeten tegen de broncode van 31cf3e8 zelf.

## B-3a: zelf uitgerust hervat-item telt als klaar (commit 65c6330)
- Vorm: `spire_resume_equip_targets` zoekt per bewaard wapen/mag eerst een exact gelijk item dat al uitgerust is; dat telt
  als klaar (geen id, niet in `*expected`). Waarom: de coroutine vergelijkt `ids.size() < expected`; zo komt er geen
  valse "Equip your gear"-regel en hoeft de coroutine zelf niet te veranderen.
- Test (src/StaticTests.cc, blok kwaliteit-1): na de 6x25 `expected == 0`; en een tweede gelijke, onuitgeruste Saber naast
  de uitgeruste: geen id, `expected == 0`.
- Rood (src van 31cf3e8, nieuwe test), `./build/newserv run-static-tests`, exit 1:
  `run-static-tests: failure at .../src/StaticTests.cc:5932: 0u != expected`
- Groen, exit 0: `-- All static tests passed`

## B-3b: vangnet als de ack van het laatste hervat-item uitblijft (commit 63e45d3)
- Vorm: pure regel `spire_resume_equip_start_due(last_item_acked, idle_ms, started, has_equipped, available)`: start bij
  de laatste ack, of na `SPIRE_RESUME_EQUIP_FALLBACK_MS` = 15 s zonder 'J'-itemverzoek. Vanaf het 'J'-startantwoord
  (hervatten, Equipped, r143 = 1) loopt `spire_resume_equip_watch` (1 s stappen, max 600) die dan
  `spire_resume_equip_start` aanroept; die rust uit wat er is en zegt "Equip your gear: Item Pack > Equip." voor wat er
  niet is. Waarom 15 s: de quest vraagt een onbeantwoord item na RS_POLLS x RS_FRAMES = 300 frames (10 s) opnieuw
  (quests/spire/make_spire.py:265, :1782), dus een lopende itemlus wacht nooit zo lang. De normale route (laatste
  ack) is ongewijzigd; eenmalig per hervatting via `resume_equip_started`.
- Test: zes gevallen (laatste ack, lus loopt nog, geen laatste ack na 15 s, al gestart, oud bestand, r143 = 0).
- Rood (mutatie: alleen `last_item_acked`, de oude regel), exit 1:
  `failure at .../src/StaticTests.cc:5950: !(spire_resume_equip_start_due(false, SPIRE_RESUME_EQUIP_FALLBACK_MS, false, true, true))`
- Groen, exit 0: `-- All static tests passed`
- Restrisico: houdt een server-draw (SRVDRAW_PENDING) de itemlus langer dan 15 s vast, dan start het vangnet midden in de
  lus; gevolg is hooguit een "Equip your gear"-regel voor items die nog komen, geen verlies.

## B-6: won 'F' van Ep1 slaat k15 zelf op vóór record_boss (commit 07ccc5f)
- Vorm: `spire_finish_boss_store_room(won, episode)` (15 voor won Ep1, anders 0); 'F' roept daarmee
  `spire_checkpoint_store(l, {c}, 15, true)` aan vóór `spire_checkpoint_record_boss`. Idempotent: een bekende kamer blijft
  zoals hij is (geen `checkpoint_new`, geen tweede uitbetaling, want `run_rooms.rooms` had hem al). De bitcheck in
  `spire_checkpoint_store` is nu `spire_checkpoint_room_first`, zodat de test dezelfde regel gebruikt. Ep2 (Olga, k30)
  bewust niet: de opdracht noemt Ep1 en Olga's tekst hangt niet van `checkpoint_new` af.
- Test (src/StaticTests.cc, bij de Run II-teksten): beide volgordes (Falz-vlag slaat k15 eerst op / vlag nooit gezien),
  daarna 'F': k15 in het checkpoint en tekst "Dark Falz beaten! Unlocked: Spire Run II."; k15 uit een eerdere run blijft
  niet-nieuw.
- Rood (mutatie: 'F' slaat niets op), exit 1:
  `failure at .../src/StaticTests.cc:1676: !(spire_finish_boss_store_room(true, 1) == 15)`
  (de test stopt bij de eerste fout; met deze mutatie faalt de volgorde "vlag nooit gezien" ook, op `cp_rooms == 0x7FFF`,
  maar die regel is niet apart gemeten.)
- Groen, exit 0: `-- All static tests passed`

## Punt 4: savestate met openstaande hervat-items — RISICO JA, gefixt in newserv (commit c4bfab0)
Meting:
- quests/spire/make_spire.py:2191 `rs_apply` start de itemlus als thread (`thread_stg t_rs_items`, :1750); de kamerstart
  daarna roept `ss_write` aan bij k > K0 (:2568, :2688, :2807; volger :4022, :4151). Een hervatting is altijd k > K0,
  dus de 'V' komt bij de start van de hervatte kamer zelf, terwijl de lus nog items maakt. quests/spire/rewards.py:1681-1690
  (`room_start_retries`) gaat er zelf al van uit dat bij room_start items open kunnen staan.
- newserv `spire_savestate_write` (31cf3e8, src/SpireFlagBridge.cc ~1820) nam `ss.items` zonder toets uit de run-kopie
  en verving het bestand. Een tweede hervatting na een crash leest dat bestand: de nog niet gemaakte items zijn weg.
  Binnen hetzelfde spel geeft de itemlus ze nog (uit `cr->savestate` in het geheugen), dus het verlies treft alleen de
  hervatting erna.
Vorm: `spire_savestate_items_owed` laat de 'V' niets schrijven zolang de hervatting items schuldig is (nog niet alle
indexen gevraagd, of het laatste item niet in de run-kopie); het bestand waaruit hervat werd blijft staan (zelfde kamer,
alle items). `spire_resume_items_watch` kijkt na het antwoord op het laatste item 10 x 500 ms of het in de kopie staat en
zet dan `resume_items_in`, zodat een later opgebruikt item de opslag niet blokkeert. `spire_resume_last_item_in`
vergelijkt tools op soort (stapelen), mags op soort (voeren), de rest op 12 data1-bytes, met aantal. Waarom newserv: daar
gebeurt het vervangen; de quest hoeft niets te weten. Prijs: bij een vol pak (P-1 houdt het item open) blijft het
bewaarpunt op de hervatte kamer tot het item binnen is: bij een crash verlies je voortgang, geen item.
- Test: zeven gevallen (1 van 3 gevraagd, alles gevraagd maar laatste niet binnen, alles binnen met gestapelde Monomates,
  eenmaal gezien en daarna opgebruikt, verse start, twee gelijke Sabers waarvan één binnen).
- Rood (mutatie: 'V' slaat altijd op, het oude gedrag), exit 1:
  `failure at .../src/StaticTests.cc:5987: !(spire_savestate_items_owed(true, 3, 1, false, false))`
- Groen, exit 0: `-- All static tests passed`
- Logregel op de pc: `Spire savestate: 'V' for serial ... not stored: the resume still owes items (x of n asked); the
  savestate it resumed from stays`, en `Spire savestate: the last of n resume item(s) is in the run copy; 'V' stores again`.

## Build en tests
`unlink src/Revision.cc`, `make -C build -j16 newserv` (nohup + wacht-op.js): exit 0. `src/Revision.cc`:
`GIT_REVISION_HASH = "c4bfab0"` = HEAD van de code. `./build/newserv run-static-tests`: exit 0, `-- All static tests passed`.

## Commits
- newserv (spike/kwaliteit-1): 65c6330 (B-3a), 63e45d3 (B-3b), 07ccc5f (B-6), c4bfab0 (punt 4), plus de docs-commit van
  dit rapport en het voortgangsbestand.
- quest (spike/kwaliteit-1): geen; HEAD blijft e992034.

## Niet gebouwd, gezien
- Ep2 (Olga, k30) heeft hetzelfde volgorde-patroon voor checkpoint en uitbetaling bij 'F', maar geen tekst die ervan
  afhangt; niet gebouwd (buiten B-6 zoals opgedragen).
