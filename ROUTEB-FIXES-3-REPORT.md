# Route B fix round 3: retake with a real Mag LV5, run mag mutation tests

Branch spike/route-b, base a143cbf. Commits: fc16119 (item 1), ce12101 (item 2), this report on top.
Review: pso-roguelike new-features/roguelike/output/routeb-f6-critic.md (C1, C3-6). Decision: B-REGIE-14-9.

**Coverage claim.** Covers: the retake decision with a real unfed Mag LV5, mutation-killing tests for the listed
server mutations, the retake callers under SpireBaseQuest. Does NOT cover: the quest side, client behaviour, balance.

## Premise notes (read first)

1. **C1 is real but does not happen on the pc config.** With `SpireBaseQuest` 925 set, neither caller of
   `spire_guard_retake_ok` runs (item 3 below). The misclassification only bites a server without `SpireBaseQuest`
   (lab variants). The fix was made anyway, as the brief asked.
2. The review's `file:line` refer to its frozen snapshot (15794ad). On a143cbf the term was at SpireFlagBridge.cc:2958,
   the callers at :3516 ('O' 98) and :4381 ('I'), the 'V' gate at :1431, the handler at :2359-2399.
3. Removing the term opens one case it used to close: a run copy whose 61 shows the snapshot's exact level and EXP,
   does not match the print, and holds a run mag that is not a Sega mag. B-REGIE-14-9 already accepted this ("exact
   level and EXP separate a run copy"). The one realistic form: a real character whose EXP is exactly the table EXP of
   its level (a fresh Lv1, EXP 0) against a bonus-0 run copy at the same level. Noted below, not built.

## Item 1 (C1): DONE, fc16119

- `spire_guard_retake_ok` no longer has the `holds_run_mag` term. Now it checks: not worse, exact level and EXP, a print
  is stored, the data does not match the print.
- The `run_mag` parameter is removed from `spire_guard_retake_ok`, `spire_guard_run_again_retake` and
  `spire_guard_base_exit_follow`, and from both callers. `spire_guard_holds_run_mag` and `spire_guard_ledger_run_mag`
  had no other use and are removed too. The ledger itself stays (savestate items, 'Z' answers, 'V').
- New test case F1 g: Nyra Lv30 holds Pitri plus a spare `02 00 05 00 F4 01 00 00 00 00 00 00`. The ledger mag has the
  same bytes. She is back at the exact level and EXP after a bank withdrawal. Both the 'I' re-take and the 98-after-'O'
  follow take her. The run copy (Lv1) is still refused.
- Test F1 b is rewritten. Its exit fixture (a "run copy" at the real character's exact level and EXP, refused only by
  the mag term) is the behaviour C1 removes. It now stores that fixture's own print and is refused by the print.
- Red on a143cbf, with only the new test added:
  `terminate ... failure at src/StaticTests.cc:741: !(spire_guard_retake_ok(after_base.first, after_base.second, snap, ledger_mag))`, exit 134.
- Green after the fix: `-- All static tests passed`, exit 0.

## Item 2 (C3-6, server part): DONE, ce12101

The handler code and the 'V' gate needed a Client and a Lobby, so no static test could reach them. They now call pure
functions in SpireMag.cc, and the logic and its order are unchanged:
- `spire_run_mag_handle`: the start-step guard, then following the mirror, then old_type, then the decision, then the
  r41 answer. The server-boss and start-bonus lookups have no side effects and now run before it.
- `spire_run_mag_savestate_follow`: the 'V' 61-gate.

The handler only sends `handled.answer`; a refusal sends the default `0x5A00FF00`. The existing suite passed on the
refactor before any new case was added.

Each mutation was run in a throwaway build: mutate src/SpireMag.cc, build, run the tests, restore the file from a copy.
Nothing was committed. The scripts are in the session scratchpad at mut/run.sh and mut/run2.sh.

| # | Mutation | Killed by | Failure under mutation |
|---|---|---|---|
| 1a | `ret.old_type = overlay_mags[0][1]` to `SPIRE_RUN_MAG_NO_OLD` | F6 "handler: old_type ..." | StaticTests.cc:4417 `kit.decision.bytes[1] != dragon.old_type` |
| 1b | answer built with `SPIRE_RUN_MAG_NO_OLD` instead of `ret.old_type` | same | :4419 `0x5A000001u != dragon.answer` |
| 2 | `boss_rooms.insert(server_boss_rooms...)` deleted (as `static_cast<void>(server_boss_rooms)`) | F6 "a lost EXAM is counted ..." | :4441 `!(a5.result)` |
| 3 | 'V' gate `!refreshed_by_61` dropped (as `(refreshed_by_61 \|\| !refreshed_by_61)`) | F6 "'V': the ledger never follows ..." | :4454 `!(!spire_run_mag_savestate_follow(&led, true, {pitri}))` |
| 4 | ReceiveCommands.cc:3638 `0x61` to `0x98` | **not reachable** | see below |
| 5 | start-step guard to `if (true)` | F6 "the start step is set by the first ASK only" | :4472 `2u != e8.decision.expected_step` |
| 6 | confirm `size() == 1` to `>= 1` | F6 "the offered mag is confirmed ... only as the one type-02 item" | :4494 `!(!spire_run_mag_follow_mirror(led, {e4.bytes, *led.last_mirrored}))` |

Every mutated build exited 0 and every mutated test run exited 134. The first attempt at mutations 2 and 3 did not
compile (`-Werror=unused-parameter`), so that run tested a stale binary and was discarded. The variants above keep the
parameter used. After the files were restored: `final build exit 0`, `final tests exit 0: -- All static tests passed`.

**Mutation 4 is not reachable from a static test.** The mark sits in `on_61_98` in ReceiveCommands.cc, which needs a
connected Client with a lobby and an overlay. No static test builds a Client, and ReceiveCommands.cc is outside this
writeSet. One way to cover it: a pure `spire_run_mag_command_refreshes(uint16_t command)` that ReceiveCommands calls,
plus a test. That needs a writeSet expansion; it was not requested or built.

Remaining wiring after the refactor (not killable statically): the handler could send something other than
`handled.answer`, or pass a wrong `refreshed` value into the 'V' function. These are single call lines.

## Item 3: callers of spire_guard_retake_ok on the merged tree

| Caller | Reached from | Runs with SpireBaseQuest 925? |
|---|---|---|
| `spire_guard_run_again_retake` (SpireFlagBridge.cc:3006) | `spire_run_again` ('I'), the call at :4353 | **No.** `spire_run_again_refusal` returns `NOT_RETURNED` unless `spire_base_returned_has` (:1065). That flag is set only by `spire_base_return_prove` (:3859), which is spawned only when 'O' passes with `refusal == R::NONE` (:3963). With a base quest set, 'O' turns NONE into `BASE_GAME` (:3913-3918) before that point. |
| `spire_guard_base_exit_follow` (SpireFlagBridge.cc:3020) | `spire_guard_after_98`, the call at :3488 | **No.** It runs only if `spire_guard_base_exit_pending` holds the client. That is set at the run end only when `base_returned` (:3422, :3465), which is the same flag as above. |

So on the pc (SpireBaseQuest 925) `spire_guard_retake_ok` never runs, and the base-game flow handles the real
character (v57-v61). The callers run only on a server without `SpireBaseQuest`. This was read from the code; it was not
measured live.

## Tests

- Static suite: `./build/newserv run-static-tests` exits 0 with "All static tests passed". There are 66 `--` sections
  both before and after. The new F1 g case and five new F6 cases are logged in the run. No existing case was deleted;
  F1 b was rewritten (see item 1).
- Logs are in the session scratchpad: t-red-c1.log (red), t-green-c1.log, mut/*.test, mut/final2.test.

## Decisions (for the regie's decisions.md; this track does not write in pso-roguelike)

- B-E-RB3-1: remove the `run_mag` parameter and the two unused helpers instead of leaving a dead parameter. Class 2,
  route zelf. Covered by the brief item 1 ("remove the holds_run_mag term") and output-quality.md (no dead code).
- B-E-RB3-2: extract the 'Z' ledger step and the 'V' gate into pure SpireMag functions so the C3-6 mutations are
  reachable. Class 2, route zelf, within the writeSet. Covered by brief item 2 and the existing "pure parts for the
  static tests" pattern in SpireMag.hh.
- B-E-RB3-3: rewrite F1 b's exit fixture (same level and EXP, refused only by the mag term). That assertion is the
  behaviour C1 removes, per B-REGIE-14-9 and the brief item 1.

## Observations (noted, not built)

- Mutation 4 needs a pure predicate in ReceiveCommands.cc (outside the writeSet) to be testable. Small change, needs
  a writeSet expansion.
- The residual from premise note 3: if the regie wants it closed, keep a mag term only for ledger mags that are not
  Sega-obtainable (anything other than kind 0, level 5, no PB). That is the review's alternative fix. It has no effect
  under SpireBaseQuest.
