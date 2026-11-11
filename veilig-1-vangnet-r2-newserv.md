# veilig-1 vangnet r2 (newserv) - rapport

Tak spike/veilig, basis 1a00938. Commits: 6d5236e (punt 1), 135080b (punt 2), plus deze rapportcommit.

## Punt 1: een vijand die schiet maar niet te raken is (moet, vastloper)

Fix: elke vloer heeft nu twee klokken. De bestaande vloerklok (elk teken van een gevecht) en een nieuwe vijandklok die
alleen tekens van de vijanden zet: een treffer op een vijand (hit claim), 6x0A, 6xE4, 6x9C en 6x76 op een vijand, ook
op een kind (Pan Arms: Hidoom en Migium zijn zelf vijanden, dus hun treffers zetten de vijandklok). Een speler die
geraakt wordt (6x4A/4B/4C) of sterft (6x4D) zet alleen de vloerklok (`spire_bridge_note_fight(l, floor, false)` in
ReceiveSubcommands.cc). Het net telt af na 120 s stilte op de vloer (zoals nu) of na 300 s zonder een teken van de
vijanden (`SPIRE_UNHIT_ENEMY_QUIET_SECONDS`), allebei alleen als er een speler op de vloer staat. Dat geldt voor beide
netten (de golf-wacht en de wacht op gestarte events zonder levende vijand), want ze lezen dezelfde blik.

Melding: ja, ook op de tweede grens. De melding komt na 45 s van welke stilte ook, dus na 45 s zonder een teken van de
vijanden. Waarom: zonder melding staan spelers die beschoten worden vijf minuten in een dichte kamer zonder te weten
dat die vanzelf opengaat, en geven ze op. Het net loopt alleen als de golf al alleen door nooit-geraakte vijanden wordt
opgehouden, dus 45 s zonder één treffer op een vijand is in dat geval vrijwel altijd een onbereikbare vijand. De tekst
("Wait: the room opens by itself") klopt ook hier.

Rood (fix uitgeschakeld met `false &&` op de tweede grens; 1a00938 zelf kent de nieuwe aanroep niet, dus de test bouwt
daar niet):
```
never-hit net under player hits every 10 s: notice at 45 s, counted at -1 s
run-static-tests: failure at .../src/StaticTests.cc:2619: !(counted_at >= 0)
```
Groen (6d5236e):
```
never-hit net under player hits every 10 s: notice at 45 s, counted at 300 s
-- All static tests passed   (exit 0)
```
Wat de speler merkt: de melding na 45 s, de kamer gaat in het ergste geval 300 s na de laatste kill open (golf-wacht;
de event-wacht kijkt elke 30 s, daar uiterlijk rond 330 s). Raakt iemand intussen een andere vijand op de vloer, dan
begint die 300 s opnieuw. Niemand op de vloer telt nooit af, zoals voorheen.

## Punt 2: een baas nooit via de keten (kan)

Fix: `SpireRoomEvent` heeft `is_boss`; `spire_unstarted_room_chain` zet hem als een vijand van de golf van het event een
baas is, en `spire_unstarted_chain` neemt zo'n event nooit op (ook niet de events erachter).
Rood (zonder de check): `failure at .../src/StaticTests.cc:2736: !(spire_unstarted_chain({E{1, false, true, {2}}, E{2, false, false, {}, true}}).empty())`
Groen (135080b): `-- All static tests passed` (exit 0). Wat de speler merkt: niets met de kaarten van vandaag.

## Afsluiting

- `build/newserv help | head -1`: `newserv-135080b built 2026-11-11 20:39:25.000000 UTC` (schoon, geen +).
- `build/newserv run-static-tests`: exit 0, `-- All static tests passed`, 0 keer "failure" in de uitvoer.
- `git diff --quiet ee0fa66 HEAD -- src/SpireDrops.cc src/SpireDrops.hh`: exit 0, gelijk.
- De twee startlevel-aanroepen in SpireFlagBridge.cc niet aangeraakt. Geen pc, geen labrun.

## Dekking

Deze opdracht dekt: dat het vangnet altijd binnen een vaste tijd aftelt, ook als spelers geraakt blijven worden, en dat
een baas nooit via de keten afgeteld wordt. Niet gedekt: golven die via "construct enemies" volgen (alleen triggers
worden gevolgd), en of een vijand op de Wii echt onbereikbaar is (alleen de Wii).

## Beslissingen (zelf genomen, binnen de opdracht)

- 300 s geldt vanaf het laatste vijandteken op de vloer, niet per golf: de klok is per vloer, zoals de vloerklok.
- Een nieuwe klok start beide tijden op nu (later, nooit eerder), ook als het eerste teken van een speler komt.
- De melding op beide grenzen (zie punt 1).
