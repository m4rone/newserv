# Kwaliteit 3, bundel 3 (samenspel en robuustheid): de newserv-kant

Tak `spike/kwaliteit-2`, start 9e40c01. Geschreven 2026-11-11. Alle getallen hieronder zijn op die dag gemeten in deze
worktree, tenzij er een andere datum bij staat.

## Voortgang

| Punt | Stand | Commit |
|---|---|---|
| 1 ontgrendeling en score bij een verloren run | gebouwd, rood en groen gemeten | 87c7cbc |
| 3 rustkamers in het bewaarpunt | gebouwd, rood en groen gemeten | 87c7cbc |
| 2 startkamer in het hervat-antwoord | gebouwd, rood en groen gemeten | 87c7cbc |
| 4 teleporter-bit in 'C' 6 | gebouwd, rood en groen gemeten | 87c7cbc |
| 5 test op een echte kaart | gebouwd, rood (mutatie) en groen gemeten | 87c7cbc |
| meting 79 (harde serverstop) | beschreven, niets gebouwd | - |
| meting 128 (EXP tweede speler) | beschreven, niets gebouwd | - |
| 6 testklok | niet gebouwd, reden hieronder | - |
| 6 testhaken ruimen hun sleutels op | gebouwd, rood en groen gemeten | 87c7cbc |

De punten 1 tot en met 5 en de opruimende testhaak staan in één commit: ze raken dezelfde functies in
`src/SpireFlagBridge.cc` en dezelfde testopzet in `src/StaticTests.cc`. Commit ea605f2 scherpt punt 1 aan: bit 22 van
'F' laat alleen de score met rust, de examens blijven die van de server. De regelnummers in dit rapport zijn die van
ea605f2.

## Dekking

Dit dekt: de zes bouwpunten aan de serverkant met statische tests, en twee metingen. Niet gedekt: de questkant van de
contracten (volgende stap), wat de client toont (alleen de Wii beslist dat), en balans. De huidige quest (d6fc2ef)
blijft ongewijzigd werken; wat er voor de speler verandert zonder questwijziging staat bij punt 1.

## Statische tests

- Basis 9e40c01: 14015 regels, exit 0, slotregel `-- All static tests passed`, tien W-regels, 5,6 s.
- Na ea605f2 (werkboom vóór de commit gemeten, daarna opnieuw op de eindbuild): exit 0, slotregel `-- All static tests passed`, tien W-regels (dezelfde tien: zeven van de
  guard-bestandstest, drie van de lean-test van het run-mag-register). Geen nieuwe W-regel.
- Nieuw blok `-- Spire kwaliteit-3` met zeven cases (`src/StaticTests.cc:7585-8156`).
- Eén bestaande assertie is bewust veranderd (`src/StaticTests.cc`, B8-case "'C' 6 names both pipes"): `0x43050605`
  gold als "type bestaat niet" en gaf de terugvaltekst. Type 5 is nu "elite achter Sega's teleporter" (punt 4). De
  assertie toetst nu type 8 (`0x43080605`), dat nog steeds niet bestaat. De terugval zelf is dus nog gedekt.
- Rood gemeten in één build met vijf mutaties tegelijk (elke mutatie zet één fix terug naar het gedrag van 9e40c01):
  log in `build/k3-red.log`. De rode regels staan per punt hieronder.

## 1. Ontgrendeling en score bij een verloren run

### Gemeten: wat een run nu zet (9e40c01) en wat hij na de fix zet

Gemeten met een echte `Client` in een echte `Lobby`, door `spire_bridge_handle_register`: 'B', de 'V' van elke
kamerstart, 'K' en 'F', in een tijdelijke map met de echte `system/spire-shop.json`. De waarden van 'K' en 'F' zijn
wat de quest van d6fc2ef stuurt (score.py:203-227: r169 = k - 1, examens 1 vanaf 4 en 2 vanaf 8; 10 kills, geen levens,
geen meseta, 3000 s). "Nu" is de build met de fix uitgezet (`seen.known = false`), dat is het pad van 9e40c01.
Vlaggen op Normal.

| Run | Quest stuurt | Nu: vlaggen | Nu: beste score | Na de fix: vlaggen | Na de fix: beste score |
|---|---|---|---|---|---|
| Ep1 q921 verloren in k1 | 'F' 46000BB8, score 1000 | 0380 | 1000 | 0380 | 1000 |
| Ep1 q921 verloren in k5 | 46010BB8, 4500 | 0380 0381 0384 0385 | 4500 | gelijk | 4500 |
| Ep1 q921 verloren in k9 | 46020BB8, 8000 | 0380 0381 0382 0384 0385 0386 0387 | 8000 | gelijk | 8000 |
| Ep1 q921 gewonnen, niemand dood | 46860BB8, 11500 | 0380-0388, 038B, 03A0, 03A1, 03A2 | 11500 | gelijk | 11500 |
| Ep2 q922 verloren in k16 | 46020BB8, 11500 | 0380 0381 0382 0384 0385 0386 0387 | 11500 | 0380 | 1000 |
| Ep2 q922 verloren in k19 | 46020BB8, 13000 | 0380 0381 0382 0384 0385 0386 0387 | 13000 | 0380 0381 0384 0385 | 4000 |
| Ep2 q922 verloren in k22 | 46020BB8, 14500 | 0380 0381 0382 0384 0385 0386 0387 | 14500 | gelijk | 7000 |
| Ep2 q922 gewonnen, niemand dood | 46860BB8, 19000 | 0380-0388, 038C, 03A0, 03A1, 03A2 | 19000 | gelijk | 11500 |
| Ep2 q922 hervat in k20, daar verloren | 46020BB8, 13500 | (niet apart gemeten op 9e40c01) | - | 0380 0381 0384 0385 | 4500 |

Overige records in alle rijen, voor en na gelijk: Runs 1; Won 0 bij verloren en 1 bij gewonnen; beste tijd 0 bij
verloren en 3000 bij gewonnen. Splinters 13 bij de gewonnen Ep1-run (de 'F' slaat k15 zelf op) en 0 in de andere
rijen: deze test stuurt geen kamer-clears, dus de splinters per kamer zijn hier niet gemeten en door deze fix niet
veranderd.

Wat de meting bevestigt en weerlegt:

- Bevestigd: een verloren Ep2-run in zijn eerste kamer zette Dragon (0381), De Rol Le (0382) en alle vier pacts
  (0384-0387), en kreeg 10500 punten voor 13 kamers en 2 examens die niet gespeeld zijn (11500 tegen 1000).
- Weerlegd (critic bundel 2, P1): de uitbetaling 03A0 gaat bij een verloren run niet open. `spire_meta_apply_finish`
  slaat de uitbetaalvlaggen over zolang de run niet als gewonnen is geteld (`src/SpireMeta.cc:434`).
- Nieuw gezien: ook een gewonnen Ep2-run kreeg 7500 punten voor de 15 kamers vóór k16 (19000 tegen 11500). Zijn
  vlaggen waren en blijven terecht.

### Gebouwd

- `spire_meta_exams_passed` (`src/SpireMeta.cc:798`): de examens van een run zijn de baaskamers van zijn episode,
  vanaf zijn eerste kamer, die hij achter zich liet (de run startte de kamer erna; dezelfde regel als de bestaande
  "baas verslagen" van B-AF-1). Ep1: Dragon k4, De Rol Le k8, Vol Opt k11, Dark Falz k15. Ep2: Barba Ray k18, Gol
  Dragon k21, Gal Gryphon k27, Olga Flow k30. Een gewonnen run heeft ze allemaal.
- `spire_run_reached_room` (`src/SpireFlagBridge.cc:7045`): de kamer die de client bereikte is de hoogste van de eerste
  kamer van de run, de bewaarde kamer bij hervatten, de kamer van zijn laatste 'V' en de kamers waarvan de server in
  deze game een clear zag.
- `spire_finish_seen` (`src/SpireFlagBridge.cc:7050`, aangeroepen in de 'F'-afhandeling op `:3092`):
  - Run vanaf kamer 1 (Ep1): de examens van de quest of die van de server, de hoogste van de twee, en de score zoals
    gestuurd. Zo'n run kan niets verliezen, ook niet als de server geen enkele kamer zag.
  - Run vanaf een latere kamer (Ep2, k16): de examens die de server zag (hoogstens 2; gewonnen blijft 2), en de
    score min 500 per kamer vóór de eerste kamer en min 1500 per examen dat niet gehaald is (de baaskamer die wel
    gespeeld is telt dan als gewone kamer), maal de pact-factor (4 + pacts) / 4. Exact, want elke term is een
    veelvoud van 4.
  - De tweede trap is per episode de tweede baas: in Ep2 gaan Iron man en Haste open na Barba Ray, Bare en Hungry na
    Gol Dragon.
  - Bit 22 van 'F' (de quest telde zelf vanaf de eerste kamer) zet alleen de scorecorrectie uit. De examens van
    een run die na kamer 1 begint blijven die van de server, met of zonder het bit.
  - De oude splinterregel (zonder Payout-blok) telt zijn bazen nu ook uit de gecorrigeerde 'F'.
- Rood: `K3 (1: what a lost and a won run of Episode 1 and 2 set ...) FAILED: ... 0u != wrong`, met de vier
  Ep2-regels uit de tabel hierboven als `(EXPECTED ...)`. Groen: alle acht regels kloppen.

### Wat de speler merkt zolang de quest d6fc2ef is

- Het eindscherm van Spire Run II toont nog de score van de quest. De server bewaart een lagere beste score (7500
  maal de pact-factor lager, of meer). De regel "New best score: N" in de lobby noemt het servergetal.
- De slottekst van de pacts in Spire Run II zegt "It unlocks when a run ends (not a practice run)"
  (score.py:100 op d6fc2ef). Dat is na deze fix onwaar: een run die in k16 of k17 strandt ontgrendelt niets.
- In Ep1 gaat de Dragon-trap nu ook open als er een rustkamer vóór de Dragon lag (de quest telde dan 3 kamers en geen
  examen). Dat is erbij, er gaat niets af.

## 3. Rustkamers in het bewaarpunt

- De 'V' mag in zijn tweede waarde (de variant) de rustbits meesturen: r131 & 0x3F in plaats van r131 & 3.
  `spire_savestate_take_rest_rooms` (`src/SpireFlagBridge.cc:879`, aangeroepen op `:1954`) haalt bits 2-5 eruit. In
  `Values` blijft de variant 0-2 staan zoals altijd; de vier rustbits gaan naar het eigen veld `RestRooms` (0-15).
- `RestRooms` wordt alleen geschreven als het niet 0 is (`:761`). Een bestand zonder het veld laadt zoals voorheen en
  betekent "geen rustkamer". Een waarde die geen geheel getal van 0 tot 15 is telt als geen; de run hervat dan toch.
- Omgekeerd leest een oudere newserv een nieuw bestand ook: de sleutel is extra en `Values` heeft de oude vorm.
- Test (`src/StaticTests.cc:7949`): een letterlijk bestand in het formaat van 9e40c01 laadt, hervat op dezelfde kamer
  en wordt zonder nieuwe sleutel teruggeschreven; een bewaarpunt met rustbits gaat naar het bestand en terug. De test
  met de echte client (`:8029`) stuurt een 'V' met rustbits en leest het bestand van schijf.
- Rood: `K3 (2 and 3: ...) FAILED: ... 5 != ss.rest_rooms` (mutatie: het veld niet schrijven).

## 2. Startkamer in het hervat-antwoord

- `spire_resume_answer_value` (`src/SpireFlagBridge.cc:888`, verstuurd op `:2218`): r41 = 'J' << 24 | eerste kamer van
  de run << 16 | rustbits << 12 | 0x100 | aantal items. Een vers antwoord blijft alleen de tag.
- De huidige quest negeert de nieuwe bits. Gelezen in make_spire.py op d6fc2ef: de wachtlus vergelijkt
  `r41 & 0xFF000000` met de tag (:1782-1785), kopieert `r41 & 0xFFFF` naar RS_ON (:1790-1791), en RS_ON wordt alleen
  gebruikt als "niet 0" (:1733, :1802, :1891, :2089, :3716; score.py:400, :606) en als `& 0xFF`, het aantal items
  (:1841-1842, :1892-1893, :1943-1944). Bits 16-23 komen dus nooit in RS_ON, en bits 12-15 raken geen van beide
  lezingen. De test toetst dat voor elke rustwaarde en vijf startkamers.
- Rood: `K3 (3: ...) FAILED: ... 0x4A100100u != spire_resume_answer_value(old_ss)` (mutatie: het oude antwoord).
- Groen in de test met de echte client: het antwoord is `0x4A105101` (start 16, rustbits 5, 1 item), en de hervatte
  run die in k20 strandt krijgt de vlaggen van Barba Ray en score 4500.

## 4. Teleporter-bit in 'C' 6

- Bit 2 van het type van een kant (`SPIRE_WAY_SEGA_TELEPORTER`, `src/SpireFlagBridge.hh:1049`; tekst in
  `spire_text_way_open`, `src/SpireFlagBridge.cc:2284`): die kant is Sega's eigen teleporter, zonder bord.
  - `0x14` (k11): "The way is open: teleporter to Room 12 normal, warp to Room 12 ELITE."
  - `0xF7` (baasuitgang): "The way is open. Take the teleporter: Exam <boss>.", ongeacht de kaart.
  - Beide kanten een teleporter: "The way is open: Room k A or Room k B. Take a teleporter."
- Zonder het bit is elke regel die van voorheen; de test loopt alle bitcombinaties af en vergelijkt de regels zonder
  bit letterlijk met de oude vorm. `0xF3` blijft de kaart volgen.
- Rood: `K3 (4: ...) FAILED: ... "The way is open: teleporter to Room 12 normal, warp to Room 12 ELITE." !=
  spire_bridge_message_text(0x4314060C)`.

## 5. Test op een echte kaart

- `spire_map_has_boss_teleporter` (`src/SpireFlagBridge.cc:3882`) is uit de bestaande functie gehaald en geëxporteerd.
- De test (`src/StaticTests.cc:8128`) laadt `tests/spire-gc.dat` in een `SuperMap` en een `MapState`: 805 objecten,
  Sega's baasteleporter op vloer 2, 5, 7 en 10 (k3, k7, k10, k14) en op geen andere van de 18 vloeren.
- `tests/spire-gc.dat` is een kopie van de gebouwde kaart uit de quest-worktree van 2026-11-11 13:17 (25125 bytes,
  sha256 bd1abb98...). De kaart staat niet in git van de questrepo, dus hij is niet aan een questcommit te koppelen.
- Rood: `K3 (5: ...) FAILED: ... (floor == 2) || (floor == 5) || (floor == 7) || (floor == 10) != has` (mutatie: op
  objecttype 0x0018 zoeken).
- Niet getoetst: een Ep2-kaart (Seaside zonder baasteleporter). De gevraagde kaart is de Ep1-kaart.

## 6. Testklok en testhaken

**Testhaken ruimen op: gebouwd.** `spire_static_test_forget` (`src/SpireFlagBridge.cc:7673`) wist nu elke sleutel van
zijn client en zijn game in alle tabellen van de bridge (6 per game, 21 per client). Rood, gemeten vóór deze
wijziging (`build/k3-red-forget.log`): de hervat-case faalde met `0x4A105101u != answers.back()`, omdat het run-einde
van de vorige case onder lobby-id 0x7B02 was blijven staan en de volgende game met dat id elke 'J' met "vers"
beantwoordde. De K3-cases beginnen daarom expres steeds op dezelfde lobby-id's en serienummers.

**Testklok: niet gebouwd.** Het is niet klein:

- De wachttijden lopen via `async_sleep` (`src/AsyncUtils.cc:112`), een `asio::steady_timer`. Die functie is van heel
  newserv: 31 aanroepen in 5 bestanden, 27 daarvan in `SpireFlagBridge.cc`.
- Daarnaast leest de bridge de klok 16 keer direct (`steady_clock::now()`), en `Client` bewaart twee tijdstippen van
  dat type (`src/Client.hh:235`, `:238`).
- Een verzetbare klok vraagt een eigen timertype met eigen wachtregels in asio, en die vervangt dan de timer en alle
  kloklezingen in productiecode. Dat is een wijziging aan het tijdgedrag van de draaiende server om 5 s testtijd te
  winnen. De K7-case duurt daardoor nog steeds ongeveer 5 s.

## Voor de quest

1. **'F' en de score.** Stuur bit 22 (`0x00400000`) in 'F' zodra score.py kamers en examens telt vanaf de eerste kamer
   van de run (K0 bij een verse run, de startkamer uit het 'J'-antwoord bij hervatten). Met dat bit laat de server de
   score met rust; zonder het bit trekt hij de kamers vóór de eerste kamer eraf. Het examenveld (bits 16-17) wordt bij
   een run die na kamer 1 begint niet meer geloofd: de server telt zelf. Pas de slotteksten van de pacts aan voor
   K0 16: Iron man en Haste na Barba Ray, Bare en Hungry na Gol Dragon, of een gewonnen run.
2. **Startkamer.** Lees in rs_begin, vóór het maskeren naar RS_ON, `(r41 >> 16) & 0xFF`: de eerste kamer van de run.
   0 betekent onbekend (een oudere newserv): val dan terug op K0.
3. **Rustkamers.** Stuur in ss_write als tweede waarde `r131 & 0x3F` in plaats van `r131 & 3`. Zet bij hervatten in
   rs_apply `r131 |= (r41 >> 10) & 0x3C`. Volgorde: eerst deze newserv, dan de quest. Een oudere newserv bewaart de
   waarde ongemaskeerd en weigert dan het hervatten van een run met een rustkamer.
4. **'C' 6.** k11 stuurt `0x14` in plaats van `0x10`. De baasuitgangen met Sega's teleporter (k3, k7, k10, k14, k17,
   k20, k29; de Ep2-kamers zijn gemeten in ronde 2, `kwaliteit-2-tekst-newserv-r2.md:109-111`, de Ep1-kamers vandaag
   in punt 5) sturen `0xF7` in plaats van `0xF3`; k26 houdt `0xF3`. Een oudere newserv antwoordt op `0x14` en `0xF7`
   met "The way is open. Walk to a warp.", dus ook hier eerst newserv.

## Meting 79: harde serverstop midden in een game

**Wat er op dat moment op schijf staat.** Eén bestand per game en karakter,
`system/inventory-backups/<serienummer>.<karakter-id>/<tijd>.json`, geschreven bij het binnenkomen van de game
(`src/ReceiveCommands.cc:5155`, bij de 6F), atomair met fsync. Erin: de inventaris, de meseta, het level en de naam
op het moment van binnenkomen, het questnummer en de Spire-markering (`src/InventoryBackup.hh:57-76`). De laatste 20
blijven bewaard.

**Wat er niet staat.** Het einde van de game. Een nette stop (SIGTERM, SIGINT) schrijft eerst voor elke client de
inventaris zoals de server hem op dat moment kent (`src/SignalWatcher.cc:61`, `src/InventoryBackup.cc:612`). Een
crash of SIGKILL schrijft niets. Ook de lijst van wat tijdens de game legitiem uit de inventaris ging (weggelegd,
geruild, verkocht, in de bank gezet, gebruikt) staat alleen in het geheugen
(`Client::inventory_backup_removals`).

**Wat de speler kwijt is.** Wat Sega's eigen straf voor een niet netjes beëindigde game wegneemt; de server ziet dat
pas bij de volgende login. Hij zet dan een verliesmarkering en zegt: "Je verloor N items bij een harde serverstop.
$restore kan dit niet terugzetten." (`src/InventoryBackup.cc:407-413`, `:426`). `$restore` weigert met "onbekend
einde" (`:355-356`).

**Waarom hij weigert.** Zonder eindstand is de inventaris van het binnenkomen de enige bron. Teruggeven daaruit geeft
ook terug wat de speler in die game zelf wegdeed.

Drie smalle routes:

| Route | Wat | Risico verkeerde of dubbele teruggave |
|---|---|---|
| A. Tussenstand bijschrijven | Bij elke wijziging die de server ziet de huidige inventaris als voorlopige eindstand in het bestand zetten (afgifte direct, oppakken mag gebundeld). Na een crash telt de laatste tussenstand als eindstand. | Klein: alleen een afgifte in de laatste milliseconden vóór de crash komt dubbel terug. Kost een bestandsschrijf met fsync per afgifte. |
| B. Afgiftelogboek | Naast het bestand een logboek met één regel per afgifte die de server ziet. Herstel = inventaris van binnenkomen, min huidige, min logboek. | Even klein voor dubbel. Wat in de gecrashte game is gevonden komt niet terug: te weinig, nooit te veel. |
| C. Handmatig vrijgeven | Een beheerderscommando markeert zo'n back-up als herstelbaar vanuit de stand van binnenkomen. Het bestaande `spire-backup-import` doet iets vergelijkbaars voor guard-snapshots. | Groot: alles wat in die game is verkocht, geruild of in de bank gezet komt dubbel terug. Alleen verantwoord als de beheerder weet wat er in die game gebeurde. |

Niets gebouwd: dit raakt items van een karakter.

## Meting 128: de tweede speler groeit nauwelijks mee

- **Wie rekent EXP uit op de GameCube: de client.** De server ziet alleen het level-up-bericht (6x30) en neemt daar
  level en stats uit over (`src/ReceiveSubcommands.cc:4100-4134`). De EXP-verzoeken en de gedeelde EXP bestaan alleen
  voor Blue Burst: 6xC6 en 6xC8 hebben voor GameCube geen afhandeling (`:5955`, `:5957`).
- **Kan de server EXP geven: nee.** `send_give_experience` (6xBF) gooit een fout voor elke client die geen Blue Burst
  is (`src/SendCommands.cc:3259-3263`). Het stat-commando dat wel op GameCube werkt (6x9A) kent alleen HP, TP en
  meseta (`src/SendCommands.hh:314-320`). `$edit exp` wijzigt alleen de kopie op de server
  (`src/ChatCommands.cc:912-913`).
- **Wat de quest kan.** `get_player_level` leest een level (`src/QuestScript.cc:2450`). Een level zetten kan alleen
  in het regelblok van de Battle-regels: `ba_set_char_level` (`:1789`). Sega heeft daar ook `ba_death_lvl_up`
  (`:1524`): levels erbij bij een dood. Dat is een regel van Sega zelf, uitgevoerd door de client; in onze regels
  staat hij niet en op de Wii is hij niet gemeten.
- **Wat de run nu al doet.** Het startlevel komt uit het regelblok: `char_level` 0 (Lv1) in spire.json:18 en 23
  (Lv24) in spire-ep2.json:14 op d6fc2ef. newserv schrijft per client zijn eigen level in de quest die hij krijgt
  (`spire_start_level_patch_bin`, `src/SpireMeta.hh:336-355`) en zet bij hervatten het bewaarde level terug
  (`SpireSavestate::level`, `src/SpireFlagBridge.hh:377-381`).
- **Haalbaar zonder clientpatch.**
  1. Een levelvloer voor de achterblijver op elk moment dat een quest laadt (start, kamerquest, hervatten): newserv
     geeft hem bijvoorbeeld het level van de leider min een paar. Het mechanisme staat er al. Het werkt niet
     tussen de kamers van één doorlopende run, want daar laadt geen quest.
  2. `ba_death_lvl_up` in het regelblok: wie doodgaat stijgt. Eerst op de Wii meten.
  3. EXP delen tijdens de run kan niet zonder clientpatch.

## Beslissingen in dit werk (voor decisions.md, door de regie te loggen)

- De server corrigeert ook de score, niet alleen de vlaggen. Dekking: de opdracht ("krijgt punten voor 13 kamers en
  2 examens die niet gespeeld zijn", "ontgrendelt en telt daaruit"). Gevolg: eindscherm en bewaarde score lopen uiteen
  tot de quest bit 22 stuurt.
- Een run vanaf kamer 1 houdt het examenveld van de quest als ondergrens. Dekking: "een Ep1-run mag niets verliezen".
- De rustbits reizen in de variantwaarde van de 'V' en in bits 12-15 van het 'J'-antwoord, niet als twintigste waarde
  of nieuw register. Reden: de huidige quest leest die bits aantoonbaar niet, en het bestand houdt zijn oude vorm.
- `tests/spire-gc.dat` staat buiten `src/**`. Dekking: punt 5 van de opdracht ("kopieer wat de test nodig heeft naar
  de testdata van newserv").
- De assertie op `0x43050605` is aangepast naar `0x43080605` (zie Statische tests).

## Gezien, niet gebouwd

- datakwaliteit · een gewonnen Ep2-run slaat k30 bij zijn 'F' niet zelf op, een gewonnen Ep1-run k15 wel
  (`spire_finish_boss_store_room`, `src/SpireFlagBridge.cc:7041`); zonder kamer-clear van de server mist k30 dan zijn
  splinters · klein
- datakwaliteit · na hervatten met de huidige quest telt het eindscherm een rustkamer nog als gehaalde kamer; de
  server corrigeert dat deel van de score niet (alleen de kamers vóór de eerste kamer) · weg zodra de quest contract 3
  volgt
- tekst · de slottekst van de pacts in Spire Run II is na deze fix onwaar tot de quest hem aanpast (zie punt 1) ·
  quests/spire/score.py:100
- ux · critic r2 K-1 (wie binnen 4 s na de laatste baas de game verlaat mist "the gift is not given") is in deze
  ronde niet aangeraakt · `src/SpireFlagBridge.cc`, 'C' 12
- test · de K7-case zet nog de werkmap van het hele proces om en slaapt ongeveer 5 s (zie punt 6); de twee nieuwe
  cases met een echte client zetten de werkmap op dezelfde manier om, zonder te slapen · `src/StaticTests.cc:7300`,
  `:7894`, `:8029`
- test · `tests/spire-gc.dat` is een kopie van een ongetrackt bouwresultaat; een nieuwe kaartbouw werkt hem niet bij ·
  een kopieerstap in de questbuild zou dat dichten
