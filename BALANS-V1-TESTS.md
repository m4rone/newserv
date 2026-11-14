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

## Ronde 2 (gaten van systeemcritic 13:53)

| Gat | Test | Mutant | Rood (met mutant) | Groen (zonder) |
|---|---|---|---|---|
| a | backup bal 7805 (64 baasdrops door spire_drops_filter, Saber uitgerust in de inventaris) | 7805-uitgerust-niet-gelezen (SpireDrops.cc:504 `0 *`) | exit 1, StaticTests.cc:13665 `!(strength > carried)` | exit 0 |
| b | level floor, run again ('I') met savestates van beide spelers (eind van "Spire level floor ... two clients") | 19c4-leider-hervat (SpireFlagBridge.cc:4921) | exit 1, StaticTests.cc:14174 `12u != ...disp.stats.level` | exit 0 |

Bij (b) gaf de eerste versie (na het wissen van spire_room_quests) ook met de mutant exit 0: zonder kamerquest leest spire_resume_level_for_client geen savestate. De controle staat nu vóór het wissen; de kamervloer van kamer 5 (Lv11) ligt onder de verwachte Lv13.
Ook hier geen productiefout: beide tests waren groen zonder mutant.

Wat dit dekt: dat de statische tests deze vijf mutanten vangen. Wat het niet dekt: hoe de back-up, het baaswapen en de levelvloer zich op de Wii gedragen; dat is niet gemeten.
