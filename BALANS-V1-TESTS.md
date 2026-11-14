# Balans v1: vijf ontbrekende back-up-tests

Tak spike/balans-v1-tests op 98aad8c. Alleen src/StaticTests.cc veranderd (vijf nieuwe K3-gevallen na "backup r2 M6"); geen productiecode.
Mutanten: dezelfde vervangingen als systeemcritic (mut/run-mutants.py), per mutant een build in build/ en `build/newserv run-static-tests`, daarna het bestand teruggezet.

| Gat | Test | Mutant | Rood (met mutant) | Groen (zonder) |
|---|---|---|---|---|
| 1 | backup bal M1 (login, harde stop en Exit) | m1-login-aanroep (:1407 `from.meseta`) | exit 1, StaticTests.cc:13600 `0u != marked.loss_meseta` | exit 0 |
| 2 | backup bal M2a (6x5D en 6x29 door on_subcommand_multi) | m2-aanroep-weg (ReceiveSubcommands.cc:2136) | exit 1, StaticTests.cc:13516 `500u != from_a.meseta` | exit 0 |
| 3 | backup bal M2b (stapel 3 eigen + 5 onbekend, 5 afsplitsen) | m2-stapel-alles-eigen (:549 `return units`) | exit 1, StaticTests.cc:13536 `3u != kb_units_of(...)` | exit 0 |
| 4 | backup bal M2c (1100 Meseta waarvan 600 onbekend, 800 laten vallen) | m2-meseta-alles-eigen (:541 `return units`) | exit 1, StaticTests.cc:13553 `500u != from_a.meseta` | exit 0 |
| 5 | backup bal M3 (inventory_restore_command, check, harde stop en Exit) | m3-regel-weg (:1537) | exit 1, StaticTests.cc:13613 de own_only-regel ontbreekt in de uitvoer | exit 0 |

Eindstand zonder mutant: `run-static-tests` exit 0 ("All static tests passed"), `git status --short src` leeg.
Geen productiefout gevonden: alle vijf tests waren groen op de huidige code, dus geen fix (B-REGIE-22-7 niet nodig).

Wat dit dekt: dat de statische tests deze vijf mutanten vangen. Wat het niet dekt: hoe de back-up zich op de Wii gedraagt; dat is niet gemeten.
