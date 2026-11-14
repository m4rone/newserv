# Kwaliteit 3, back-up na een harde stop: fixronde op de critic (newserv)

Tak `spike/kwaliteit-2`, start a5fa1dc. Geschreven 2026-11-11. Bron: `kwaliteit-3-backup-critic.md` (NIET SHIPPEN op
B1, B2, B3). Alle uitkomsten hieronder zijn op die dag in deze worktree gemeten.

## Voortgang

| Punt | Stand | Commit |
|---|---|---|
| B1 herkomst: na een harde stop alleen wat bewezen van de speler is | gebouwd, rood en groen gemeten | 50fedd8 |
| B2 een harde-stop-back-up vervalt zodra de speler verder speelde | gebouwd, rood en groen gemeten | bfebcc1 |
| B3 mislukte schrijf na een afgifte zet de back-up opzij als onherstelbaar | gebouwd, rood en groen gemeten | 7be9702 |
| M1 opzij gezette back-up blijft in de lijst als geweigerde, nieuwste game | gebouwd, rood en groen gemeten | 5548c6a |
| M4 dezelfde regel voor de Exit (verbroken verbinding) | gemeten: klopt; gebouwd, rood en groen | adcb498 |
| M2 een einde dat in een levend proces verloren ging is geen harde stop | gebouwd, rood en groen gemeten | 79561be |
| M3 schrijftijd meten | niet gebouwd (opdracht); commando hieronder | - |
| De zes "kan"-punten | K1 deels, K2 deels, K5 gebouwd; K3, K4, K6 niet (zie onder) | in bovenstaande |
| Dit rapport | klaar | de commit na 79561be |

## Verdict

Gebouwd en groen in statische tests; elk punt is rood gemeten met de regel van a5fa1dc teruggezet. Gebouwd is de
**nauwkeurige vorm van B1** (herkomst per item), niet de kale doorsnede. Niet bewezen: een echte harde stop met een
echte client, en of de GameCube-client alles meldt wat de server hieronder aanneemt te zien (zie "Wat de server weet").

## Dekking

Dit dekt: de herstelregel na een harde stop en na een verbroken verbinding, en de vier schrijf- en lijstpunten, met
statische tests op bestanden en op de serverhaken met echte `Client`- en `Lobby`-objecten. Niet gedekt: een echte
SIGKILL met een echte client, de schrijftijd op de schijf van de pc, en de afhandelaars van de spelcommando's zelf
(6x2A, 6x59, 6x5F, 6x29, 6x2B): de tests roepen dezelfde haken in dezelfde volgorde aan, niet de afhandelaars.

**Wat een speler na een harde stop terugkrijgt, in gewone woorden.** Alles wat hij bij het binnenkomen had en op het
moment van de stop nog had. Alles wat hij in die game van een vijand of uit een kist opraapte, als de server die drop
zelf maakte of het drop-verzoek voor die vijand of kist zelf had gezien. De Meseta die hij zo opraapte. Wat hij zelf
liet vallen en zelf weer opraapte.

**Wat hij maximaal kwijt is.** Wat hij in die game van een andere speler kreeg (van de grond of in een ruil). Wat hij
uit de bank haalde. Wat hij in de winkel kocht, en de Meseta die hij daarvoor betaalde. Wat hij liet taxeren en wat
een quest hem gaf (beide komen als "nieuw item van de client" binnen). Een stapel die hij splitste, liet vallen en
weer opraapte. Meseta die een ander hem gaf. Wat hij aan een ander gaf: dat krijgt die ander niet terug, en hijzelf
alleen als zijn kaart het nog heeft. En wat in de laatste twee seconden binnenkwam. **Nooit: iets dubbel door
`$restore`.** Na een verbroken verbinding geldt precies hetzelfde.

## Wat de server over herkomst weet (GameCube-client)

Gelezen in `src/ReceiveSubcommands.cc`, `src/ReceiveCommands.cc`, `src/Lobby.cc` en de protocolnotities in
`src/CommandFormats.hh`. Niets hiervan is op de Wii gemeten; het is wat de servercode doet met wat de client stuurt.

| Gebeurtenis | Wat de server ziet | Betrouwbaar? | Telt mee in het herstel |
|---|---|---|---|
| Vloeritem van vijand of kist, serverdrops (`SERVER_*`) | de server maakt het item zelf (`on_entity_drop_item_request`) | ja, eigen werk | ja |
| Vloeritem van vijand of kist, clientdrops (`CLIENT`, de standaard voor V3) | de leider meldt het item met 6x5F; de server zag eerder het drop-verzoek (6x60/6xA2) voor die vijand of kist en markeerde hem zelf | de inhoud is het woord van de leider; het verzoek en "één per vijand of kist" zijn van de server | ja, alleen met gezien verzoek, één keer per vijand of kist |
| Speler laat een item vallen (6x2A) | de server haalt het zelf uit de inventaris en legt het zelf op de vloer | ja | voor een ander: nee; voor dezelfde speler die het weer opraapt: ja |
| Speler laat een deel van een stapel of Meseta vallen (6x5D, daarna 6x29) | het vloeritem is wat de client opgeeft | nee | nee |
| Oprapen (6x59/6x5A) | de server verplaatst zijn eigen vloeritem naar de inventaris | ja; de herkomst is die van het vloeritem | volgt het vloeritem |
| Ruil (D2, daarna 6x29 bij de gever en 6x2B bij de ontvanger) | de gever: verwijdering; de ontvanger: "nieuw item" | de verwijdering ja; de herkomst van het nieuwe item nee | nee |
| Bank opnemen, taxeren, quest-item (6x2B) | "nieuw item in de inventaris", zonder bron | nee | nee |
| Bank storten, verkopen, mag voeren (6x29) | verwijdering | ja, als de client het stuurt (volgens de protocolnotitie wel; niet gemeten) | n.v.t.: weg |
| Winkel (6x5E) | item erbij, Meseta eraf | het item ja; de herkomst van de Meseta nee | nee |
| Verkoopopbrengst op V3 | niets: de Meseta-stand van de server stijgt niet | - | nee (de server telt hem niet) |
| `$restore`-drop | de server legt het zelf neer, alleen zichtbaar voor die speler | ja | ja |

De server houdt dit per client bij (`InventoryProvenance`): per item-id hoeveel eenheden níet bewezen zijn. Na elk
subcommando vergelijkt hij de inventaris met de vorige; wat erbij kwam zonder dat de server ervoor instaat, is
onbewezen. De standaard is dus "onbekend": een pad dat niemand heeft aangesloten geeft minder terug, nooit meer.

## Wat er gebouwd is

1. **B1 (50fedd8).** Na een harde stop is de bron niet de tussenstand zelf maar het bewezen deel ervan
   (`inventory_backup_own_state`). Vloeritems dragen hun herkomst (`Lobby::FloorItem::backup_origin`). De tussenstand
   schrijft `Own` (item-id's waarvan het bewezen aantal afwijkt van de standaardregel) en `OwnMeseta`. De
   standaardregel, voor wie geen herkomst heeft: zelfde id en zelfde type als bij het binnenkomen, hooguit het aantal
   van toen; Meseta hooguit die van toen. Een tussenstand zonder `Own` (geschreven door a5fa1dc) is geen eindstand:
   geweigerd zoals een harde stop vóór de tussenstand.
2. **B2 (bfebcc1).** Een back-up met verliesmerk blijft alleen voorkeur hebben zolang er geen latere game is (anders
   dan de huidige) die met een 98 eindigde of waarvan het einde afwijkt van het binnenkomen. Daarna zegt `$restore`
   "Na dat verlies speelde je verder: / niet meer herstelbaar." en vraagt de login-controle er niet meer om.
   `$restore`, `$restore check` en de tests gebruiken nu één functie (`inventory_restore_decide`).
3. **B3 (7be9702).** Mislukt een schrijf nadat iets de inventaris verliet, dan wordt de back-up meteen hernoemd naar
   `<tijd>.lost.json`. Hij blijft in de lijst als die game en `$restore` weigert hem ("Die back-up kon niet worden /
   bijgewerkt: niet herstelbaar."). Lukt ook het hernoemen niet: E-regel, en elke volgende wijziging probeert beide
   opnieuw. Een mislukte schrijf zonder afgifte vertraagt alleen wat binnenkwam.
4. **M1 (5548c6a).** Opzij zetten heet nu `<tijd>.unmarked.json`: het bestand blijft in de lijst op zijn plek en leest
   als Spire-back-up (de naam is het merk), ook als de inhoud onleesbaar is.
5. **M4 (adcb498).** Gemeten: het klopt (rood hieronder). De Exit volgt dezelfde regel. Hij schrijft `Own` alleen waar
   dat afwijkt van de standaardregel, dus een game zonder bewezen aanwinst laat byte voor byte het bestand van 054db56
   achter.
6. **M2 (79561be).** Een levend proces dat een back-up loslaat zonder einde (einde niet te lezen of te schrijven,
   volgende join terwijl de back-up nog open is, verbinding weg zonder game-lobby, SIGTERM met een open back-up zonder
   client in een game) zet hem opzij als `.lost.json`. Alleen een proces dat stierf laat een back-up zonder einde na.

## Rood en groen

Rood is per punt gemeten met één mutatie in `src/InventoryBackup.cc` die de regel van a5fa1dc terugzet, in een eigen
build (logs `build/r2-<punt>-red.log`, elk exit 1). "Zonder de fix" zelf compileert niet: de tests gebruiken de nieuwe
functies.

```
B1 (bron = de tussenstand zelf):
K3 (backup 7: two players in one game, ...) FAILED: ... StaticTests.cc:8495: 1u != count(KB_SABER)
K3 (backup B1 1: A drops what he had equipped at the join, B picks it up, hard stop: the item exists once ...) FAILED: ... :8927: 1u != kb_total(g, KB_SABER, kb_penalty_card)
K3 (backup B1 2: a trade on the GameCube with the hard stop between every two commands ...) FAILED: ... :8971: !(n <= 1)
K3 (backup B1 3: the bank: ...) FAILED: ... :9018: 0u != kb_plan_n(both.a, KB_FOUND)
K3 (backup B1 4: Meseta handed over ...) FAILED: ... :9042: 100u != both.b.meseta
K3 (backup B1 5: the good path: ...) FAILED: ... :9086: 0u != kb_plan_n(both.b, KB_X2)
K3 (backup B1 6: the rules on disk: ...) FAILED: ... :9168: 650u != from.meseta
B2 (een gepinde back-up vervalt nooit):
K3 (backup B2: a hard-stop backup is refused once the player played on: ...) FAILED: ... :9260: KbR::SUPERSEDED != d.refusal
B3 (alleen de W-regel):
K3 (backup B3: an interim write that fails after a removal sets the backup aside: ...) FAILED: ... :9303: !(!std::filesystem::exists(live.path))
M1 (opzij gezet valt uit de lijst):
K3 (backup hooks 3: ...) FAILED: ... :8769: 2u != inventory_backup_list(dir).size()
K3 (backup M1: a backup set aside for its missing Spire mark stays the newest game and is refused; ...) FAILED: ... :8808: std::filesystem::path(aside).filename().string() != names.b...
M4 (bron = de Exit zelf, de regel van 054db56):
K3 (backup 5: ...) FAILED: ... :8426: 500u != r.plan.meseta
K3 (backup M4: after a dropped connection the Exit gives back only what is proven the player's own, ...) FAILED: ... :9362: 0u != kb_plan_n(d.plan, KB_SABER)
M2 (een verloren einde laat het bestand staan):
K3 (backup M2: a game whose end got lost inside a living server process is set aside; ...) FAILED: ... :9410: !(!std::filesystem::exists(first) && std::filesystem::exists(lost_name(firs...
```

De regelnummers zijn die van de build waarin het punt gemeten is. De B2-meting is gedaan vóór een correctie van de
test zelf (de Meseta op de kaart in het geval "verkocht, verbinding weg"); de rode regel is het geval "verkocht, 98".

Groen, 79561be (`build/r2-m2-green.log`, exit 0, slotregel `-- All static tests passed`):

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
K3 (backup M1: a backup set aside for its missing Spire mark stays the newest game and is refused; $restore does not work from the game before it)
K3 (backup B1 1: A drops what he had equipped at the join, B picks it up, hard stop: the item exists once (the critic's sequence, through the hooks))
K3 (backup B1 2: a trade on the GameCube with the hard stop between every two commands: the traded item exists at most once)
K3 (backup B1 3: the bank: what the client says it took out (6x2B) is never given back; what it put in (6x29) is gone)
K3 (backup B1 4: Meseta handed over is never given back to the receiver; Meseta of an enemy is)
K3 (backup B1 5: the good path: an item of an enemy or box the server vouches for is given back after a hard stop; everything else that came in is not)
K3 (backup B1 6: the rules on disk: Own names what differs from the default rule; an interim state without Own is no end state)
K3 (backup B2: a hard-stop backup is refused once the player played on: a later game with a sale and a clean exit)
K3 (backup M4: after a dropped connection the Exit gives back only what is proven the player's own, like the interim state)
K3 (backup M2: a game whose end got lost inside a living server process is set aside; only a process that died leaves a hard stop)
K3 (backup B3: an interim write that fails after a removal sets the backup aside: it stays listed and $restore refuses it)
```

W- en E-regels op 79561be: **20 W en 5 E** (basis a5fa1dc: 11 W en 1 E). De negen nieuwe W-regels en vier nieuwe
E-regels komen alle uit cases die expres een onleesbaar of onschrijfbaar bestand aanbieden:

- M1: 2 W "cannot write the interim state" en 2 E "the Spire mark could not be written ...; set aside as
  ....unmarked.json" (de twee varianten: onleesbaar en onschrijfbaar).
- M2: 5 W ("the next join came before the end of the game ...; set aside", "cannot record the end of the game",
  twee keer "the end of the game could not be recorded ...; set aside", "the server stops and the end of the game
  never came ...; set aside").
- B3: 2 W "cannot write the interim state", 1 E "a removal could not be written ...; set aside as ....lost.json",
  1 E "a removal could not be written ... and the backup could not be set aside either".

De cijfers van de eindbuild staan in het antwoord aan de regie.

## De gevraagde tests bij B1

| Scenario | Uitkomst |
|---|---|
| De reeks van de critic (B1 1, en backup 7 op bestanden) | Het wapen bestaat op elk stopmoment één keer: op de kaart van A. Was het niet uitgerust: nul keer na het oprapen door B. |
| A raapt zijn eigen drop weer op (B1 1) | Blijft van A. Ging het tussendoor langs B: niet meer bewezen, ook al heeft het zijn oude id. |
| Ruil op de GameCube, stop tussen elk tweetal commando's, beide volgordes (B1 2) | Hooguit één keer; nooit via `$restore` bij de ontvanger. |
| Bank (B1 3) | Opnemen komt binnen als 6x2B: nooit terug. Storten is een 6x29: weg. Een storting die de client niet meldt kan de server niet zien: zie Resterende risico's. |
| Meseta doorgeven (B1 4) | De ontvanger krijgt hooguit wat bewezen van hem is; Meseta van een vijand telt wel; uitgaven tellen eerst als eigen. |
| Het goede pad (B1 5) | Item van vijand of kist: terug. Tweede item voor dezelfde kist, item zonder gezien verzoek, winkelaankoop: niet terug. Stapel: 3 eigen + 2 van een vijand + 2 van een speler geeft 5 terug; na één gebruik 4. |

## M3: de schrijftijd op de pc meten, zonder de server te raken

Niet gebouwd. De duurlog staat er nog (`interim state N written (..., N us)`, W-regel boven 50 ms). Twee metingen:

```
# 1. Losse meting op dezelfde schijf en in dezelfde map als de back-ups (schrijft alleen een eigen proefmap en ruimt die op)
cd ~/pso-tools/newserv && python3 -c "
import os, time
d = 'system/inventory-backups/.fsync-probe'; os.makedirs(d, exist_ok=True)
data = b'x' * 9000; t = []
for i in range(200):
    s = time.perf_counter()
    fd = os.open(d + '/probe.json.tmp', os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644); os.write(fd, data); os.fsync(fd); os.close(fd)
    os.rename(d + '/probe.json.tmp', d + '/probe.json')
    dfd = os.open(d, os.O_RDONLY); os.fsync(dfd); os.close(dfd)
    t.append((time.perf_counter() - s) * 1000)
t.sort(); print('ms per write: p50 %.2f  p95 %.2f  p99 %.2f  max %.2f' % (t[100], t[190], t[198], t[-1]))
os.remove(d + '/probe.json'); os.rmdir(d)
"
# 2. Na een gespeelde game, uit het journal (alleen lezen); de eenheidsnaam is die van de pc
journalctl -u newserv --since today | /usr/bin/grep -o 'interim state.* [0-9]* us' | awk '{print $(NF-1)}' | sort -n | awk '{a[NR]=$1} END {print "n", NR, "p50", a[int(NR/2)+1], "p99", a[int(NR*0.99)+1], "max", a[NR], "(us)"}'
```

Het eerste commando is hier in een proefmap gedraaid om te zien dat het loopt (op tmpfs, dus het getal zegt niets).
De server schrijft daarnaast per keer ook de JSON (lezen, parsen, serialiseren); de laptopmeting uit ronde 1 was
2,6 ms per schrijf inclusief dat.

## Gekozen, met reden (door de regie te loggen in decisions.md)

- **De nauwkeurige vorm, niet de kale doorsnede.** De server ziet de herkomst van een vloeritem betrouwbaar genoeg:
  hij maakt het zelf, of hij verplaatst het zelf. De kale doorsnede op type had bovendien zelf een gat: wie zijn eigen
  wapen verkoopt en eenzelfde wapen van een ander krijgt, houdt "het wapen van toen". De regel is daarom op item-id.
- **Het woord van de leider (6x5F) telt alleen met eigen bewijs van de server**: clientdrops aan, het drop-verzoek voor
  die vijand of kist gezien en gemarkeerd, en één item per vijand of kist. Anders: onbekend.
- **Winkelaankopen zijn niet bewezen.** Het item komt uit de winkel, maar de Meseta kan van een ander zijn.
- **Wat een stapel verlaat telt eerst als eigen.** Zo blijft over wat mogelijk van een ander was, en dat valt weg.
- **Een tussenstand zonder `Own` wordt geweigerd** (bestanden van a5fa1dc). a5fa1dc is niet uitgerold.
- **Een Exit van vóór deze build leest volgens de standaardregel.** Gevolg: voor zo'n oude back-up komt wat de speler
  in díe game opraapte niet meer terug (wel wat hij bij het binnenkomen had). Dat is minder dan 054db56 gaf, bewust:
  het bestand zegt niet waar de aanwinst vandaan kwam.
- **B2: een bewaakte Spire-run telt niet als "verder gespeeld"** (de bewaker zet het karakter terug zoals het
  binnenkwam), anders zou één Spire-run na een verlies het herstel al blokkeren. Een geïmporteerde back-up vervalt
  niet (dat is een handeling van de beheerder).
- **M2: een 98 die zonder lobby binnenkomt wordt als net einde geschreven**; een verbroken verbinding zonder
  game-lobby zet de back-up opzij.

## De zes "kan"-punten

- **K1 deels gebouwd.** `$restore` en de tests delen `inventory_restore_decide` (B2, B3, M1, M4). De oude helper
  `kb_restore` bestaat nog voor de cases op één bestand. De afhandelaars zelf worden nog door geen test aangeroepen.
- **K2 deels gebouwd.** Tests voor B2, M1 en de bank zijn er; backup 7 rekent nu met de kaart die het uitgeruste wapen
  houdt. Backup 3 en 4 kunnen nog steeds niet rood.
- **K3 niet gebouwd.** Twee servers op dezelfde map: op te lossen in de inrichting, niet in de code.
- **K4 niet gebouwd, bewust.** De dubbele notitie bij een V3-ruil geeft verlies, nooit dubbel. Hem weghalen leunt op
  de aanname dat de 6x29 van de client altijd volgt.
- **K5 gebouwd.** De kop zegt nu dat een tussenstand per speler klopt, en waarom dat over spelers heen niet genoeg is.
- **K6 niet gebouwd.** Een 98 schrijft de kaart mogelijk niet; dat moet op de Wii gemeten worden. B2 leunt op "een 98
  betekent verder gespeeld", niet op "de kaart is geschreven".

## Resterende risico's

- **Een verwijdering die de client niet meldt.** Stort of verkoopt de GameCube-client zonder 6x29, dan blijft het item
  in het beeld van de server en komt het na een harde stop terug, terwijl de bank op de kaart het ook heeft. De
  protocolnotitie zegt dat de 6x29 komt; op de Wii is het niet gemeten. Dit gold ook al voor de Exit op 054db56.
- **Clientdrops:** wat het item ís, is het woord van de leider. Stuurt de client het drop-verzoek niet vóór de 6x5F,
  dan telt geen enkel opgeraapt item mee (verlies, geen dubbel). Te zien in het log: "Inventory backup"-regels en de
  `Own`-lijst in het bestand na een game met een opgeraapt item.
- **Schrijven én hernoemen mislukken:** dan staat er een E-regel en leest de back-up na een stop als herstelbaar uit
  de oude stand (B3), of als harde stop (M2).
- **Oude Exit-bestanden met stapels:** kwam er in zo'n game een stapel van een ander bij een eigen stapel, dan telt
  de standaardregel tot het oude aantal mee. Geldt alleen voor bestanden van vóór deze build.
- **Eén subcommando dat uit dezelfde stapel haalt en er onbewezen eenheden in stopt** ziet de vergelijking niet. Geen
  afhandelaar doet dat nu.
- **De kaart.** De tests rekenen met twee kaartstanden (alleen het uitgeruste; alles van het binnenkomen). Wat de kaart
  werkelijk houdt, beslist de Wii.

## Gezien, niet gebouwd

- datakwaliteit · een vervallen back-up met verliesmerk wordt nooit weggeroteerd en stapelt zich op · `src/InventoryBackup.cc` (`inventory_backup_files_to_delete`) · klein: vervallen telt niet meer als vastgepind
- gebruiksvriendelijkheid · winkelaankoop valt weg na een harde stop, ook als de speler met eigen Meseta betaalde · `src/ReceiveSubcommands.cc` (`on_buy_shop_item`) · middel: een eigen-Meseta-budget bijhouden
- gebruiksvriendelijkheid · een getaxeerd wapen en een quest-beloning vallen weg (komen als 6x2B) · `src/ReceiveSubcommands.cc` (`on_create_inventory_item_t`) · vraagt een meting van wat de client stuurt
- gebruiksvriendelijkheid · een gesplitste stapel of Meseta die de speler zelf weer opraapt telt niet meer als eigen · `src/ReceiveSubcommands.cc` (`on_drop_partial_stack_t`) · klein, maar leunt op wat de client opgeeft
- test · de afhandelaars 6x2A, 6x59, 6x5F, 6x29, 6x2B worden door geen test aangeroepen (K1) · `src/StaticTests.cc` · middel: een game met geladen itemtabellen in de testopzet
- performance · elke afgifte doet synchroon een fsync in de netwerklus (M3) · `src/InventoryBackup.cc` (`inventory_backup_write_file`) · eerst meten met het commando hierboven
- datakwaliteit · het ontvangen item van een ruil staat pas na de bundel op schijf · `src/ReceiveSubcommands.cc` · klein; sinds B1 telt het toch niet mee
- inrichting · twee newserv-processen op dezelfde map lezen elkaars lopende games als harde stop (K3) · `system/inventory-backups` · controle van de labinstanties
- bewijs · of een 98 de kaart schrijft is niet gemeten (K6) · Wii · één meting

## Afwijkingen van de opdracht

- Rood is met mutaties gemeten, niet met een build zonder de fix (die compileert niet).
- De commit van B3 brengt de naamgeving voor opzij zetten mee die M1 daarna gebruikt; M4 staat vóór M2 zoals gevraagd.
- `decisions.md` en `observaties.md` zijn niet aangeraakt (buiten de schrijfset); de regels staan hierboven.
- Twee keer liep een build met testrun langer dan 180 s (de machine was druk) en is hij op de achtergrond afgemaakt;
  de uitkomsten zijn uit de logs gelezen. Bij M1 liepen daardoor kort twee `make`-aanroepen tegelijk in `build/`; de
  builds daarna (M4 en M2) zijn elk alleen gedraaid en hebben alles opnieuw gecompileerd (de kop is gewijzigd); de
  eindbuild is alleen gedraaid en linkt opnieuw.
