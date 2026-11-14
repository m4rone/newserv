# veilig-2-ruil newserv — voortgang

- 2026-11-11 start op spike/veilig-ruil 37bd1f8. Stap 1: nameting ruil/verkoop.
- Nameting klaar (D0/6x29/D2-D3 in ReceiveCommands.cc en ReceiveSubcommands.cc). Fix + tests gebouwd; groen 1e run exit 0; rood met stubs vastgelegd.
- 5df8eb7 gebouwd en groen (exit 0, GUARD E2E 16, FAILED 0). Rapport geschreven.
- Correctie orchestrator: 6x29 alleen verandert niets, ruil pas bij uitvoering volgens D0. Groen exit 0, GUARD E2E 16, FAILED 0. K7-deel niet gebouwd (andere bouwer).
