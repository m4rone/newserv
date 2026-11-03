# KWALITEIT1-R2N2 — tweede critic, newserv-kant (N-3, N-2, N-1, K-3, K-2)

Tak spike/kwaliteit-1, start 1c755dc. Bron: kwaliteit-1-bundel1-critic-r2.md (N-1, N-2, N-3, K-2, K-3).

Dekking: dit dekt N-3, N-2, N-1 (en K-3/K-2 waar goedkoop) uit de tweede critic, elk met een StaticTest rood naar
groen op de laptop. Dit dekt niet: clientgedrag op de Wii, de quest-check-fix (andere agent), en problemen buiten
deze punten.

Werkwijze rood/groen: per punt eerst de helper met een mutatie die het oude gedrag nabootst (`// MUTATION`), de test
erachter, build, `./build/newserv run-static-tests` rood; dan de fix. De tests van punt n+1 staan in StaticTests.cc ná
die van punt n, dus de rode run van n+1 is tegelijk de groene run van n (de fout valt later in het bestand).

## N-3 — 'V' met openstaande hervat-items slaat alleen het opslaan over (commit 1bb72c7)
- Vorm: `spire_savestate_v_plan(owed)` -> `{store, follow_ledger, record_boss, burst_text}`; de 'V'-handler rekent
  `owed` uit, logt "not stored", en loopt daarna gewoon door: ledger-follow, (alleen bij `store`) ss-bouw + bestand,
  `record_boss(room_k - 1)`, burst-tekst. Waarom: één helper maakt het verschil toetsbaar zonder de handler te draaien.
- Rood (mutatie: alles `!owed`, het oude early-return-gedrag), exit 1:
  `run-static-tests: failure at .../src/StaticTests.cc:6002: !(kept.record_boss)`
- Groen: de run van N-2 hieronder faalt pas op :6009, dus :6000-6007 (N-3) passeren.

## N-2 — het equip-vangnet telt pas vanaf de itemlus (commit 873f587)
- Vorm: `spire_resume_equip_start_due(..., items_asked)`: de 15 s-tak vuurt alleen als `items_asked > 0` (het eerste
  'J'-item); `resume_item_last` wordt bij elk itemverzoek ververst, dus de klok telt vanaf het laatste itemverzoek.
  De laatste-ack-tak blijft ongewijzigd. Waarom: de critic-variant, één voorwaarde, dekt nog precies het ronde-1-geval.
- Rood (mutatie: `items_asked` genegeerd), exit 1:
  `run-static-tests: failure at .../src/StaticTests.cc:6009: !(!spire_resume_equip_start_due(false, 20000, false, true, true, 0))`
- Groen: de run van N-1 faalt pas op :6013, dus :6009-6011 passeren (20 s zonder itemverzoek -> niets; laatste ack -> equip;
  vastgelopen lus na het eerste item -> vangnet).

## N-1 — een hervat-item dat niet past zegt het éénmaal (commit 46f53ba)
- Vorm: `spire_resume_full_pack_text_due(asked, index, pack_items, capacity, sent)`: her-vraag (`index + 1 <= asked`)
  én volle tas in de run copy, éénmaal per hervatting (`SpireClientRun::resume_full_text_sent`, gereset bij het
  startantwoord). Regel: `spire_resume_full_pack_text(name)` = "Pack full: your saved <naam> waits. Sell or drop
  something." (naam uit de item name index, anders "item"), via `spire_send_ticker`, geen venster.
  Waarom de volle-tas-voorwaarde: een vloerlading tijdens de lus (de teleporter in de stad) laat de quest het lopende
  item ook opnieuw vragen; zonder die voorwaarde kwam de regel vals in de gewone hervatting.
- Rood (mutatie: altijd vals), exit 1:
  `run-static-tests: failure at .../src/StaticTests.cc:6013: !(spire_resume_full_pack_text_due(5, 4, 30, 30, false))`
- Groen: eindrun hieronder.

## K-3 — docregel (in commit 1bb72c7)
- HOOKS.md staat in de quest-worktree (buiten de writeSet). Daarom: de "not stored"-logregel zelf zegt nu
  "(expected while the resume item loop runs)" en noemt de kamer; plus een codecommentaar bij de 'V'-handler.

## K-2 — niet gebouwd
- Een `resume_items_watching`-vlag moet op elk uitgangspad van `spire_resume_items_watch` worden teruggezet; dat is
  geen één regel. De critic: "Laten mag" (begrensd, 10 x 500 ms, weak_ptrs). Open voor de gebruiker.

## Eindbuild en tests
- `unlink src/Revision.cc`, `make -C build -j16 newserv` (nohup + wacht-op.js): exit 0. `src/Revision.cc`:
  `GIT_REVISION_HASH = "46f53ba"` = HEAD van de code.
- `./build/newserv run-static-tests`: exit 0, `-- All static tests passed`.
- Niet op de Wii en niet in het lab gemeten; de pc niet aangeraakt; niet gepusht.

## Beslissingen
- N-1: extra voorwaarde volle tas (zie boven), zodat de regel niet vals komt na een vloerlading tijdens de lus.
- K-3 als logtekst + commentaar in plaats van HOOKS.md (buiten de writeSet); zit in de N-3-commit omdat het
  dezelfde logregel is.
- K-2 niet gebouwd (niet in één regel).
