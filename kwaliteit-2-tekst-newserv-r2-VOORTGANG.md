# Kwaliteit 2 (tekst en ux), newserv-kant, ronde 2: voortgang

Tak spike/kwaliteit-2, start b2b38f2. Eén regel per afgerond punt.

- 2026-11-11 10:45 pre-flight: tak spike/kwaliteit-2, HEAD b2b38f2, werkboom schoon, `build/newserv help` meldt newserv-b2b38f2 = HEAD. Critic-rapport (K2, K3, K5, K7) en ronde 1-rapport gelezen.
- 2026-11-11 10:52 basis hermeten: `build/newserv run-static-tests` op b2b38f2 exit 0, 13894 regels, 10 W-regels, 0 E-regels, 0,36 s.
- K5-telling (questkant 25c3a0c, build/spire-map.json en build-ep2/spire-map.json, make_spire.py:828-832): 0xF3 gaat uit 8 kamers; 7 hebben Sega's baasteleporter op de plek van de pijp (k3, k7, k10, k14, k17, k20, k29), k26 (Seaside) houdt onze pijp. Dezelfde 7 vloeren hebben in de gebouwde .dat precies één TObjWarpBoss (type 0019): Ep1 vloer 2, 5, 7, 10; Ep2 vloer 2, 4, 11.
- Gebouwd, groen (exit 0, 10 W-regels, 5,7 s): wachtende regel kijkt bij wakker worden opnieuw (spire_ticker_wake_delay), K2 ('C' 12 laatste clear in de wachtrij), K3 (mag-herinnering kijkt opnieuw), uitrust-controles via dezelfde route, K5 (teleporter bij 0xF3 als de kaart een TObjWarpBoss op de vloer heeft), K7 (echte Client en Lobby door spire_bridge_handle_register).
- Levensduur: gevonden en gefixt met een test dat de controle na SpireEquipItem de game pas las ná het antwoord van de client; een speler die intussen de game verliet kreeg de controle (en mogelijk een regel) op zijn nieuwe plek. Nu krijgt de coroutine de game mee waarin gevraagd is.
- Rood per punt gemeten met acht mutaties (build/r2-red-*.log, elk exit 1 op de verwachte assertie), bron teruggezet, geen MUTATION in de bron; groen op dezelfde bron: exit 0, "-- All static tests passed", 0 E-regels, 10 W-regels (gelijk aan de basis), 0 compilerwarnings.
- 2026-11-11 11:21 code vastgelegd in fbd40a4 (fix: waiting ticker lines look again when they go). K5 voor k11 kant A niet gebouwd: de server kan het niet afleiden, de quest moet een bit meesturen (rapport § K5).
- 2026-11-11 11:26 test losgemaakt van wandklok-ijkpunten (0626572): hij wacht tot de laatste verwachte regel er is en vergelijkt verzendtijdstippen; groen exit 0 in 5,6 s, 10 W-regels; de drie geraakte mutaties (wake, k3, left) opnieuw rood gemeten op StaticTests.cc:7548, :7551 en :7569.
- Rapport geschreven: kwaliteit-2-tekst-newserv-r2.md. Telling: gebouwd 7, niet gebouwd 1 (k11 kant A). De eindbuild op de laatste stand en zijn testuitvoer staan in het antwoord aan de orchestrator.
