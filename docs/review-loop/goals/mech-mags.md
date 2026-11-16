---
slug: mech-mags
base: NAAD
---

## Doel
Marck, de speler en eigenaar (16 nov 2026, 20:50-20:56): "de mags aanval verdediging is echt te beperkt, er zijn
guides over perfecte mags per type ventje uit de community en speciale tpye mags. het mag natuurlijk altijd een keuze
blijven iemand mag ook lol hebben in een run."

Het mag-voer in het beloningsmenu wordt een echte keuze: de regel zegt waar het heen gaat en hoeveel, en na de pauze
groeit zijn mag precies daar. Is zijn mag sterk genoeg, rond de tweede helft van Episode 1, dan kan Epic mag-voer een
zeldzame mag van Sega zijn en Legendary de vierde vorm van zijn type, die hem soms opwekt als hij sterft. Ontwerp:
`new-features/roguelike/design/mechanics/mags.md` § 2, § 3b, § 3c, § 3e, § 5 en § 7.

Wat de wijziging doet:

**Mag-voer** (soort 3, `SpireRewardMagFood.cc`, met de functies in `SpireMag.*`):
- Bij de trekking kiest de server een stat: meestal de hoofdstat van de richting waarin de run-mag groeit (ontwerp:
  60 %), anders een andere stat die het type gebruikt. Een android krijgt nooit MIND.
- De hoeveelheid blijft die van het rooster: Normal +5, Rare +10, Epic +20, Legendary +30 levels op die stat.
- De menuregel noemt stat en hoeveelheid, zoals "RARE Mag DEX +10", in plaats van "Mag food +10".
- Na de keuze groeit de run-mag met die hoeveelheid op die stat; de soort volgt Sega's evolutie (klasse, Section ID).
- Regel B blijft winnen: wat een beloningswapen of -schijf van de mag vraagt, blijft staan; boven 200 levels kort de
  server eerst de stats die het item niet vraagt. Zelf voeren met mates in de run blijft werken.

**Zeldzame vormen** (ontwerp § 3c):
- Epic mag-voer op een mag vanaf level 50 geeft in plaats van levels een celvorm van Sega (Soniti of Pitri, Preta of
  Churel, naar een even of oneven Section ID). Legendary mag-voer vanaf level 50 geeft de vierde vorm van het type, of
  één op de vier keer (uit de tabel) een hartvorm (Chu Chu, Kapu Kapu; Elenor alleen voor androids). Onder level 50
  geven ze +20 en +30 levels. Legendary gaat pas open na de eerste De Rol Le, zoals nu.
- Welke vorm het wordt, zegt Sega's eigen regel in newserv (klasse, geslacht, Section ID); waar `mags.md` § 3d een
  andere vorm noemt, geldt die regel. Een vorm die het type volgens Sega niet mag dragen, komt nooit.
- Een vorm wisselt alleen de soort: de stats blijven, de groei gaat door, de vorm evolueert niet meer, en de groei bij
  een baas zet geen gewone soort terug.

**Bewaren:**
- De savestate (H10, `SpireMagState.cc`) bewaart het voer per stat en de vorm; een hervatte run gaat verder met
  dezelfde mag. Een savestate van vóór dit stuk laadt en de run gaat verder, zonder itemrisico.

Uitzetten (integratie kop 7):
- De uitknoppen van het nieuwe deel in `spire-mag.json` uit (voer per stat, vormen), of die sleutels ontbreken of zijn
  fout: mag-voer en run-mag werken zoals vóór dit stuk ("Mag food +N", geen vorm).
- `mags` uit: soort 3 geeft geen aanbod en de regel geeft zijn eigen item.

Grenzen van het project die dit stuk raakt: geen itemverlies (na de run is de echte mag van het personage precies wat
hij was; de Battle-regel blijft "verboden", `forbid_mags 0`, en elke nieuwe mag gaat langs het bestaande wisselpad van
de run-mag, zonder tweede itempad), en een beloning kun je gebruiken op het moment dat je hem krijgt (regel B).

De wijziging zit in twee repos, beide op tak `feature/mech-mags`:
- newserv `/home/m4rck/code/worktrees/newserv/mech-mags`: `src/SpireMag.cc/.hh` en `system/spire-mag.json` (de
  functies en sleutels die er al staan), `src/SpireRewardMagFood.cc` (soort 3), `src/SpireMagState.cc` (H10) en
  `src/SpireTestsMags.cc`.
- quest `/home/m4rck/code/worktrees/pso-roguelike/mech-mags`: `quests/spire/data/grid/mag.json` en het logboek
  `new-features/roguelike/bouw/mags/`.

## Buiten scope
- De latere stukken: de startkeuze uit vier richtingen bij de kit (quest `spire.json:275`, `make_spire.py:2389-2405`,
  via de bundel), de groei bij elke baas langs de richting met het nieuwe budget, de levels per trede en de vloeren
  per startlevel; synchro, IQ en de PB-meter.
- De verzameling over runs (H11, de meta per personage: een vorm die het personage kreeg komt in een latere run
  eerder terug): het laatste stuk van het ontwerp (§ 7), niet dit stuk.
- Het gebruikte startlevel doorgeven aan de mag (`SpireFlagBridge.cc`, via de bundel; B-ONTWERP-8).
- Regels in gedeelde bestanden en in de quest, zoals de infotekst van de regel Mag food en wat het bord van de
  verzameling toont: die legt de bundel uit `bouw/mags/voor-de-bundel.md`.
- De keuze van getallen en balans als zodanig, zoals het aandeel van de hoofdstat en de kans op een hartvorm.
- Uitrol naar de pc, herstarten van een newserv en de volle pc-build.
- Wat alleen een run op de Wii bewijst: of een zeldzame soort via `item_create2` goed verschijnt, aangaat en zijn
  triggers geeft (ook de kans erop), en hoe het opwekken door de mag samengaat met de levens van de run (B-ONTWERP-7).
