---
slug: audit-slice-c
base: 80e8986
---

## Doel
- S-12: follow-the-leader warpt geen client die nog laadt (LOADING-vlag of floor 0x0F).
- S-3: Challenge-overlay onder het vangnet of geweigerd voor beschermde karakters (SpireBlockedCharacters).
- S-6: registerbestand (registers 232-247, SPIR) per karakter, zoals flags en records.
- Q2-2: SpireDraw::from_json_file begrenst entries op <=40 en slot_c_zero_tags op <num_tags.
- StaticTests voor kamer-clear, draw en meta.

## Buiten scope
- Uitrol naar pc of lab; quest-scripts in pso-roguelike.
