# Naad N2: voortgang

Start 2026-11-16, kop 2cfb90c, tak feature/beloningen-b (schoon bij aanvang).

- [bezig] voorbereiding: doel, ontwerp §3/§5/§9, N1-stand en `run_static_tests()` lezen
- [af] SpireMechanics.hh/.cc (aanmelding van tests), SpireTestSupport.hh, glob in CMakeLists.txt, aanmelding in SpireTestsSeam.cc geschreven; nog niet gebouwd
- [af] vier testbestanden en de nieuwe StaticTests.cc gegenereerd uit 2cfb90c met een verhuislijst (scratch: manifest.json, gen.py); nog niet gebouwd
- [af] gebouwd (exit 0, geen waarschuwingen); run-static-tests groen met de vijf aangemelde testbestanden (attack, defence, force, mags, seam)
- [af] bewijs: verify.py (scratch) alle controles ok; tellingen 4653 voor en 4653 na (StaticTests 4054 + Attack 35 + Defence 34 + Force 87 + Mags 443), wacht apart 17
- [af] een verhuisde test bijt: 40u naar 41u in SpireTestsForce.cc:42 gaf exit 1 met "failure at ...SpireTestsForce.cc:42"; teruggezet (cmp gelijk), herbouwd
