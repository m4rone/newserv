# Route B bundle 8 (ux-pass), newserv side: report

Branch spike/route-b-8 on m4rone spike/route-b-2 cc4abd4. Contract: pso-roguelike route-b-8
`quests/spire/ROUTEB8-PROTOCOL.md` § 1-4 (not edited). Commits: tests against stubs ff15bc47c315721ea7e4af6b8c15ea23ba4c8484 (red), implementation
e9871e69a2e2b08d52abe8da72d855c970c6d331 (green). Version tag in the code: v62 (the highest before was v61), reference B-REGIE-16-13.

**What this covers:** the newserv side of bundle 8: the draw.json keys, the overview line and the server text per code.
**What it does not cover:** the quest side (what the quest sends and when), bundle 9, and client behaviour (only the
Wii decides whether a text fits on screen). Nothing ran on the pc, in the lab or on a Wii; nothing was restarted.

## Per contract rule

| Contract | Built | Where |
|---|---|---|
| § 1 `name` per entry | `SpireDrawEntry::name`; read with a default: missing key, not a string, empty, longer than 22, non-printable ASCII or a `$` is "" (no name known) | src/SpireDraw.hh:25-27, src/SpireDraw.cc:22-50, 76 |
| § 1 `tag_names` | `SpireDrawTable::tag_names`, one slot per list item, bad items "" (max 10) | src/SpireDraw.hh:59, src/SpireDraw.cc:129-133 |
| § 1 `exams` | `std::map<uint8_t, SpireDrawExam> exams` (boss + modifiers); a key that is not a room number 1-255 or a value that is no dict is skipped | src/SpireDraw.hh:31-35, 60, src/SpireDraw.cc:134-159 |
| § 1 `exam_extras` | `SpireDrawTable::exam_extras` (max 24) | src/SpireDraw.hh:61, src/SpireDraw.cc:160-164 |
| § 1 an old draw.json still loads | the new keys never throw; the existing checks are unchanged | test "loader: a draw.json without the keys loads ..." |
| § 2 overview line | `spire_draw_overview_line`: page order slot 3, 4, 5, then 2 (R14); " (cursed)" wins over " (rare)"; over 90 the suffixes go; still over "Rewards: <count> ready. Walk to choose."; "" if a drawn entry has no name | src/SpireDraw.hh:89-96, src/SpireDraw.cc (end of file) |
| § 2 sent to that client at the 'Q' answer, kept until the next 'Q' or a floor change | `spire_overview_entries` (per Client, weak_ptr, lobby id and floor of the 'Q'); erased at every 'Q', at a floor change (`spire_replay_after_floor_load`, runs on 6x1F and 6x21), at the quest start; read through `spire_overview_kept_line` (also checks lobby and floor) | src/SpireFlagBridge.cc:1360-1395, 1401, 1452-1461, 1905, 5955; src/SpireFlagBridge.hh:785-791 |
| § 3 'C' 2 | the kept overview line, else "Take a step: your reward list opens." | src/SpireFlagBridge.cc:1756, context built at 2772-2790 |
| § 3 'H' | "Take a step: the menu opens." | src/SpireFlagBridge.cc:1750 |
| § 3 'P' b = 1 / b = 0 | "Your partner is ready. Walk to choose your reward." / unchanged | src/SpireFlagBridge.cc:1740 |
| § 3 'C' 6 | `spire_text_way_open`; bits 16-23 = 0 or an unknown type: today's text | src/SpireFlagBridge.cc:1642-1660 |
| § 3 'A' types 0-3 | `spire_text_approach`; types 0/1 with extra 0 and types > 3: today's text | src/SpireFlagBridge.cc:1663-1692 |
| § 3 'A' rest text | "Room k - Rest: no enemies, full HP, then the shop." from spire.json `texts.sign_rest` ("No enemies: full HP, then the shop.") as a fixed string | src/SpireFlagBridge.cc:1677 |
| § 3 'C' 10 | `spire_text_exam_reward`; rare name from the writer's archetype (character class, `spire_draw_archetype_for_class`), extra from `exam_extras`, meseta 500 + 100 n, full note | src/SpireFlagBridge.cc:1695-1725, 1801-1815 |
| § 3 'C' 11 | "Take a step: your run starts." (writer only, as every 'C') | src/SpireFlagBridge.cc:1770 |
| § 3 r251 boss clear (SpireBossClearQuests) | `SPIRE_BRIDGE_BOSS_CLEAR_TEXT` "Exam passed!" through `spire_bridge_clear_text(true)`; the normal room clear and 'ROOM' keep "Room clear! Take a step for your reward." | src/SpireFlagBridge.hh:83-89, src/SpireFlagBridge.cc:1817, 5611-5617 |
| § 3 kit ticker | "Use your HP Materials now (Item Pack)." only when hp_mat > 0 (atp_grind alone: no ticker) | src/SpireMag.hh:264-267, src/SpireMag.cc:474-477 |
| § 4 header comment and text function | kind comment updated, new v62 block per code; every text one line, at most 90 | src/SpireFlagBridge.hh:90-118; src/SpireFlagBridge.cc:1621-1795 |

The 90-character rule is a property test: 147456 texts ('A' types 0-3 and 'C' 6 for all 256 values of bits 16-23 at six
k values, 'C' 10 for all bits with six low bytes, four tables including one with over-long names, four archetypes):
none empty, none over 90, none with a line break; longest 90.

## Tests (src/StaticTests.cc:5193-5506, section "Spire bundle 8")

12 cases in one `b8_case` wrapper that logs every failing case and fails the section at its end, so one run shows all.

**Red** (commit ff15bc47c315721ea7e4af6b8c15ea23ba4c8484: tests plus the new API stubbed to today's behaviour), `./build/newserv run-static-tests`, exit 134:

```
B8 (loader: the new keys are read) FAILED: ...StaticTests.cc:5254: std::string("Saber") != named.archetypes.at(0).entries.at(0).name
B8 (loader: a draw.json without the keys loads, nothing named, the texts of before) FAILED: ...:5294: std::string("Room 2.") != spire_bridge_message_text(0x41010002, ctx)
B8 (loader: a bad value is no name, never a failed table) FAILED: ...:5308: std::string("Twenty-two chars name!") != t.archetypes.at(0).entries.at(3).name
B8 (overview line: names, (rare), (cursed), page order, fewer slots, the limits) FAILED: ...:5321: std::string("Rewards: Saber, Fire Shield (cursed), Lavis Cannon (rare). Walk to choose.") != spire_draw_overview_line(...)
B8 ('C' 2 shows the kept overview line until the next 'Q' or a floor change) FAILED: ...:5357: std::string("Take a step: your reward list opens.") != spire_bridge_message_text(0x43000202)
B8 ('H', 'P', 'C' 11 and the boss clear) FAILED: ...:5372: std::string("Take a step: the menu opens.") != spire_bridge_message_text(0x48000000)
B8 ('C' 6 names both pipes, an exam pipe its boss; bits 16-23 = 0 as before) FAILED: ...:5384: std::string("The way is open: Room 5 ELITE or Room 5 REST. Walk close to a warp to read it.") != ...
B8 ('A' names the tag, the rest, the exam boss and modifier; extra 0 of types 0 and 1 as before) FAILED: ...:5400: std::string("Room 2. Next reward: Fire.") != ...
B8 ('C' 10 the exam reward, to the writer's archetype) FAILED: ...:5419: std::string("Exam reward: 600 meseta, Lavis Cannon, Photon Drop x2.") != ...
B8 (every text one line of at most 90 characters) FAILED: ...:5453: std::string("Room 4 - Exam: A boss with a long name.") != ...
B8 (kit ticker: HP Materials only, grinders are a grind on the kit weapon) FAILED: ...:5486: std::string("Use your HP Materials now (Item Pack).") != std::string(SPIRE_KIT_USE_TEXT)
B8 (the draw archetype of a class) FAILED: ...:5496: 0 != spire_draw_archetype_for_class(0)
what():  failure at .../StaticTests.cc:5505: 0u != b8_failed
```

Every section before it passed in the red run (65 sections, then the bundle 8 section failed).

**Green** (commit e9871e69a2e2b08d52abe8da72d855c970c6d331), same command, exit 0: all 12 cases logged without FAILED,
`B8: 147456 texts checked, longest 90 characters`, `-- All static tests passed` (66 sections).

One expectation changed between red and green: the 'C' 10 full case first expected
"Exam reward: 600 meseta, Lavis Cannon, Photon Drop x2. Inventory full: make room, it waits." (92 characters, over the
limit); it now expects the shortened form, plus one more full case without a rare. That case was red in the red run
already (line 5419, before it).

Older assertions that the contract changes were moved out of their sections into the bundle 8 section, with a note
where they stood: 'H' (StaticTests.cc:2026), 'C' 2 (2030), 'A' rest (2041), the kit "atp_grind only" case (4780). The
old texts of 'C' 6 and 'A' 0/1 at bits 0 stay asserted in the v35 section and again in the bundle 8 section.

## Deviations and contract gaps (safest reading built; contract not edited)

1. **'C' 6 two normal pipes encode to bits 16-23 = 0x00**, the same as an older quest, so they show today's text "The
   way is open. Walk to a warp." and never "Room k normal or Room k normal". Fixing it needs a contract change (e.g. a
   marker bit, or typeB stored + 1); newserv cannot tell the two apart.
2. **'A' rest (type 2) has extra 0 by contract**, which collides with "bits 16-23 = 0 gives today's text". Built: the
   rest text is always the new one. Safe for an older quest: its rest room is the same (main's spire.json
   `texts.sign_rest` = "No enemies: full HP, then the shop.", identical to route-b-8's).
3. **'C' 10 can pass 90 characters with ordinary names** (Lavis Cannon + Photon Drop x2 + the full note = 92). The
   contract has no shortening order; built: drop the extra first, then the rare name (the full note is about the rare,
   so the rare stays as long as it fits).
4. **Not in the contract, built as follows:** one pipe that is no exam (typeB 0xF, typeA 0-2): "The way is open. Walk
   to the warp: Room k <normal/ELITE/REST>."; an exam pipe in a two-pipe code: the word "EXAM"; any type above 3: today's
   text. 'C' 6 unknown boss: "Exam Room k" (the literal substitution of "<boss>"); 'A' exam with unknown boss: "Room k -
   Exam."; a boss or modifier line that does not fit: the modifier goes, then the boss.
5. **'A' types 0/1 with extra 0** (an older quest, or a new quest pipe without a tag) show today's text, not "Room k."
   / "Room k - Elite: harder foes, rare reward.". With extra > 0 but no tag name known, the new form without "Next
   reward".
6. **Four rewards (R14):** the overview lists all four in page order (slot 3, 4, 5, then 2); the count fallback says
   "Rewards: 4 ready." (the contract writes "3 ready" for the three-slot case).
7. **'C' 10 archetype** comes from the writer's character class (HU 0, RA 1, FO 2, the same split as the quest's
   r156), so it works without an earlier 'Q' in the game. An unknown class: the rare name is left out.
8. **'P' b = 1** loses the $C4 colour of the old text (the contract gives plain words).
9. **Loader limits:** boss and modifier strings are capped at 90 (the contract names no limit); any value outside its
   limit is "no name known", never a load error, so the draw itself never fails on a name.
10. **Kit:** `spire_kit_use_text_due` keeps its atp_grind argument (the callers log it) but no longer uses it.

## Decisions (would be B-E entries; this round's writeSet is only this worktree, so they are logged here)

- Names that break a rule are "no name known" instead of a table error: the draw (r181) must keep working with any
  draw.json (§ 1 "a missing key means no name known"; the draw is the critical path). Class 2.
- The overview store is keyed per Client with a weak_ptr, lobby id and floor, like `spire_run_rooms`; the floor
  check is done both at the floor load and at the read, so a missed floor event cannot show a stale line. Class 2.
- Static tests collect failures per case (`b8_case`) so one run shows every red case. Class 2.

## Observations (seen, not built)

- The 'C' 6 encoding gap (deviation 1) is the one with a player-visible effect: the most common case (two normal
  pipes) keeps the old text. Cost: one contract line plus a quest-side and a newserv-side change of a few lines each.
- The draw table is read from disk for each 'A', 'C' 6 and 'C' 10 text (and each 'Q', as before). These are a few
  per room; a per-quest cache would save the reads. Not needed now.

## Notes for the next step

- The quest must send the new bits as § 3 says; check the actual quest texts against these strings in one run on the
  Wii (clipping is a client question).
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j16`, then
  `./build/newserv run-static-tests`.
