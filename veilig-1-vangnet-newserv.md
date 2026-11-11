# Veilig 1, het vangnet "nooit geraakt" en de kamer die openblijft (newserv)

Worktree `/home/m4rck/code/worktrees/newserv/veilig-1b`, tak `spike/veilig-vangnet`, start 9e40c01.

## Voortgang

| Punt | Stand | Commit |
|---|---|---|
| Basismeting statische tests op 9e40c01 | gedaan: 14015 regels, 10 W, 0 E, exit 0 | - |
| 2-M2 nameting | klopt | - |
| 2-M2 aanvulling (treffer op een kind) nameting | klopt in de code | - |
| 2-M2 gebouwd, met tests | gedaan, apart gebouwd en gemeten | c5f7141 |
| 2-M1 nameting | klopt | - |
| 2-M1 gebouwd (server-side), met test | gedaan | 8ff540e |
| 2-M3 meting (niet bouwen) | gedaan, zie hieronder | - |
| Rapport | deze commit; de eindbuild en de eindmeting van de tests komen erna en staan in de terugmelding | - |

## Verdict

Het vangnet telt een vijand pas af als er op de vloer van de kamer twee minuten lang niets gevochten is terwijl er een
speler staat, en een treffer op een kind (Hidoom, Migium, een Bee) telt als treffer op de ouder. Gaat het net af in
een golf die niet de laatste is, dan telt de server de golven erachter ook als klaar en komt "Room clear!" alsnog.
Alles is serverlogica met statische tests. Wat de GameCube-client doet met een vijand die de server dood verklaart, is
niet gemeten: dat beslist alleen de Wii.

## Wat de server over een vijand weet (9e40c01)

De client simuleert de vijanden zelf. De server ziet alleen wat een client erover meldt:

| Bericht | Wat de server bewaart | Waar |
|---|---|---|
| treffermelding bij een aanval, techniek of photon blast (6x46, 6x47, 6x49) | wie de vijand het laatst raakte | ReceiveSubcommands.cc:493-503 |
| toestand van een vijand (6x0A) | schade en toestandsvlaggen, en "geraakt" | ReceiveSubcommands.cc:3592-3634 |
| schade-ophoging (6xE4, alleen met een patch) en statusvlaggen op Ultimate (6x9C) | schade, vlaggen | ReceiveSubcommands.cc:3658-3727 |
| vlag op een vijand, 8 is verslagen (6x76) | de vlag, en daarna de golfcontrole | ReceiveSubcommands.cc:3478-3533 |
| een speler wordt geraakt (6x4A, 6x4B, 6x4C) of sterft (6x4D) | niets over de vijand; wel dát er gevochten wordt | ReceiveSubcommands.cc:1727-1745, 1807-1821 |
| spelerpositie en vloer (6x3E tot 6x42, 6x1F, 6x21) | de vloer van de speler | Client.hh:156 |

De server weet dus niet waar een vijand staat, of hij een speler ziet, of dat hij op de client bestaat. "Onbereikbaar"
is alleen af te leiden uit het uitblijven van meldingen. Daarom is de gekozen voorwaarde: het uitblijven van elk teken
van een gevecht, lang genoeg, terwijl er wel iemand is om te vechten.

## 2-M2: het net telt levende, bereikbare vijanden af

### Nameting: klopt

De keten op 9e40c01:

- ReceiveSubcommands.cc:3387-3391: een nederlaag die de golf niet afmaakt roept `spire_bridge_watch_unhit_wave` aan.
- SpireFlagBridge.cc:6678: de enige voorwaarde is `spire_wave_held_only_by_unhit`: minstens één dode, en de rest
  "nooit geraakt" en geen baas (6591-6603). "Nooit geraakt" is: geen treffermelding, geen schade, geen 6x0A-vlaggen
  (6605-6607).
- SpireFlagBridge.cc:6628: `co_await async_sleep(SPIRE_UNHIT_ENEMY_SECONDS)`, vast 30 seconden (SpireFlagBridge.hh:1076).
- SpireFlagBridge.cc:6656-6667: geldt de voorwaarde dan nog, dan krijgt elke nooit-geraakte vijand vlag 8, alleen in
  de kaartstand van de server.
- SpireFlagBridge.cc:6668 roept de golfcontrole opnieuw aan (ReceiveSubcommands.cc:3372-3476), die bij een klare
  laatste golf via 3473-3474 in `spire_bridge_check_room_clear` uitkomt: r251 en "Room clear!" naar elke client
  (SpireFlagBridge.cc:6568-6584).

Er is geen enkele toets op "kan iemand erbij": dertig seconden niet raken is genoeg. Een vijand achterin de kamer, een
speler die eerst herstelt, of een vijand die nog niet tevoorschijn is gekomen voldoet er allemaal aan.

Het tweede net (een gestart golf-event "zonder levende vijand", 6696-6791) heeft hetzelfde gat: een vijand die nog
niet geraakt is telt daar als niet levend (6698), en na twee blikken van 30 seconden telt het event als klaar
(6766-6783).

### Aanvulling van de critic (treffer op een kind): klopt in de code

- SpireFlagBridge.cc:6613-6615: `spire_wave_enemies` slaat elk kind over (`child_index` ongelijk 0) en leest
  "geraakt" alleen van de ouder (6616).
- De server boekt een treffer op de vijand met het gemelde nummer, zonder omleiding naar de ouder:
  ReceiveSubcommands.cc:496-498 (treffermelding), 3609-3623 (6x0A), 3671-3686 (6xE4). Alleen een alias krijgt de
  treffer ook (500-503, 3626-3634, 3687-3695); een alias bestaat alleen bij Dark Falz en Olga Flow (Map.cc:4877-4879,
  4886).
- Een Pan Arms is op de server drie vijanden: de ouder en de kinderen Hidoom en Migium (Map.cc:4755-4763).

Vechten de spelers tegen Hidoom en Migium, dan staan die treffers op de kinderen en leest het net de Pan Arms als
"nooit geraakt". De keten klopt. Niet gemeten: dat de client die treffers werkelijk met de nummers van de kinderen
meldt (ik heb het journal niet gelezen).

### De gekozen voorwaarde

Een net (het eerste en het tweede) telt alleen nog af bij een blik waarop alle vier gelden:

1. de golf wordt alleen nog opgehouden door vijanden die nooit geraakt zijn (als voorheen, geen baas);
2. een treffer op een kind is een treffer op zijn ouder (`spire_wave_enemies`);
3. op de vloer van de kamer is 120 seconden lang geen enkel teken van een gevecht geweest
   (`SPIRE_UNHIT_QUIET_SECONDS`): geen treffermelding, 6x0A, 6xE4, 6x9C of 6x76 op welke vijand ook, geen speler die
   door een vijand geraakt is en geen speler die stierf (`spire_bridge_note_fight`, de "gevechtsklok" per game en
   vloer);
4. er staat op dat moment een client op die vloer; een blik zonder speler op de vloer zet de klok opnieuw op nul.

Waarom zo, met wat de server werkelijk ziet:

- "Nog aan het bevechten" is zichtbaar: elke treffer op elke vijand van de kamer (ook een andere golf of de elite) en
  elke klap die een speler krijgt zet de klok terug. Een run vecht per vloer één kamer tegelijk en een geraakte speler
  noemt geen kamer; daarom loopt de klok per vloer.
- "Nog niet bereikt" is niet zichtbaar (de server kent geen positie van een vijand). Daar helpt alleen tijd: twee
  minuten zonder één treffer en zonder één klap is ruim meer dan een oversteek van een kamer.
- "Dood of weg" is zichtbaar: een sterfgeval zet de klok terug, een run die geëindigd is telt niet (als voorheen), en
  zonder client op de vloer loopt de klok niet.
- Een uitdrukkelijke keuze van de speler vraagt een menu in de quest (buiten mijn schrijfgebied) of een chatcommando
  (op de Wii zonder toetsenbord niet te typen). In plaats daarvan leest de speler na 45 seconden stilte één regel
  (zie 2-M1); wachten is dan zijn keuze.

Getal: 120 seconden. De ruimste stilte in de drie journal-gevallen was 63 seconden (zie hieronder); 120 is bijna het
dubbele. Een later net is minder erg dan een te vroege "Room clear!".

### De drie journal-gevallen nagerekend

De tijden komen uit de fixlijst (journal van de pc, gemeten op 25 en 26 oktober door een ander spoor; ik heb het
journal niet gelezen). Het oude net telde precies 30 seconden na de nederlaag die het wachten startte, dus die
nederlaag viel 30 seconden voor het aftellen.

| Geval | Wachten gestart | Afgeteld (oud) | Echte nederlaag | Langste stilte tot die nederlaag | Onder de nieuwe voorwaarde |
|---|---|---|---|---|---|
| 25 okt, kamer 7, drie vijanden van golf 1 | 10:21:52 | 10:22:22 | 10:22:55 tot 10:23:09 | 63 s | blijven leven: 63 s is minder dan 120 s, en elke nederlaag zet de klok terug |
| 25 okt, één vijand | 20:53:44 | 20:54:14 | 20:54:36 | 52 s | blijft leven |
| 26 okt, kamer 50, laatste vijand, golf met een Pan Arms | 19:55:40 | 19:56:10 | 19:56:28 | 48 s | blijft leven; geen "Room clear!" vóór de echte nederlaag |

In alle drie was het net niet afgegaan. De stilte is een bovengrens: de eerste treffer op zo'n vijand komt vóór zijn
nederlaag en zet de klok al eerder terug, en vanaf die treffer is de vijand "geraakt" en telt het net hem nooit meer.
Bij het geval van 26 oktober komt daar de regel van het kind bij: een treffer op Hidoom of Migium maakt de Pan Arms
"geraakt". Wat ik niet weet: of er in die 48 tot 63 seconden andere tekens van een gevecht waren; dat maakt de marge
alleen groter. De statische test rekent deze drie gaten na (geen blik binnen 0 tot 63, 52 en 48 seconden telt af).

## 2-M1: een onbereikbare vijand in een golf die niet de laatste is

### Nameting: klopt

- Het net zet vlag 8 alleen in de kaartstand van de server (SpireFlagBridge.cc:6663); er gaat niets naar een client.
- De golfcontrole zet het event van de golf op klaar (ReceiveSubcommands.cc:3403-3404) en doet bij de actie
  "trigger_event" (0C) niets, met de reden erbij: "This opcode causes the client to send a 6x67 command, so we don't
  have to do anything here" (3459-3463). De volgende golf start dus alleen als een client zelf de 6x67 stuurt
  (`on_trigger_set_event`, 3535-3553), en dat doet hij pas als hij de golf ziet eindigen.
- `spire_bridge_check_room_clear` vindt de kamer niet klaar ("triggers event N, not finished", 6450-6454, 6553-6557)
  en start de nawacht (6797-6821). Die kijkt na 30 seconden: het volgende event is niet gestart, dus er is geen
  gestart event zonder levende vijand, en bij 6785-6788 stopt de nawacht.
- 6801: bij een golf die via "construct enemies" (09/0D) volgt, start de nawacht niet (`waits_on_trigger` wordt alleen
  bij een trigger gezet, 6484-6491 en 6514-6516). Dat klopt in de code, maar komt in de kaarten van de run niet voor:
  mapgen schrijft alleen 0C en 01 (quests/lib/mapgen.py:197-201).

De kamer bleef dus open; de speler kon alleen opgeven of de tijd laten aflopen.

### Hoe de volgende golf start, en wat de server zou kunnen sturen

De quest start golf 1 met `setevt` (make_spire.py:2777; QuestScript.cc:1124, "Sends 6x67"), alleen op de client van de
leider. Elke volgende golf start de client zelf vanuit de kaart (0C na de laatste vijand van de golf). De quest heeft
geen eigen weg om een latere golf te forceren: hij kent alleen het eerste event en eventueel de elite
(make_spire.py:2777-2784) en wacht daarna op r251 of op `if_zone_clear` met het aantal kills (2806-2826).

Vier wegen, en waarom er één gebouwd is:

| Weg | Wat het doet | Zeker uit de bron? | Gebouwd |
|---|---|---|---|
| A. De server telt de nooit gestarte golven achter de afgetelde golf ook als klaar en stuurt de gewone "Room clear!" (r251) | de kamer gaat door, de golven erachter vervallen | ja: alleen serverstand plus het bestaande r251, dat de quest al volgt (make_spire.py:2806-2807) en dat het oude net al stuurde terwijl een vijand nog leefde (journal 26 okt 19:56:10) | ja |
| B. De server stuurt zelf de 6x67 van het volgende event naar alle clients | de volgende golf verschijnt, er vervalt niets | nee: de server stuurt nergens zelf een 6x67 (alleen doorsturen, ReceiveSubcommands.cc:3552). Welke velden de client controleert (`client_id`, het entity-id in de kop, CommandFormats.hh:4596-4602) en of een client die de vorige golf nog ziet leven het event start, staat niet in de bron | nee |
| C. De server doodt de vijand op de clients met een 6x0A met vlag 0x800 | de golf eindigt op de client zoals bij een echte kill, de client stuurt zelf 6x76 en 6x67 | nee: de bron zegt "for most enemies" en "doesn't work on other Episode 1 bosses" (CommandFormats.hh:3756, ReceiveSubcommands.cc:3636-3638), gemeten door de maker van newserv op een andere client. Voor een vijand in een muur of warp, of een vijand die op de client nooit verscheen, is niets bekend | nee |
| D. De server zet een register, de quest van de leider doet daarop `setevt` van het volgende event | de volgende golf verschijnt langs de weg die golf 1 al gebruikt | het dichtst bij zeker (zelfde opcode als golf 1, en de losse elite draait al naast golf 1), maar het is questwerk met een vrij register en het moet op de Wii gemeten worden | nee, buiten mijn schrijfgebied |

Risico van A: klopt het aftellen niet (de vijand was toch bereikbaar), dan vervallen er golven en komt de clear terwijl
er nog een vijand leeft. Daarom geldt voor deze stap dezelfde strenge voorwaarde als voor het net, opnieuw getoetst op
het moment zelf. Wie de waves wil behouden, kiest D als vervolg; dat is een questwijziging met een Wii-meting.

### Wat er gebouwd is

- Telt een net een golf of event van een kamer af, dan onthoudt de server die kamer (`spire_net_rooms`).
- De nawacht geeft in zo'n kamer niet meer op bij de eerste blik. Bij elke blik (om de 30 seconden) berekent hij de
  events die nooit gestart zijn en waar alleen nog de trigger van een klaar event naar wijst, door de keten heen
  (`spire_unstarted_chain`: de golf erna, die daarna, een geketende elite-golf). Houden alleen die de kamer nog open,
  en is de vloer op dat moment 120 seconden stil met een client erop, dan telt hij ze als klaar en loopt de gewone
  kamercontrole: r251, "Room clear!", bewaarpunt.
- Zonder gevecht in de tussentijd is dat 30 seconden na het aftellen van de golf, dus ongeveer 150 seconden na de
  laatste treffer (liep er in die kamer al een nawacht, bijvoorbeeld voor een losse elite, dan valt zijn eerstvolgende
  blik binnen die 30 seconden). Komt er vóór die blik toch een teken van een gevecht (de vijand bleek bereikbaar), dan
  wacht de nawacht opnieuw de volle 120 seconden stilte af; start de client de volgende golf alsnog zelf, dan is er
  geen keten meer en loopt alles als gewoon.
- Hetzelfde geldt voor het tweede net: een event "zonder levende vijand" midden in een keten kon voorheen nooit
  afgeteld worden (zijn trigger hield de kamer open, 6766 op 9e40c01); nu gaat de keten erachter mee.
- De regel voor de speler: is de vloer 45 seconden stil terwijl een net een golf bewaakt, dan leest elke speler op die
  vloer één keer per kamer: "Enemy out of reach? Wait: the room opens by itself." (`SPIRE_STUCK_NOTICE_TEXT`). Deze
  regel zit in de commit van 2-M2, omdat hij aan dezelfde blik hangt.

Niet gedekt door deze stap: een golf die echt verslagen is maar waarvan de 6x67 van de volgende golf nooit komt (geen
net in het spel): de nawacht geeft daar nog steeds op bij de eerste blik, als voorheen.

## 2-M3: een kind dat sterft telt de ouder als dood (alleen gemeten)

### De keten (9e40c01): klopt in de code

- ReceiveSubcommands.cc:3501-3510: een 6x76 op een kind wordt omgeleid naar de ouder (`parent_index = enemy_index -
  child_index`) en daar gaat `set_flags |= cmd.flags` op, dus ook vlag 8.
- 3527-3528: daarna de golfcontrole voor de golf van de ouder; is elke ouder van die golf dood, dan zijn de events
  klaar en loopt `spire_bridge_check_room_clear`.
- SpireFlagBridge.cc:6540-6546: heeft de golf een baas, dan geen "Room clear!", maar wel
  `spire_checkpoint_record_room(l, floor, room, true)`: het bewaarpunt van de baaskamer en de uitbetaling van de
  kamer.
- Heeft de golf geen baas, dan volgt de gewone kamercontrole en bij een klare kamer r251 en "Room clear!" (6560-6584).
- ReceiveSubcommands.cc:3520-3522 kijkt na de omleiding naar het type van de ouder; alleen Vol Opt fase 2 geeft daar
  een baas-clear (SpireFlagBridge.cc:6830-6832), en die heeft geen kinderen (Map.cc:4865-4867).

Of de client een 6x76 voor een kind stuurt, staat niet in de bron.

### Welke vijanden in de kamers van de run een kind hebben

Uit quests/spire/spire.json en spire-ep2.json (worktree veilig-1, d6fc2ef) en Map.cc:4718-5055. De aantallen zijn
de standaardaantallen van de server; mapgen zet het aantal kinderen van een vijand die de run plaatst op 0, en dan
geldt die standaard (quests/lib/mapgen.py:166-170, 276-281). De gebouwde kaarten zelf heb ik niet geopend.

| Vijand | Kinderen op de server | Waar in de run |
|---|---|---|
| Pan Arms | Hidoom, Migium (2) | k7 Caves 3 (vloer 5, kamer 50): golf 502 en 503 |
| Sinow Beat / Sinow Gold | 4 | k9 Mines 1: golf 602 (2x); k10 Mines 2: golf 701 (2x), 702 (1x), elite 709 (2x) |
| Canane | 8 Canadines (de ring) | k9 elite 609; k10 golf 702 |
| Chaos Sorcerer | Bee R, Bee L (2) | k12 Ruins 1: elite 809; k14 Ruins 3: golf 1001; k20 Spaceship Beta: golf 2003 |
| Sinow Berill / Spigell | 4 | k22 Central Control Area: golf 2202 (3x); k23 Jungle North: elite 2309 |
| De Rol Le (baas) | 10 lijfdelen, 9 mijnen | k8 |
| Vol Opt fase 1 (baas) | 6 pilaren, 24 monitoren, kern en meer (0x23) | k11 |
| Dark Falz (baas) | 0x1FD Darvants, plus drie aliassen van de fasen | k15 |
| Barba Ray (baas) | 10 gewrichten, 36 Pig Rays, 1 (0x2F) | k18 |
| Gol Dragon (baas) | 5 | k21 |
| Olga Flow (baas) | 0x200, allemaal alias van de ouder | k30 |

Geen kinderen: Dragon (k4), Gal Gryphon (k27), Vol Opt fase 2, en alle andere vijanden van de run (Booma, Rappy,
Wolf, Shark, Poison Lily, Nano Dragon, Grass Assassin, Dubchic, Gillchic, Canadine, Garanz, Dubwitch, Dimenian,
Dark Belra, Claw, Delsaber, Chaos Bringer, Gee, Merillia, Gibbon, Mericarol, Gibbles, Gi Gue, Dolmolm, Deldepth,
Morfos, Sinow Zoa). Monest en Pofuilly Slime hebben kinderen maar staan niet in de run.

### Wat er misgaat als het bericht wel komt

- Baaskamer (De Rol Le, Vol Opt fase 1, Dark Falz, Barba Ray, Gol Dragon, Olga Flow): bij het eerste kind dat sterft
  (de eerste Darvant, mijn, monitor of Pig Ray) krijgt de baas vlag 8, is de baasgolf "klaar" en schrijft de server het
  bewaarpunt van de baaskamer en betaalt hij de kamer uit, terwijl de baas nog leeft. Sterven de spelers daarna, dan
  hervat de run ná de baas.
- Gewone kamer: sterft een Bee, een Canadine uit de ring, Hidoom of Migium, dan telt de Chaos Sorcerer, Canane of Pan
  Arms als verslagen. Zijn de andere vijanden van de laatste golf ook dood, dan komt "Room clear!" terwijl de ouder nog
  leeft. Bij de Pan Arms is dat hooguit één deel te vroeg (na het eerste van de twee).
- Het raakt ook het net van 2-M2 niet: een kind dat geraakt wordt telt daar nu als treffer op de ouder, dus het net
  blijft af; maar deze omleiding van vlag 8 is een ander pad en is ongemoeid gelaten.

### Welke regels in het serverlog het beslissen

Zoek in het journal van een echt potje in een kamer uit de tabel:

1. `Client set set flags 0008 on E-xxx but it is a child (n); redirecting to E-yyy` (ReceiveSubcommands.cc:3506-3507).
   Staat die er, dan stuurt de client het bericht voor een kind. Staat er bij een vlag anders dan 0008 (bijvoorbeeld
   0002), dan komt het bericht wel maar niet als nederlaag, en gaat er niets mis.
2. Direct daarna, in een baaskamer: `Spire bridge: wave N of room R on floor FF has a boss; no room clear`
   (SpireFlagBridge.cc:6542) gevolgd door `Spire checkpoint: room K cleared in q...` terwijl de baas nog niet dood is
   (bij Dark Falz: vóór de regel `Spire bridge: DARK_FALZ_2 defeated` of `DARK_FALZ_3 defeated`).
3. Direct daarna, in een gewone kamer: `Spire bridge: room R on floor FF cleared in q...; sent "Room clear! ..."`
   terwijl de 6x76 van de ouder zelf (`Client set set flags 0008 on E-yyy (flags are now 0008)` zonder de regel van
   punt 1 ervoor) nog niet geweest is.

De fixlijst meldt dat "but it is a child" in het hele journal 0 keer voorkomt. Dat zegt alleen iets over de vijanden
die in die periode gespeeld zijn: is k7 (Pan Arms), k9 of k10 (Sinow, Canane) in dat journal uitgespeeld, dan stuurt
de client voor hun kinderen geen 6x76 met dit pad. Voor Dark Falz, de Chaos Sorcerer en de bazen van episode 2 zegt
die nul niets, want er staat geen regel van hun vloer in. Een nul bewijst bovendien alleen dát de regel ontbreekt; een
potje in k12 met de elite, k14 of k15 beslist het.

## Wat een speler meemaakt

Vóór de fix:

- Je slaat een golf bijna leeg, loopt naar de laatste vijand achterin of vecht tegen de twee helften van een Pan Arms,
  en na dertig seconden zegt het spel "Room clear!" terwijl die vijand nog leeft en je aanvalt tijdens de beloning.
- Zit een vijand echt vast in een muur in de eerste of tweede golf van een kamer, dan gebeurt er niets meer: geen
  volgende golf, geen clear. Je kunt alleen opgeven en bent de run kwijt.

Na de fix:

- Zolang er gevochten wordt (jij raakt iets, iets raakt jou) telt het spel niets af, hoe lang het ook duurt.
- Zit een vijand echt vast en kan niemand iets raken, dan lees je na driekwart minuut "Enemy out of reach? Wait: the
  room opens by itself." Na twee minuten stilte telt het spel hem af. In de laatste golf komt dan "Room clear!". In een
  eerdere golf komt "Room clear!" een halve minuut later en vervallen de golven die nog moesten komen; je krijgt de
  beloning van de kamer en gaat door.
- Het wachten is langer dan voorheen (twee minuten in plaats van een halve). Dat is de prijs voor geen te vroege clear.

## Tests

Opdracht: `build/newserv run-static-tests`, volledige uitvoer gelezen. In de blokken hieronder is alleen het
proces-id en de datum uit de regelkop weggelaten.

### Basis (9e40c01, de eerste build)

- `build/newserv help 2>&1 | head -1`: `newserv-9e40c01 built 2026-11-11 13:33:33.000000 UTC`
- 14015 regels, 10 W-regels, 0 E-regels, slotregel `-- All static tests passed`, exit 0.
- De 10 W-regels horen bij bestaande tests van de bewaker en het bewaarpunt (7 keer "Spire guard: ... unreadable" of
  "not stored", 3 keer "Spire savestate: run mag ledger ... has lean"). Ze staan in elke latere meting ongewijzigd
  (vergeleken na het weglaten van proces-id en tijd: geen verschil).

### Rood

De drie testblokken staan in src/StaticTests.cc (2314, 2367 en 2439). Rood is gemeten op een build van de eindbron
waarin voor telkens één functie de logica van 9e40c01 teruggezet was, gekozen met een omgevingsvariabele
(`V1B_RED=quiet`, `child` of `chain`). Die schakelaar heeft alleen in die ene, niet gecommitte build gestaan
(`/usr/bin/grep -c V1B_RED src/SpireFlagBridge.cc` geeft 0 in elke commit). Dezelfde build zonder schakelaar: exit 0,
`-- All static tests passed`.

Wat er per schakelaar terugstond:

- `quiet`: `spire_unhit_net_look` telt af zodra de golf alleen nog door nooit-geraakten wordt opgehouden, zoals het
  oude net bij zijn blik na 30 seconden (9e40c01 SpireFlagBridge.cc:6656-6667).
- `child`: `spire_wave_enemies` leest "geraakt" alleen van de ouder (9e40c01 SpireFlagBridge.cc:6613-6617).
- `chain`: `spire_unstarted_chain` geeft niets terug, zoals de nawacht die opgaf (9e40c01 SpireFlagBridge.cc:6785-6788).

2-M2, de voorwaarde (`V1B_RED=quiet build/newserv run-static-tests`, exit 1):

```
I 14:56:48 - -- Spire bridge: the never-hit net waits for a floor without a fight (veilig-1, 2-M2)
E 14:56:48 - run-static-tests: failure at /home/m4rck/code/worktrees/newserv/veilig-1b/src/StaticTests.cc:2320: A::WAIT != spire_unhit_net_look(true, seconds(30), true).action
```

2-M2, treffer op een kind (`V1B_RED=child`, exit 1; regel 2420 is de eerste toets na de treffermelding op Hidoom):

```
I 14:56:49 - -- Spire bridge: a hit on a child is a hit on its parent for the nets (veilig-1, 2-M2)
E 14:56:49 - run-static-tests: failure at /home/m4rck/code/worktrees/newserv/veilig-1b/src/StaticTests.cc:2420: !(!spire_wave_held_only_by_unhit(wave(3)))
```

2-M1, de keten (`V1B_RED=chain`, exit 1):

```
I 14:56:49 - -- Spire bridge: behind a wave a net counted the room goes on (veilig-1, 2-M1)
E 14:56:49 - run-static-tests: failure at /home/m4rck/code/worktrees/newserv/veilig-1b/src/StaticTests.cc:2446: !(V{2} == spire_unstarted_chain({E{111, false, true, {112}}, E{112, false, true, {113}}, E{113, false, false, {}}}))
```

### Groen

Commit c5f7141 (2-M2), apart gebouwd: `build/newserv help 2>&1 | head -1` gaf `newserv-c5f7141 built 2026-11-11
13:58:15.000000 UTC`, werkboom schoon. Statische tests: 14017 regels (basis plus de twee nieuwe kopregels), 10
W-regels gelijk aan de basis, 0 E-regels, exit 0:

```
I 15:05:58 - -- Spire bridge: the never-hit net waits for a floor without a fight (veilig-1, 2-M2)
I 15:05:58 - -- Spire bridge: a hit on a child is a hit on its parent for the nets (veilig-1, 2-M2)
I 15:06:03 - -- All static tests passed
```

De eindbron (gelijk aan commit 8ff540e op één tekst na: de regel voor de speler was toen nog "Enemy out of reach? Wait
without attacking: the room opens by itself."; commit c015651 wijzigt daarna alleen een commentaarregel), gebouwd vóór
het splitsen in commits: 14018 regels, 10 W-regels, 0
E-regels, exit 0:

```
I 14:55:37 - -- Spire bridge: the never-hit net waits for a floor without a fight (veilig-1, 2-M2)
I 14:55:37 - -- Spire bridge: a hit on a child is a hit on its parent for the nets (veilig-1, 2-M2)
I 14:55:37 - -- Spire bridge: behind a wave a net counted the room goes on (veilig-1, 2-M1)
I 14:55:43 - -- All static tests passed
```

Commit 8ff540e en c015651 zijn niet apart gebouwd. De eindbuild is van de commit met dit rapport (de laatste commit van de
tak); zijn eerste regel van `help`, de slotregel en de exitcode van de statische tests staan in de terugmelding aan de
orchestrator, omdat ze pas na deze commit te meten zijn.

## Dekking

Dit dekt: de voorwaarde van beide vangnetten (stilte op de vloer, speler aanwezig, treffer op een kind) en wat er na
het net gebeurt in een golf die niet de laatste is, met statische tests op de serverlogica (de beslisfuncties, de
gevechtsklok, de vijandenlijst uit een echte kaartstand met een Pan Arms en een Chaos Sorcerer, en de keten van
events).

Niet gedekt: wat de GameCube-client doet met een vijand die de server dood verklaart (alleen de Wii beslist), of de
client de treffers op Hidoom en Migium met hun eigen nummers meldt, of de regel op het scherm goed leesbaar is, de
bewaker, de back-up, en 2-M3 (alleen gemeten, niet gebouwd). De wachtlussen zelf (de coroutines met hun timers en het
versturen van de regel) zijn niet door een test gedekt, alleen de beslissing die ze nemen. Er is geen labrun en geen
potje gedraaid.

## Beslissingen (voor decisions.md; de orchestrator schrijft die deze ronde)

- De voorwaarde van het net is "120 seconden geen teken van een gevecht op de vloer, met een speler erop", niet
  alleen een langere tijd en niet een menukeuze. Dekking: opdracht punt 1 (drie genoemde vormen, "motiveer met wat de
  server werkelijk kan zien"), critic systeem 2 bezwaar 2 ("kleinste fix: tijd van de laatste vijand-activiteit per
  kamer"). Klasse 2, omkeerbaar.
- Een geraakte of gestorven speler telt als teken van een gevecht. Gevolg: een onbereikbare vijand die de spelers
  blijft raken (een schutter in een muur) laat het net niet afgaan zolang zij in zijn bereik blijven. Gekozen omdat een
  te vroege clear erger is dan een later net (opdracht punt 1).
- 2-M1 is weg A (server-side overslaan), niet een commando naar de client. Dekking: opdracht punt 2 ("bouw het dan
  NIET blind"), critic systeem 2 bezwaar 1 optie (b).
- De regel voor de speler en zijn tekst zijn nieuw. Dekking: opdracht punt 2 ("dat de speler een regel leest"). De
  woorden zijn aan de regie.
- De haken voor de gevechtsklok staan in ReceiveSubcommands.cc (zeven regels in de handlers van 6x0A, 6xE4, 6x9C,
  6x76, de treffermeldingen, 6x4A tot 6x4C en 6x4D). Dat is het directe aanroeppad van het net; geen van de functies
  van de andere bouwer is geraakt.

## Gezien, niet gebouwd

- vastloper · een golf die echt verslagen is maar waarvan de 6x67 van de volgende golf nooit komt, laat de nawacht nog
  steeds opgeven bij de eerste blik · SpireFlagBridge.cc `spire_dead_event_wait` · een eigen voorwaarde en een meting
  of dit voorkomt
- vastloper · een golf die via "construct enemies" (09/0D) volgt krijgt geen nawacht · SpireFlagBridge.cc
  `spire_dead_event_watch` · komt in de kaarten van de run niet voor (mapgen schrijft alleen 0C en 01)
- datakwaliteit · "ooit geraakt" leest de 6x0A-vlaggen van nu; een latere 6x0A met vlaggen 0 en schade 0 maakt een
  vijand zonder treffermelding weer "nooit geraakt" · ReceiveSubcommands.cc:3622-3623 · een eigen vlag "ooit geraakt"
  op de vijand
- vastloper · een onbereikbare vijand die spelers blijft raken houdt het net tegen tot zij uit zijn bereik gaan; de
  regel op het scherm zegt dat niet · `spire_bridge_note_fight` in `on_change_hp` · een tweede regel of een eigen
  grens
- spelverloop · weg D (register plus `setevt` in de quest) behoudt de golven in plaats van ze over te slaan · quest
  make_spire.py rond 2777-2826 · questwerk, een vrij register en een meting op de Wii
- itemveiligheid · 2-M3: een 6x76 met vlag 8 op een kind telt de ouder als dood, ook voor het bewaarpunt van een
  baaskamer · ReceiveSubcommands.cc:3501-3510 · eerst het journal van een potje in k12, k14 of k15
- lab · lab/e2e_lab.py:472-474 rekent op het net na 30 seconden; het net komt nu pas na 120 seconden stilte (de
  gezochte logregel "of room N on floor FF running without a living enemy" bestaat nog) · quests-repo · de wachttijd
  van de bot aanpassen als het lab weer draait
- werkwijze · de commit-haak meldt dat voor deze newserv-worktree geen integratiebranch en uitrol zijn vastgelegd ·
  .claude/omgeving.json of CLAUDE.md van de newserv-repo · een regel in de repo; ik heb niets gepusht of samengevoegd

## Commits en bestanden

- c5f7141 `fix: never-hit net waits for a quiet floor and counts child hits`: src/SpireFlagBridge.cc,
  src/SpireFlagBridge.hh, src/ReceiveSubcommands.cc, src/StaticTests.cc
- 8ff540e `fix: room goes on behind a wave the never-hit net counted`: src/SpireFlagBridge.cc, src/SpireFlagBridge.hh,
  src/StaticTests.cc
- c015651 `docs: net comment quotes the log line as the server writes it`: src/SpireFlagBridge.hh (alleen commentaar)
- deze commit: dit rapport

Niet aangeraakt: `spire_guard_*`, `spire_savestate_write`, src/InventoryBackup.*, src/SpireDrops.*.
