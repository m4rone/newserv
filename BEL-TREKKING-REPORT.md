# BEL-TREKKING rapport (spoor beloningen, pakket S-trekking, vondst #8)

Tak spike/bel-trekking, basis 695844f, fix-commit 45f0588 (plus deze rapport-commit).

## De vondst, nagemeten

- Belofte: quest spire.json:291 `sign_elite` "... Rare, +50% meseta."; spire.json:294 `sign_tag` "Next reward: <tag> (slot A of the next pick)".
- Code op 695844f: `spire_draw` zet `force_rare = (in.path == 1)` voor slot 3; `draw_one` kiest de rij van de pijptag, z = 2, en `pick()` valt via 8 + 8 pogingen en de sequentiele loop stil terug op een niet-zeldzame.
- Pool (quest data/rewards.json op efe7703, HU): zeldzaam zijn alleen index 10 Brand +20 Hit 35 (melee, vanaf k5), 17 Fire Saber +20 (vuur/hit/glass, vanaf k3), 24 More Eyes (tempo, vanaf k2, cap 1). Op k2 is More Eyes dus de enige geldige zeldzame; elke andere pijptag gaf in slot A geen zeldzame.

## De wijziging

- src/SpireDraw.cc:228 `valid(id)`: de geldigheid (vanaf_k, tot_k, android_ok, cap/al op het bord), die de loop al gebruikte, staat nu in een helper; de loop roept hem aan op :346 (zelfde uitkomst als de oude expressie).
- src/SpireDraw.cc:238-258 `replace_with_rare(slot, forced_tag, curse)`: alle geldige zeldzame entries van het archetype; geen enkele, dan `false` en niets verandert. Anders uniform een (een enkele: zonder rnd-aanroep, zoals `pick()`), dezelfde vloekregels als nu (eigen vloek wist de vloek, Glass alleen op glass_ok), `record`, en `moved_tag = forced_tag`.
- src/SpireDraw.cc:347: in de geldige tak, alleen als `force_rare && !e.rare`. Dit gebeurt na alle rnd-aanroepen van de bestaande trekking van slot 3, dus de volgorde in het ongewijzigde pad blijft gelijk.
- src/SpireDraw.cc:199 en :270: slot 4 neemt `moved_tag` als forced_tag (0 = de oude gewogen keuze), zodat de pijptag in het menu blijft.
- src/SpireDraw.hh:86-89: commentaar bij `spire_draw`.
- `spire_draw_overview_line` is niet aangepast: hij leest `entries[id-1].rare`, dus "More Eyes (rare)" volgt vanzelf (zie test a).
- src/StaticTests.cc:5517-5612: nieuwe sectie "Spire draw: an elite room gives a rare in slot A, the pipe tag moves to slot B (B-BEL-3, #8)", in de bel_case-stijl van "Spire bundle 8". Een kleine tabel in de test, gemodelleerd op de echte HU-pool rond k2 (Saber +8, Gladius +18, More Eyes, Monomate x3, Fire Saber +20; banden 1/2/3, lege zeldzame rij een trede lager gevuld zoals rewards.py resolve()). Een echte draw-tabel laden kan niet: geen bestaande test doet dat en de newserv-boom heeft geen spire-draw.json.
  - (a) HU k2, elite-pijp A, tag melee: slot 3 = More Eyes (rare), slot 4 = Saber +8 uit de melee-rij, overzicht "Rewards: More Eyes (rare), Saber +8, Monomate x3. Walk to choose."; met 2 levens blijft de getrokken vloek R12 op de vervangende zeldzame.
  - (b) tag tempo op k2 (More Eyes) en tag melee op k3 (Fire Saber): de trekking van voor de fix.
  - (c) More Eyes al op cap: geen geldige zeldzame, slot 3 Gladius +18 zoals voor de fix; een gewone pijp (path 0) forceert nooit.

## Bewijs

Build (in de worktree): `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` en dan `cmake --build build -j8 --target newserv`; exit 0 (build.log.exit), 0 regels met "warning" in build.log.

Groen, met de fix (commit 45f0588), `./build/newserv --config=tests/config.json run-static-tests > st.log 2>&1; echo $?`:

```
exit=0
208:I 997910 2026-10-27 22:17:48 - -- Spire draw: an elite room gives a rare in slot A, the pipe tag moves to slot B (B-BEL-3, #8)
209:I 997910 2026-10-27 22:17:48 -    BEL ((a) melee has no rare at k2: slot 3 is More Eyes, slot 4 carries the melee tag)
210:I 997910 2026-10-27 22:17:48 -    BEL ((b) a tag with a rare at this k: the draw of before)
211:I 997910 2026-10-27 22:17:48 -    BEL ((c) no valid rare (More Eyes at its cap): the draw of before)
212:I 997910 2026-10-27 22:17:48 - -- All static tests passed
```

Rood, met de fix uitgezet (tijdelijk `return false;` bovenaan `replace_with_rare`, niet gecommit, daarna teruggezet; `git diff` daarna leeg), st-red.log:

```
exit=134
210:E 997064 2026-10-27 22:17:31 -    BEL ((a) melee has no rare at k2: slot 3 is More Eyes, slot 4 carries the melee tag) FAILED: failure at /home/m4rck/code/worktrees/newserv/bel-trekking/src/StaticTests.cc:5567: 3u != slot(drawn, 3)
211:I 997064 2026-10-27 22:17:31 -    BEL ((b) a tag with a rare at this k: the draw of before)
212:I 997064 2026-10-27 22:17:31 -    BEL ((c) no valid rare (More Eyes at its cap): the draw of before)
terminate called after throwing an instance of 'phosg::expectation_failed'
  what():  failure at /home/m4rck/code/worktrees/newserv/bel-trekking/src/StaticTests.cc:5611: 0u != bel_failed
```

(b) en (c) zijn groen met en zonder de fix: zo bewijzen ze dat dat pad ongewijzigd is. De bestaande deterministische draw-tests ("Spire draw: table limits and a fixed draw", "Spire bundle 8") blijven groen.

## Wat niet gedekt is, en meldingen

- De quest heeft een eigen reserve-trekking (rewards.py draw_one, :568-600, T_TMP = force rare), die dit niet spiegelt. Niet gewijzigd, zoals gevraagd. Valt de server-trekking weg, dan geeft de quest op k2 bij een andere tag nog steeds geen zeldzame.
- Teksten: spire.json:294 `sign_tag` zegt "(slot A of the next pick)". Na deze fix staat de tag in het vervangende geval in slot B; B-BEL-3 zegt zelf "de tag schuift naar slot B". Die tekst hoort bij een quest-pakket (niet in deze writeSet).
- Niet gedekt: de zeldzame kansen, de pool zelf en de quest-reserve-trekking. Wat de Wii laat zien is niet gemeten.
- Afwijking van de opdracht: `cmake --build build -j8 newserv` geeft "Unknown argument newserv" (build.log, exit 1). Ik heb `--target newserv` gebruikt.
- Hook-melding bij de commit: in de newserv-repo staat geen git-flow (geen integratiebranch en geen uitrol in .claude/omgeving.json of CLAUDE.md). Dat valt buiten deze writeSet en is voor de regie.

## Beslissingen (decisions.md valt buiten de writeSet, dus ze staan hier)

### B-BEL-S1 — Een enkele geldige zeldzame zonder rnd-aanroep
- **Datum:** 2026-10-27 22:15
- **Route:** zelf
- **Type:** technisch
- **Klasse:** omkeerbaar
- **Dekking:** dispatch ("uniform uit alle zeldzame entries ... die nu geldig zijn"); SpireDraw.cc pick() slaat rnd ook over bij een lijst van een
- **Beslissing:** bij een geldige zeldzame wordt die gekozen zonder rnd(1); bij meer uniform met rnd(n), na alle bestaande rnd-aanroepen van slot 3.
- **Waarom:** volgt het bestaande pick()-patroon; uniform blijft uniform.

### B-BEL-S2 — Geldigheid in een helper
- **Datum:** 2026-10-27 22:15
- **Route:** zelf
- **Type:** technisch
- **Klasse:** omkeerbaar
- **Dekking:** dispatch ("dezelfde geldigheid als nu"); rule code-consistentie (niet dupliceren)
- **Beslissing:** `valid(id)` vervangt de inline-expressie in de loop en wordt ook door de vervanging gebruikt.
- **Waarom:** een bron voor de geldigheid, zodat de vervanging nooit afwijkt van de gewone trekking.
