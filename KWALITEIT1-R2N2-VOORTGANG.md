# KWALITEIT1-R2N2 voortgang

- 2026-11-03 start: tak spike/kwaliteit-1, HEAD 1c755dc; volgorde N-3, N-2, N-1, K-3/K-2, build.
- N-3 gebouwd: spire_savestate_v_plan, alleen het opslagblok slaat over bij openstaande hervat-items; rood StaticTests.cc:6002 !(kept.record_boss) op de mutatie, groen volgt in de volgende build
- N-2 gebouwd: het vangnet telt pas na het eerste 'J'-itemverzoek (items_asked > 0); N-3 groen (build b), N-2 rood StaticTests.cc:6009 op de mutatie
