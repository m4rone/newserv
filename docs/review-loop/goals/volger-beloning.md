---
slug: volger-beloning
base: 9c8f2a6
---

## Doel
Twee kleine stukken naast deel A van de beloningen, in de Spire-run die Marck met twee spelers speelt.

1. De beloning van de volger verdwijnt niet meer zonder een woord. Beslissing B-REGIE-22-48 (14 nov 2026): "staat de
   trekking van de volger bij de warp nog open, dan neemt de server de eerste optie voor hem en meldt dat in één regel.
   De 60 s en de open warp blijven, zodat niemand op een ander wacht." Reden daar: "een beloning mag nooit stil
   verdwijnen". Uitwerking in B-E02-16 (`new-features/roguelike/decisions.md`, quest-repo): neemt de leider een warp
   terwijl de volger nog niet koos, dan krijgt de volger vlak voor hij volgt de eerste gewone beloning uit zijn lijst,
   of de bonus van Overslaan, en de server zegt dat in één regel. Nooit automatisch: een zeldzame, een vervloekte of
   een serverwapen. Vaste grenzen van het project die hier gelden: geen itemverlies en geen dubbele gift, geen
   vastloper, en geen item maken op een client terwijl daar een golf loopt (B-REGIE-14-10).
2. Eigen herstartscripts herstarten de pc-server niet meer terwijl iemand speelt. CLAUDE.md: "The pc newserv is never
   restarted or reconfigured while a game is running (check the journal first); restarts and config changes go only
   through `apply-on-pc.sh` at zero connected players", "from every session, including ones outside the roguelike
   (2026-10-25: a restart at 12:37 dropped Marck mid-run)".

De wijziging beslaat twee repos: newserv (deze repo, tak feature/klein-naast-a, base 9c8f2a6) en de quest-repo in
`/home/m4rck/code/worktrees/pso-roguelike/klein-naast-a` (tak feature/klein-naast-a, base 0d14b8db).

## Buiten scope
- Deel A en deel B van de beloningen zelf (wapens, mag-regel B, schijven, krachten).
- De beloning van de leider die zelf een pijp neemt zonder te kiezen.
- Uitrol naar de pc of het lab, en herstarten van een newserv.
- `~/pso-tools/quests/apply-yasha-on-pc.sh` (staat buiten git).
