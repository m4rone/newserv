---
slug: mech-ranger-types
base: 92869f9
---

## Doel
Marck, de speler en eigenaar (16 nov 2026, 20:50-20:56): "en de rewards voor alle andere types aan een aparte agent
geven." en "het mag natuurlijk altijd een keuze blijven iemand mag ook lol hebben in een run." Eerder die avond: "er
moet gewoon een design liggen per class en wellicht zelfs per type ventje want er zijn 4 type venjtes per class die
allemaal hun eigen specifieke hoek hebben."

Speelt hij een RAmar of RAmarl, dan ligt er straks een spellschijf waar nu vier Dimates of Trimates liggen: bij een
RAmar vooral Shifta, Deband en Resta, bij een RAmarl ook Jellen en Zalure, en tot een hoger level. "Nieuwe lijn" geeft
een ranger een ander schietwapen, geen sabel of speer. De androids merken niets: zij houden de genezing. De schijf in
het menu komt pas als de bundel R18 en R31 tot serverregel maakt; dit stuk legt de gewichten. Ontwerp:
`new-features/roguelike/design/mechanics/ranger-types.md` § 1, § 2, § 3, § 6 en § 7.

Wat de wijziging doet (haak H3: een soort kiest tussen kandidaten, een lijn, een techniek of een unit, en de tabel
`spire-types-ra.json` geeft per kandidaat een gewicht):
- De aanmelding spreekt alleen voor de vier rangers (RAmar, RAmarl, RAcast, RAcaseal); voor een hunter of een force
  geeft hij gewicht 1. Met z'n tweeën weegt elk aanbod naar het type van de speler die het krijgt.
- **Nieuwe lijn:** Handgun, Rifle, Mechgun, Shot en Slicer, gewogen per type (uit de tabel; ontwerp § 6): RAmar vooral
  Rifle en Handgun, RAmarl Rifle en dan Handgun en Mechgun, RAcast Shot en dan Slicer en Mechgun, RAcaseal Mechgun en
  dan Handgun en Rifle. Saber en Partisan krijgen gewicht 0, behalve als hij zelf een slagwapen vasthoudt. De lijn die
  hij vasthoudt valt af, zoals nu.
- **Spell**, alleen RAmar en RAmarl: het grootste deel van de technieken (ontwerp: drie op vier, uit de tabel) is bij
  RAmar Shifta, Deband of Resta en bij RAmarl Jellen, Zalure, Shifta, Deband of Resta; de rest verdeelt zich over de
  andere technieken die het type kan leren. Wat het type niet kan leren komt nooit (Sega's `MaxTechLevels`: RAmar geen
  Jellen of Zalure en hoogstens level 15, RAmarl tot level 20). Het level van de schijf blijft van het schijfpad.
- Een android (RAcast, RAcaseal) krijgt geen spell; op de genezingsregels houdt hij het eigen item van de regel.
- **Units per ranger**, als dit stuk ze meeneemt: gewicht per type en niveau uit de tabel (ontwerp § 2): RAmar Arm en
  Power, RAmarl Legs, Mind en TP, RAcast Power, HP en HP-herstel, RAcaseal Body, HP, weerstand en HP-herstel. Ongeveer
  één op de vier (aandeel uit de tabel) is een unit van een andere ranger. Een android krijgt nooit een unit voor
  spellkracht of mana en nooit Cure/Poison of Cure/Paralysis. Battle-units krijgen van deze tabel gewicht 1: ze
  bestaan alleen als de superkracht Snel slaan (B-ONTWERP-6). Welke units er als kandidaat zijn, komt uit de
  unittabel van verdediging.
- Ontbreekt de tabel of een type erin, dan blijft het gedrag van vandaag (ontwerp § 6).

Uitzetten (integratie kop 7): met `ranger-types` uit, of de uitknop in `spire-types-ra.json` uit, is H3 niet
aangemeld, is elk gewicht 1 en is de trekking dezelfde als vóór dit stuk (Nieuwe lijn gelijk verdeeld over alle andere
lijnen, ook Saber en Partisan).

Grens van het project die dit stuk raakt: een beloning kun je gebruiken op het moment dat je hem krijgt. Een gewicht
kantelt alleen de kans tussen kandidaten die er ook zonder dit stuk waren; het maakt geen item mogelijk dat het type
niet kan dragen of leren.

De wijziging zit in twee repos, beide op tak `feature/mech-ranger-types`:
- newserv `/home/m4rck/code/worktrees/newserv/mech-ranger-types`: `src/SpireRangerTypes.cc/.hh` (H3),
  `system/spire-types-ra.json` en `src/SpireTestsRanger.cc`, de rij van ranger-types in integratie kop 5.
- quest `/home/m4rck/code/worktrees/pso-roguelike/mech-ranger-types`: geen questbestand (de rij noemt er geen), alleen
  het logboek `new-features/roguelike/bouw/ranger-types/`.

## Buiten scope
- De latere stukken: R18 en R31 als soort 4 in de quest (via de bundel), de herstelkaart voor de androids (van
  verdediging) en de vallen (code van verdediging, Sega's regel 2 + level / x per type, B-ONTWERP-5; de aantallen per
  type in deze tabel horen bij dat stuk, uit tot een run op de Wii).
- Master's zwaarder voor androids op Ultimate: vervallen (B-ONTWERP-9).
- De unitrijen zelf: welke units er voor een ranger als kandidaat bestaan, staat in de `units-*.json` van aanval,
  verdediging en de forces (B-ONTWERP-6). De unit-kaart vraagt de gewichten van dit stuk al op (`SpireRewardUnit.cc`
  roept H3 aan); de gewichten voor units horen dus wel bij dit stuk.
- Regels in gedeelde bestanden en in de quest: die legt de bundel uit `bouw/ranger-types/voor-de-bundel.md`.
- De keuze van getallen en balans als zodanig: de gewichten en de aandelen.
- Uitrol naar de pc, herstarten van een newserv en de volle pc-build.
- Wat alleen een run op de Wii bewijst: of de client een ranger de schijf laat leren met de MST-eis uit het
  client-image (niet gemeten), en of HP/Restorate in de run-kopie werkt.
