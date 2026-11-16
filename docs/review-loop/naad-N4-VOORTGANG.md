# Naad N4: voortgang

Start 2026-11-16, kop 655f1cd, tak feature/beloningen-b.

- [bezig] voorbereiding: doel, ontwerp §3/§7/§9, de rapporten van N1 en N3 en de aanroepplekken lezen
- [af] (a) de elf haken in SpireMechanics.hh/.cc (aanmeldlijst per haak, vast volgnummer, schakelaar, één aanroepfunctie) met tests in SpireTestsMechanics.cc; draagveld `sections` in SpireSavestate en SpireMetaFile; cyclus a2: build 0 static 0 city 0, vingerafdruk en savestate-regel aanwezig
- [af] (a) gecommit als 78d4850
- [af] (b) de aanroepregels in de kern: H1 in ReceiveCommands.cc; H4-H7 in ReceiveSubcommands.cc; H2, H8, H9 (drie plekken) en H10 (vier momenten) in SpireFlagBridge.cc; H11 (lezen en schrijven) in SpireMeta.cc; H3 in SpireRewardWeapon/Disk/Unit.cc; tests door de kern heen (H3 via het aanbod, H10 via de savestate-tekst, H11 via het meta-bestand); cyclus b2: build 0 static 0 city 0. Eén commit voor alle kernbestanden samen (één gemeten cyclus), niet per bestand
- [af] (b) gecommit als 039b8d0
- [af] (c) de run-mag in de savestate letterlijk naar src/SpireMagState.cc, aangemeld op H10 als "mags" zonder schakelaar (volgnummer 10): 63 regels verhuisd (schrijfblok 31, leesblok 32, elk aaneengesloten), 2 aanroepregels in SpireFlagBridge.cc kregen een langer commentaar; cyclus c: build 0 static 0 city 0, vingerafdruk en savestate-regel aanwezig
- [af] (c) gecommit als 43f7c7c
- [af] bijttoets: schakelaartoets in each_hook weg -> `build 0 static 1 city 0`, failure at SpireTestsMechanics.cc:255 (e_l != called()); teruggezet (git diff --quiet 0)
- [af] eindcyclus na terugzetten en één testregel (2u -> 2): build 0 static 0 city 0
- [af] (d) rapport docs/review-loop/naad-N4-rapport.md

## Stand

Alle vier de onderdelen af. Niets open voor een verse bouwer.

## Vervolg (2026-11-17): vier punten uit Gezien, niet gebouwd

- [af] punt 3, schakelaarbestand hoogstens eens per seconde: cffa87f, build 0 static 0 city 0
- [af] punt 4, H2 begrenst kamerindex en trede van een aanmelder: b312e6e, build 0 static 0 city 0
- [af] punt 2, H1, H4-H7 en H9 alleen in een Spire-spel (spire_game_is_spire): 2fbe372, build 0 static 0 city 0
- [af] punt 1, de sleutel van een aanmelder met schakelaar uit wordt gedragen (savestate en meta): bca7769, build 0 static 0 city 0
- [af] aanvulling onderaan het rapport

Alle vier af. Niets open voor een verse bouwer.
