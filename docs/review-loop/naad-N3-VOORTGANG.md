# Naad N3: voortgang

Start 2026-11-16, kop 80c2046, tak feature/beloningen-b (schoon bij aanvang).

- [bezig] voorbereiding: doel, ontwerp §3/§5/§7/§9, N1- en N2-stand en de te verhuizen code lezen
- [af] ontwerp gekozen; src/SpireRewardKinds.hh (gedeelde interne header) en de soorttabel + schakelaars in src/SpireMechanics.hh/.cc geschreven; nog niet gebouwd
- [af] stap a geschreven (gen.py in de scratchmap schrijft SpireRewards.cc en SpireFlagBridge.cc uit 80c2046): verdeling vraagt de tabel, soorten 1-5 aangemeld vanuit hun oude plek; bouw exit 0, vingerafdruk groen; StaticTests kind_known-verwachting aangepast (6-8 bekend, 9 niet)
- [af] tweede verwachting in StaticTests.cc (B8-lader: srv 6 was 'geen soort') hangt aan dezelfde verbreding: het onbekende voorbeeld is nu srv 9; zelf beslist, staat in het rapport
- [af] stap a groen: bouw exit 0 (0 waarschuwingen), run-static-tests 'All static tests passed', spire-follow-city 'ok: spire-lives-check'
- [af] commit 1a931b1: stap a (soorttabel, schakelaars, enum, verdeling; soorten nog op hun oude plek)
- [af] soort 1 en 2 in src/SpireRewardWeapon.cc (schakelaar aanval): bouw exit 0, All static tests passed, vingerafdruk groen, ok: spire-lives-check
- [af] commit 481f673 (wapen); soort 3 in src/SpireRewardMagFood.cc (schakelaar mags): bouw exit 0, All static tests passed, vingerafdruk groen, ok: spire-lives-check
- [af] commit 5c33319 (mag-voer); soort 4 in src/SpireRewardDisk.cc (schakelaar force.schijf): bouw exit 0, All static tests passed, vingerafdruk groen, ok: spire-lives-check
