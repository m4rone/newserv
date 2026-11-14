# Kwaliteit 3, bundel 3: items na een harde serverstop (newserv)

Tak `spike/kwaliteit-2`, start 054db56. Geschreven 2026-11-11. Besluit: B-REGIE-20-4, route A. Alle getallen zijn op
die dag in deze worktree gemeten.

## Voortgang

| Stap | Stand | Commit |
|---|---|---|
| Bronnen gelezen (besluit, meting 79, InventoryBackup.cc en .hh volledig) | klaar | - |
| Drie bestanden geschreven door de code van 054db56 (testdata van vóór de wijziging) | klaar | 9702c3e |
| Tussenstand in het back-upbestand, herstel na harde stop, duurzaam schrijven | gebouwd | 9702c3e |
| Twaalf testcases (zes gevraagde, zes eigen) | groen; rood gemeten met mutaties | 9702c3e |
| Schrijftijd op echte schijf gemeten | klaar | - |
| Dit rapport | klaar | de commit na 9702c3e |

## Verdict

Gebouwd en groen in statische tests. Niet bewezen: een echte harde stop met een echte client (zie Dekking).

## Dekking

Dit dekt: de tussenstand in het back-upbestand en het herstel na een harde stop, met statische tests op bestanden en
op de serverhaken met een echte `Client` in een echte `Lobby`. Niet gedekt: een echte SIGKILL van een draaiende server
met een echte client (niet gedaan; alleen de Wii en de pc tonen dat), en verlies binnen het laatste bundeltje.

**Maximaal verlies bij een harde stop, in woorden:** wat de speler in de laatste twee seconden vóór de stop in zijn
inventaris kreeg (opgepakt, gekocht, uit de bank gehaald, in een ruil ontvangen) en wat nog onderweg was tussen client
en server. Wat hij wegdeed staat altijd al op schijf en komt nooit terug.

## Leeswijzer bij de opdracht: wat "afgifte" hier is

De opdracht noemt "afgifte van een item door de server aan de speler" en verwijst naar "de afgiftelijst in het
geheugen". Die lijst is `Client::inventory_backup_removals`: wat de inventaris **verliet** (weggelegd, geruild,
verkocht, bank, gebruikt, mag gevoerd, winkelmeseta). Zo gebruiken het besluit en meting 79 het woord ook, en alleen
die richting kan dubbel geven. Zo is het gebouwd: wat de inventaris verlaat gaat direct naar schijf.

De andere lezing (de server geeft de speler iets) is ook gedekt:

- Het enige wat de server zelf teruggeeft is `$restore`. Het merk "hersteld" staat op schijf vóór de items vallen
  (bestaande volgorde, `src/InventoryBackup.cc:921-922`), nu ook met fsync van de map. Getoetst in case 1 en case 9.
- Wat de inventaris binnenkomt staat binnen twee seconden in de tussenstand. Case 1 toetst dat zo'n item na de stop
  precies één keer in kaart plus teruggave zit.

## Wat er gebouwd is

Alle regelnummers zijn die van 9702c3e.

1. **Tussenstand in hetzelfde bestand.** Sleutel `Interim`: de inventaris en meseta zoals de server ze op dat moment
   kent, het tijdstip, een teller en `Boot` (het serverproces dat schreef). Bestandsformaat verder ongewijzigd.
   - Eerste tussenstand bij het binnenkomen, in dezelfde schrijf als de back-up (`inventory_backup_on_game_join`,
     `:603`).
   - **Direct** zodra iets de inventaris verlaat: `inventory_backup_note_removed` (`:823`) en
     `inventory_backup_note_meseta_removed` (`:831`) schrijven zelf. Eén aanroeper meldt vóór hij weghaalt
     (`on_use_item`); daarvoor schrijft het einde van het commando (`inventory_backup_after_command`, `:714`,
     aangeroepen in `on_command`, `src/ReceiveCommands.cc:6118`).
   - **Gebundeld** voor alles wat binnenkomt: een timer per game en speler schrijft elke 2 s als er iets veranderd is
     (`interim_ticker`, `:592`). Een directe schrijf neemt wat wacht meteen mee.
   - Een ruil die de server zelf uitvoert (Blue Burst) schrijft beide kanten direct (`src/ReceiveCommands.cc:5377`).
     Op de GameCube stuurt elke client zijn eigen verwijder- en maakcommando's; die volgen de regels hierboven.
   - Elke tussenstand is een stand waarin de inventaris echt was: het hele bestand wordt herschreven, eerst gelezen
     zodat een verliesmerk van een latere login blijft staan (`inventory_backup_write_interim`, `:485`).
2. **Duurzaam schrijven.** `inventory_backup_write_file` (`:199`) volgt nu het patroon van
   `spire_bridge_write_durable`: tijdelijk bestand in dezelfde map, fsync met foutcontrole, rename, fsync van de map.
   Dat geldt voor elke schrijf van dit bestand, ook de bestaande (join, einde, verliesmerk, hersteld-merk).
3. **Na een harde stop telt de laatste tussenstand als eindstand** (`inventory_backup_interim_is_end`, `:453`): geen
   Exit, geen 98, en `Boot` is niet het draaiende proces. `$restore` herstelt daaruit en zegt eerst
   "Stand van de laatste tussenstand / voor de harde serverstop."; is er niets weg: "Niets kwijt ten opzichte van /
   de laatste tussenstand." De login-controle noemt zo'n game herstelbaar in plaats van "harde serverstop, kan niet".
4. **Zonder tussenstand blijft alles zoals het was**: een bestand van een oudere server weigert met dezelfde tekst.
5. **Een game die eindigt laat hetzelfde bestand achter als vroeger.** Exit of 98 haalt de tussenstand weg
   (`inventory_backup_record_end`, `:427`). Byte voor byte getoetst tegen bestanden van 054db56.
6. **Spire-merk direct op schijf.** Het merk "de bewaker herstelt deze game" stond tot het einde alleen in het
   geheugen. Met een tussenstand zou `$restore` na een harde stop in een run dezelfde items geven die de bewaker ook
   terugzet. `inventory_backup_note_guarded` (`:721`) schrijft het merk nu meteen. Lukt dat niet, dan wordt het
   bestand hernoemd naar `<naam>.json.unmarked`: het blijft op schijf, maar `$restore` leest het nooit.

## Gekozen, met reden (voor decisions.md, door de regie te loggen)

- **Bundel: 2 seconden, op tijd, niet op aantal.** Een aantal laat één zeldzaam item liggen tot er meer komt. Twee
  seconden kost hooguit één schrijf per twee seconden per speler.
- **Synchroon schrijven in de lus, niet erbuiten.** Gemeten op de laptop (btrfs op een versleutelde schijf, map
  `build/`), 200 schrijven van een volle inventaris van 30 items, bestand 8968 bytes, inclusief lezen, schrijven,
  twee keer fsync en rename: gemiddeld 2,6 ms, mediaan 2,5 ms, p95 3,4 ms, hoogste 9,5 ms (tweede reeks: 2,6 / 2,5 /
  3,1 / 5,8). Buiten de lus zou de volgorde van schrijven per bestand een eigen risico worden. De schijf van de pc is
  niet gemeten: elke schrijf logt zijn duur, en boven 50 ms komt er een W-regel "the interim write took N ms".
- **Harde stop herkennen aan het proces, niet aan het geheugen.** `Boot` is de starttijd van het proces in
  microseconden. Een tussenstand van het draaiende proces telt nooit als einde: die game loopt nog, of zijn einde
  ging binnen dit proces verloren, en dat weigerde al.
- **Eerste tussenstand bij het binnenkomen.** Anders zou een stop vóór de eerste wijziging nog steeds weigeren,
  terwijl de stand van binnenkomen op dat moment de ware stand is.

## Statische tests

- Basis 054db56: 14359 regels, exit 0, slotregel `-- All static tests passed`, tien W-regels, geen E-regel.
- 9702c3e: 14402 regels, exit 0, zelfde slotregel. **Elf W-regels en één E-regel**: de tien van de basis, plus één
  W en één E uit case "hooks 3", die expres een onleesbaar bestand aanbiedt:
  `W ... Inventory backup: cannot write the interim state into system/inventory-backups/0000005200.FD2FE357/...json: end of string (retried every 2 s)`
  en `E ... Inventory backup: the Spire mark could not be written into ...; set aside as ....json.unmarked, $restore will not use it`.
- De cases staan in het blok `-- Spire kwaliteit-3`, `src/StaticTests.cc:8156-8713`, en gebruiken de bestaande opzet
  met een echte client. Looptijd van de hele suite: 5,6 s werd 8,0 s (case "hooks 2" wacht 2,4 s op de echte timer).
- Tijdelijke mappen: `/tmp/newserv-static-backup-interim-<naam>-<pid>` en de bestaande `K3TempCwd`; opgeruimd met
  `std::filesystem::remove_all` op dat vaste pad.

### Rood

"Zonder de fix" compileert niet (de functies bestaan dan niet). Rood is daarom gemeten met mutaties die elk één deel
terugzetten naar 054db56, in twee builds (`build/k3b-redA.log`, `build/k3b-redB.log`, beide exit 1). De regelnummers
in de rode regels zijn die van de mutatiebuild: 36 hoger dan in 9702c3e (er stond tijdelijk een meetblok bovenaan).

Build A, mutatie "de tussenstand telt nooit als einde" (het gedrag van 054db56):

```
K3 (backup 1: a hard stop after something left the inventory never gives it back; what the server gave is there once) FAILED: ... StaticTests.cc:8310: KbR::NONE != r.refusal
K3 (backup 2: a hard stop after a pick-up whose bundle was written loses nothing) FAILED: ... :8348: KbR::NONE != r.refusal
K3 (backup 3: a hard stop between a pick-up and its bundle leaves the interim state before it, whole) FAILED: ... :8371: KbR::NONE != r.refusal
K3 (backup 7: two players in one game, ...) FAILED: ... :8484: !(inventory_backup_interim_is_end(rb_backup, kb_new_boot))
K3 (backup 9: a $restore that a second hard stop interrupts gives nothing twice) FAILED: ... :8573: KbR::NONE != r.refusal
K3 (backup hooks 1: ...) FAILED: ... :8639: !(inventory_backup_interim_is_end(d, kb_next_boot))
K3 (backup hooks 2: ...) FAILED: ... :8707: KbKind::INTERIM != r.kind
K3 (backup hooks 3: ...) FAILED: ... :8729: !(is_end)
run-static-tests: failure at ... :8751: 0u != k3_failed
```

Build B, vijf mutaties tegelijk, elk in een eigen case zichtbaar:

```
(formaat verandert zonder tussenstand)   K3 (backup 5: ...) FAILED: ... :8415: raw != inventory_backup_to_json(b).serialize(...)
(het einde laat de tussenstand staan)    K3 (backup 6: ...) FAILED: ... :8457: !(!b.has_interim)
(idem)                                   K3 (backup 8: ...) FAILED: ... :8512: !(!r.backup.has_interim)
(weghalen schrijft niet direct)          K3 (backup hooks 1: ...) FAILED: ... :8645: 2u != d.interim_count
(de timer schrijft de bundel niet)       K3 (backup hooks 2: ...) FAILED: ... :8703: 2u != d.interim_count
(Spire-merk alleen in het geheugen)      K3 (backup hooks 3: ...) FAILED: ... :8725: !(d.spire && d.has_interim)
```

Een zesde mutatie (een game zonder einde herstelt uit de stand van binnenkomen, route C) viel al in een bestaande
test: `StaticTests.cc:4303: R::UNKNOWN_END != inventory_restore_refusal(...)`.

**Case 4 heeft geen rode stand.** Een half tijdelijk bestand werd ook op 054db56 nooit gelezen: de naam eindigt op
`.tmp` en de lijst neemt alleen `.json`. Nieuw zijn de fsync van de map en de foutcontrole op fsync; die zijn in een
statische test niet waar te nemen. De case legt het bestaande gedrag vast.

### Groen (9702c3e, `build/k3b-2.log`; de eindbuild staat in het antwoord aan de regie)

```
K3 (backup 1: a hard stop after something left the inventory never gives it back; what the server gave is there once)
K3 (backup 2: a hard stop after a pick-up whose bundle was written loses nothing)
K3 (backup 3: a hard stop between a pick-up and its bundle leaves the interim state before it, whole)
K3 (backup 4: a half-written temp file beside a good backup is never read)
K3 (backup 5: a backup from before interim states loads as it did and is refused with the same text)
K3 (backup 6: a game with a removal and a pick-up that ends cleanly leaves the file of before, byte for byte)
K3 (backup 7: two players in one game, a hard stop at any moment: the item one drops and the other picks up exists at most once)
K3 (backup 8: a player who left the game before the hard stop is treated as before; a Loss mark survives interim writes)
K3 (backup 9: a $restore that a second hard stop interrupts gives nothing twice)
K3 (backup hooks 1: the join writes the first interim state; what leaves the inventory is on disk before the command ends)
K3 (backup hooks 2: a pick-up waits for its bundle and the timer writes it within the interval)
K3 (backup hooks 3: the Spire guard's snapshot puts the Spire mark on disk at once; a backup that cannot take it is set aside)
-- All static tests passed
```

### De zes gevraagde tests

| # | Scenario | Uitkomst |
|---|---|---|
| 1 | Stop na afgifte | Het weggelegde item zit bij drie kaartstanden (alleen uitrusting, stand na weglegging, stand van binnenkomen) nooit in de teruggave. Een item dat de server gaf zit in kaart plus teruggave precies één keer. Tweede `$restore`: "al hersteld". |
| 2 | Stop na oppakken, bundel geschreven | Elk item van de stand bij de stop zit één keer in kaart plus teruggave. |
| 3 | Stop tussen oppakken en bundel | De stand op schijf is byte voor byte de vorige tussenstand. Weg: het opgepakte item en de 250 meseta van na die stand. Het eerder weggelegde item komt niet terug. |
| 4 | Half tijdelijk bestand naast een goed | Het goede bestand laadt, de lijst toont alleen dat; de volgende schrijf ruimt het halve bestand op. |
| 5 | Oud bestand zonder tussenstand en zonder Exit | Weigert met "Onbekend hoe je vorige game eindigde:\nniet herstelbaar."; login-tekst "harde serverstop" ongewijzigd. De drie bestanden van 054db56 laden en schrijven byte voor byte terug. |
| 6 | Net pad | Game met weglegging en oppakken, daarna Exit of 98: het bestand is byte voor byte dat van 054db56 (5163 en 3111 bytes). Geen verschil. |

De testbestanden `tests/inventory-backup-pre-interim-{join,exit,left}.json` zijn geschreven door de build van 054db56
met een tijdelijk blok in de testcode (niet gecommit). De inhoud is gelijk in UTC, Tokio en lokale tijd.

### Eigen randgevallen

| Geval | Uitkomst |
|---|---|
| Twee spelers in één game: A legt weg, B pakt op (case 7) | Op vier stopmomenten bestaat het item hooguit één keer. Tussen B's oppakken en B's bundel: nul keer (verlies binnen het bundeltje). Twee bestanden, geen schrijf raakt het andere. |
| Speler verlaat de game vóór de stop (case 8) | 98: "netjes afgesloten", geen tussenstand meer. Verbinding weg: de Exit telt, niet een oudere tussenstand. |
| `$restore` onderbroken door een tweede stop (case 9) | Het merk "hersteld" staat er al: nooit een tweede keer. Wat op de grond lag en niet opgepakt was, is weg. Wat wel opgepakt en geschreven was, komt uit de tussenstand van de tweede game, één keer. |
| Zelfde proces: oude verbinding zit nog in de game, of het einde ging in dit proces verloren (case 8) | Tussenstand telt niet; weigert zoals voorheen; login-controle rekent zoals voorheen. |
| Verliesmerk van een latere login in hetzelfde bestand (case 8) | Blijft staan na elke volgende tussenstand. |
| Harde stop in een Spire-run (hooks 3) | Bestand zegt Spire: `$restore` weigert ("de Spire-bewaker herstelt dat zelf"), de login-controle zwijgt. |

## Resterende risico's

- **Schrijffout bij een directe schrijf** (schijf vol, I/O-fout): de vorige tussenstand blijft staan en kan het
  weggedane item nog bevatten. Er komt één W-regel, en elke 2 s een nieuwe poging. Volgt er een harde stop vóór een
  geslaagde schrijf, dan geeft `$restore` dat item terug. Blijft de schijf weigeren, dan faalt `$restore` zelf bij
  zijn hersteld-merk en valt er niets.
- **Ruil op de GameCube, milliseconden:** de ontvanger meldt zijn nieuwe item, de gever zijn verwijdering, elk met
  een eigen commando. Schrijft de bundel van de ontvanger tussen die twee en stopt de server precies daar, dan staat
  het item in beide tussenstanden. Dit is het "laatste milliseconden"-risico uit het besluit.
- **De server ziet alleen wat de client meldt.** Wat de client deed en de server nog niet bereikte, staat nergens.
  Welke stand de kaart na een harde stop heeft is hier niet gemeten; de tests rekenen met drie kaartstanden en geen
  ervan geeft dubbel.
- **Terugdraaien naar een oudere newserv:** die leest de nieuwe bestanden (de sleutel is extra), en weigert games
  zonder einde zoals voorheen. Schrijft hij zo'n bestand opnieuw, dan is de tussenstand weg.

## Gezien, niet gebouwd

- datakwaliteit · bestond al: een back-up met verliesmerk blijft de bron van `$restore`, ook games later; wat de
  speler in de tussentijd zelf wegdeed komt dan terug. Na deze wijziging geldt dat ook voor harde stops (die
  weigerden eerst voorgoed) · `src/InventoryBackup.cc:261` · een merk "alleen in de eerstvolgende game" of de
  latere weggaven meetellen
- datakwaliteit · `$restore` legt items op de grond; stopt de server opnieuw voor ze zijn opgepakt, dan zijn ze weg
  (nooit dubbel) · `src/InventoryBackup.cc:924-938` · vraagt ontwerp: wat op de grond ligt hoort nog niemand
- datakwaliteit · bij een ruil op de GameCube staat het ontvangen item pas na de bundel (2 s) op schijf · de
  maak-item-afhandeling direct laten schrijven · klein
- datakwaliteit · bestond al: komt een client zonder 98 in een lobby, dan blijft zijn back-up zonder einde; na een
  herstart telt de tussenstand daarvan als einde (onschadelijk: alles wat wegging staat erin) ·
  `src/InventoryBackup.cc:604-605`
- codekwaliteit · het duurzame schrijven staat nu twee keer (`spire_bridge_write_durable` en
  `inventory_backup_write_file`); `SpireFlagBridge.cc` is hier niet aangeraakt · één gedeelde helper
- test · de suite duurt 2,4 s langer door de echte timer in "hooks 2"
- bewijs · de schrijftijd op de schijf van de pc is onbekend; het journal toont hem na de eerste game
  ("interim state N written (..., N us)")

## Afwijkingen van de opdracht

- De opdracht vraagt de tests "in StaticTests.cc, elk rood zonder de fix". Elf van de twaalf zijn rood gemeten met
  mutaties; case 4 kan niet rood (zie boven).
- Het Spire-merk (punt 6 hierboven) en het opzij zetten staan niet in de opdracht. Zonder dat geeft route A in een
  bewaakte run dubbel; het is dus deel van "veilig binnen route A".
- `decisions.md` en `observaties.md` zijn niet aangeraakt (buiten de schrijfset); de regels staan hierboven.
