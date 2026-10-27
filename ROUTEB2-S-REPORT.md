# Route B bundle 2, phase S: ticker lines (kit, boss door) and a test for the 61 mark

Branch `spike/route-b-2`, base f27d4f8. Code commit: **ab3c819** (`feat: boss door and kit ticker lines once per run,
testable 61 mark`). Measured 2026-10-27 in this worktree.

**Covers:** the server text after the 'M' START and at the boss door, and making the 0x61 mark testable.
**Does not cover:** client behaviour on the Wii or Dolphin (whether the ticker line really shows), the quest side, and
balance.

## Item 1: materials text after 'M' START

**What I found:** the text was already there. It came in with 4891acc (F6), which is part of base f27d4f8:
`SPIRE_KIT_USE_TEXT = "Use your materials and grinders now (Item Pack)"`, sent 3 s after the START answer
(`SPIRE_KIT_USE_TEXT_DELAY`). The orchestrator's note "no 'Use your materials' in newserv src"
(routeb2-voortgang.md row 5a) is wrong for this branch.

**What I changed:** I fixed three defects on the path of the criterion.
- **A second START from the same client sent the line again.** The leader can have a second t_ui ("a second t_ui
  (route 2 restart) waited here too", make_spire.py:1857-1860), and both run meta_start. The line now goes out once
  per client and run: `SpireClientRun::kit_text_sent`, SpireFlagBridge.cc:719. A new quest load is a new run
  (`spire_client_runs.erase`, cc:1684).
- **A resumed run got the line, but it gets no kit.** On a resume, kit_or_resume skips the kit (make_spire.py:1474-1487).
  The line is now decided after the 3 s delay, in `spire_kit_use_text_later`, SpireFlagBridge.cc:2129-2147. By then the
  client's 'J' (rs_begin, called right after meta_start) has set `answered_resume`.
- **A refused START could still send the line.** The line is now sent only when the answer is OK
  (SpireFlagBridge.cc:2262). The quest gives the meta kit only on an OK answer (make_spire.py:1397).

The pure rule is `spire_kit_use_text_due(start, hp_mat, atp_grind, resumed, already_sent)`, SpireMag.hh:196 /
SpireMag.cc:351.
- Without hp_mat or atp_grind levels (old meta, old catalogue): no text.
- Text: unchanged.
- Server event: the 'M' START answer from this client. The line goes only to that client, so two players each get
  their own line.

## Item 2: boss door text from k8 on

**Server event:** the client's **'V'** (savestate write) of the boss room.
- The quest sends it right before `boss_burst`: leader at make_spire.py:2196-2201, follower in mirror phase 4 at
  :3626-3647.
- Each client sends its own 'V', and the line goes only to that client.

**Existing state:** this hook already existed. It sent `"Your mag shields you"` on 'V' when k >= 8 and k is in
`spire-mag.json` BossRooms.

**Changed** (SpireFlagBridge.cc:1450-1466):
- **New text:** `SPIRE_BOSS_BURST_TEXT = "Boss ahead: you are buffed"` (SpireMag.hh:203).
- **Once per client, room and run:** `SpireClientRun::boss_burst_texts` (cc:720). A second 'V' of the same room logs
  "already sent".
- **No spire-mag.json:** no text and no warning. An unreadable file still gives a warning, as before.
- **Constants renamed** to match the meaning: `SPIRE_RUN_MAG_SHIELD_TEXT/FROM_K` became `SPIRE_BOSS_BURST_TEXT/FROM_K`.

The pure rule is `spire_boss_burst_text_due(k, boss_rooms, sent)`, SpireMag.cc:355. It requires k >= 8 and k in
BossRooms (shipped: 4, 8, 11, 15, 18, 21, 24, 27, so the Dragon k4 is excluded). It marks k as sent.

**Limits** (the server cannot see these):
- The line follows the 'V', not the cast (routeb-f7f9-critic.md:66).
- A quest with SAVESTATE but without the F7 burst would also get the line.
- A build without SAVESTATE sends no 'V', so it gets no line.

## Item 3: the 0x61 mark

- `spire_run_mag_command_refreshes(uint16_t)`, SpireMag.hh:211 / SpireMag.cc:362, returns `command == 0x61`.
- ReceiveCommands.cc:3638 now calls it: `if (spire_run_mag_command_refreshes(msg.command)) { spire_run_mag_note_in_game_61(c); }`.
- Test: StaticTests.cc:4541-4545. It checks that 0x61 is true and that 0x98 and 0x00 are false.

## Red and green

The build is `cmake --build build -j8` (Release, fresh `build/` in this worktree), then `./build/newserv run-static-tests`.
- **Green:** build exit 0 with no warning line, tests exit 0. The last line is `-- All static tests passed`. There are
  66 `--` sections, and the three new cases are in the log ("F6 (bundle 2 S: ...)").
- **Red:** each mutation was made in src/SpireMag.cc only, built, tested, and the file restored from a copy. Nothing was
  committed. The scripts are in scratchpad `S/mut/run.sh` and `S/mut/run2.sh`.

| Mutation | Build | Tests | Failing line |
|---|---|---|---|
| `command == 0x61` -> `0x98` | 0 | 134 | `StaticTests.cc:4542: !(spire_run_mag_command_refreshes(0x61))` |
| kit: `!resumed` -> `(resumed \|\| !resumed)` | 0 | 134 | `StaticTests.cc:4522: !(!spire_kit_use_text_due(true, 1, 1, true, false))` |
| kit: levels check `\|\| true` | 0 | 134 | `StaticTests.cc:4520: !(!spire_kit_use_text_due(true, 0, 0, false, false))` |
| kit: `!already_sent` -> `(already_sent \|\| !already_sent)` | 0 | 134 | `StaticTests.cc:4523: !(!spire_kit_use_text_due(true, 1, 1, false, true))` |
| burst: dedup removed (`return true`) | 0 | 134 | `StaticTests.cc:4531: !(!spire_boss_burst_text_due(8, table.boss_rooms, sent))` |
| burst: `room_k < SPIRE_BOSS_BURST_FROM_K` -> `room_k < 1` | 0 | 134 | `StaticTests.cc:4528: !(!spire_boss_burst_text_due(4, table.boss_rooms, sent))` |

The first kit-resume mutation (dropping `!resumed` entirely) did not compile (`-Werror=unused-parameter`). That run
tested a stale binary, so I discarded it; the variant in the table keeps the parameter in use.

After restoring the file, run2.sh printed `final build exit 0` and `final tests exit 0: -- All static tests passed`.

**Not killable by a static test:** the wiring.
- ReceiveCommands calling the function.
- The 'V' handler and the coroutine passing the right SpireClientRun fields.
- The ticker send itself.

## Decisions (route: self, class 2, covered by the dispatch of phase S)

1. **Burst text:** "Boss ahead: you are buffed".
   - It is true for both Shifta only and Shifta + Deband. The server cannot tell which: it has the modifier index
     (r178), but not the modifier names, which live in the quest's spire.json.
   - It names no mag, and it is short and English, like the other Spire lines.
   - It deliberately departs from plan-route-b-2026-10-26.md:90 ("Your mag shields you"), as the dispatch asks.
2. **Server event and boss rooms:** 'V' is the only signal the server has that falls at the boss door (right before the
   cast). The boss rooms come from the existing BossRooms list in spire-mag.json rather than a new config. That list is
   server config; it does not depend on whether a run holds a mag.
3. **Kit line:** no line on a resume or a refused START, and once per run. This fixes defects on the path of the
   criterion (the line belongs with the kit the quest gives).
4. **Deduplication per client in SpireClientRun:** each player gets the line for its own signal. A partner's signal
   never sends a line to the other client.
5. **One commit for all three items:** they share SpireMag.hh/.cc and StaticTests.cc.

## Observations (not built)

- **The burst line can be pushed aside.** At the same boss start, the quest's room_start hook may send 'Z' NO_MAG.
  That happens when a mag was issued but is not equipped. The equip reminder then goes out right after the burst line
  and pushes it aside on the ticker.
  - Where: SpireFlagBridge.cc:2288-2292 versus 1456.
  - Cost: a delayed send (`spire_send_ticker_later`) for one of the two.
- **The resume check depends on timing.** The kit-line resume check assumes the 'J' arrives within 3 s of the 'M'
  START. The quest sends it right after; a slower 'J' would let the line through on a resume.
  - Where: SpireFlagBridge.cc:2129.
- **The git-flow hook warns on commit.** The hook reported that this newserv worktree records no
  Integratiebranch/Uitrol (`.claude/omgeving.json` or `CLAUDE.md § Git flow`). That is outside the writeSet; the
  commit went through.

## Expansion requests

None.
