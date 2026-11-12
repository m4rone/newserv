# BALANS-VLOER3 voortgang

- 2026-11-12 start: tak spike/balans-vloer3 op b305a67. Stap 1 (haalbaarheid) loopt.
- Stap 1 af: geen route op GC v3 midden in een quest (6x30 beschermd, 6xBF/6xD0 alleen BB, geen EXP-opcode,
  SetExtendedPlayerInfo raakt alleen de savebuffer). Niets gebouwd. Rapport: BALANS-VLOER3.md. Klaar.
- B-BAL-23: route 1 bouwen (vaste kamervloer bij laden kamerquest en hervatten). Plan: pure functie in SpireMeta,
  max in spire_level_floor_for_client, padtest in StaticTests (eerst rood met een stomp).
- Test rood met de stomp (StaticTests.cc:10711 bin_at(9) != sent_to(fresh)), groen met de tabel (exit 0). Commit volgt.
