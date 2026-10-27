# Hotfix 1: the pack-full mag notice (route B bundle 1)

Branch spike/route-b-hotfix1, base f27d4f8. Commit 701df2d (code + test), then this report.

Covers: the server notice at a 'Z' RESULT 0 (pack full), its dedup per run and step, and whether the text matches what
the bundle 1 quest (95ecec1) and this server do next. Does NOT cover: client behaviour (whether the ticker shows on
the Wii), the quest, balance. Nothing was run on a client or in the lab.

## Premise that needed correcting

The brief says the player hears nothing. On f27d4f8 the server already sent a ticker on every RESULT 0
(`src/SpireFlagBridge.cc:2288` at f27d4f8: `spire_send_ticker(c, SPIRE_RUN_MAG_PACK_FULL_TEXT)`, text
`"Pack full: your mag grows next room"`), but its log line did not say so, so the lab log
(`lab/ep2-line/newserv.log:183897`) looks silent. Whether that ticker reached the screen is client behaviour and not
measured here. What changes in this hotfix:

1. The text was wrong in two ways: the mag does not grow on its own (only if a slot is free at the retry, see below),
   and after a boss clear the retry is the next room's clear, not "next room".
2. It fired on every RESULT 0, so a pack that stays full repeats the same line at every clear. Now: once per
   given-back step per run.
3. The log line now ends in `; pack-full notice sent` or `; pack-full notice for this step already sent in this run`.

## What changed

- `src/SpireMag.hh:175-183`, `src/SpireMag.cc:308-318`: pure `spire_run_mag_pack_full_notice(told, run_id, known,
  created, offered_step)` with its record `SpireRunMagPackFullNotices {run_id, steps}`. False unless a known offer got
  RESULT 0; a new run_id resets the record; true only the first time a step is seen.
- `src/SpireMag.hh:196-197`: the text, as a constant:
  `"Pack full: mag did not grow. Free slots; next clear retries"` (59 characters; the existing Spire run-mag tickers
  measure 52 for the equip reminder and 54 for the evolve text with "Ashvinau"; brief limit 61).
- `src/SpireFlagBridge.cc:791-793`: `spire_run_mag_pack_full_told`, an in-memory map keyed like the ledger store
  (`spire_run_mag_key`: serial + character id), apart from the ledger.
- `src/SpireFlagBridge.cc:2289-2305`: the RESULT 0 branch asks the pure function and sends the ticker only on true.
  The branch runs after `spire_run_mag_result` settled the ledger (`:2285`); it reads `run->run_id`, `known`,
  `created` and `offered_step` (captured before the result call) and writes nothing but its own map. The ledger,
  `issued_step`, the r41 answer (RESULT has none: "no answer: the quest waits for none") and the item handling are
  unchanged; the static test checks the ledger fields before and after the call.
- `src/StaticTests.cc`: case "pack-full notice: once per given-back step per run (hotfix 1)" in the F6 section.

## Evidence for every promise in the text

| Words | What the code does | Where |
|---|---|---|
| "Pack full" | the quest reports RESULT 0 only when its own `item_create2` of the new mag returned FFFFFFFF | make_spire.py@95ecec1:1732-1733, 1741-1742 |
| "mag did not grow" | on that path the unequip and the delete of the old mag are skipped: the old mag stays as it was | make_spire.py@95ecec1:1733-1741 (`jmpi_eq MAG_POLL, ALL32, full` jumps past `unequip_item` and `item_delete`) |
| "mag did not grow" (server) | RESULT 0 gives the step back: `issued_step = step_before_offer` | SpireMag.cc:293-306 (line 300) |
| "next clear retries" (room) | the full path sets MAG_PENDING r135 = 1; at a non-boss room clear `mag_entry_clear` jumps to `mag_ask` while r135 != 0 | make_spire.py@95ecec1:1743, 1781-1786; called last in the room_clear hook, rewards.py@95ecec1:1288-1289 |
| "next clear retries" (boss) | a boss room's clear skips `mag_entry_clear` (1783) but `exam_reward` always calls `mag_entry_exam`, which asks ('Z' EXAM) | make_spire.py@95ecec1:1677, 1783; rewards.py@95ecec1:1158-1159 |
| "retries" gets the same step | after a RESULT 0 at the Dragon EXAM (k4) boss_rooms holds 4, so the next ASK expects start + 1 = the given-back step, and `issued_step < expected` offers it again; at a later new boss EXAM the expected step is one higher | SpireMag.cc:128, 254-266 |
| "Free slots" | the retry's create succeeds only with a free slot (same FFFFFFFF test); that clear's own rewards are created first (room_clear: R4/R5/R9/R10/Sol before `mag['clear']`; exam: the rare before `mag['exam']`), hence "slots", not "a slot" | rewards.py@95ecec1:1155-1159, 1270-1289 |

The text says "retries", not "grows": growth at the retry depends on the pack then.

Limits of the promise (the text cannot know the room, the RESULT word carries no k, make_spire.py@95ecec1:1742):

- After the run ends no request is sent (`jmpi_eq R.phase, 9, free`, make_spire.py@95ecec1:1683, 1717). A RESULT 0 at
  the last boss of the run (De Rol Le in bundle 1) has no next clear: the retry never comes in that run.
- A request that finds another one open less than 30 s old returns and leaves r135 set (make_spire.py@95ecec1:1666-1672);
  the retry then moves to the clear after.

## Dedup in the lab case

`lab/ep2-line/newserv.log:183886-183897`: EXAM k4 offered step 1, RESULT 0, step back to 0. With this hotfix: notice
sent (step 1, run 64D5C0EF). If the pack is still full at the k5 clear, the ASK offers step 1 again and its RESULT 0
is silent. A RESULT 0 for step 2 (the De Rol Le EXAM) tells again; a new run tells again.

## Tests

`./build/newserv run-static-tests` on the committed tree (`git diff --quiet HEAD` exit 0, test run 05:32:04 after the
commit at 05:31:58): exit 0, last line `-- All static tests passed`. The new case logs
`F6 (pack-full notice: once per given-back step per run (hotfix 1))`.

Binary of 701df2d: `md5sum build/newserv` = `6b9c90e5e0ac63c23e146a03e0d72494`.

Cases: first RESULT 0 for step 1 -> notice; the same again -> none; step 2 -> notice; new run -> notice; RESULT 1 ->
none (and records nothing); no offer settled -> none; ledger fields equal before and after the call; the text and its
length <= 61.

The send itself (`spire_send_ticker` in the handler) is not covered by a static test: no static test builds a Client.

## Mutations (throwaway copy: `git archive HEAD` into the scratchpad, own build dir; the worktree was not touched)

| Mutation in src/SpireMag.cc | Build | Tests | Failing line |
|---|---|---|---|
| dedup removed (`told.steps.emplace(offered_step); return true;`) | exit 0 | exit 134 | `StaticTests.cc:4515: !(!spire_run_mag_pack_full_notice(told, 0x64D5C0EF, true, false, 1))` |
| condition inverted (`if (!known \|\| !created)`) | exit 0 | exit 134 | `StaticTests.cc:4514: !(spire_run_mag_pack_full_notice(told, 0x64D5C0EF, true, false, 1))` |
| restored from the copy | exit 0 | exit 0 | `-- All static tests passed` (05:34:26) |

## Decisions (taken within the brief, logged here: decisions.md is outside the writeSet)

- Dedup record apart from the ledger (own map, same key) instead of a field on `SpireRunMagLedger`: the ledger is
  written into the savestate file, and the brief forbids the notice to change it. Lost on a server restart: then a
  step can be told once more, never less.
- Text "Pack full: mag did not grow. Free slots; next clear retries": "retries" rather than "grows", because growth
  depends on a free slot after that clear's own rewards; "next clear" rather than "next room clear", because after a
  room clear's RESULT 0 the retry can be a boss clear's EXAM.

## Observations (not built)

- A RESULT 0 at the run's last boss (De Rol Le in bundle 1) can never be retried (phase 9). The text still says
  "next clear retries". Only the quest or a k in the RESULT word could tell; both are outside this hotfix.

## On spike/route-b-2 (newserv worktree route-b-2, base e5f4748)

- Cherry-pick of 701df2d applied without conflicts as 10a0f32: the RESULT 0 branch there (F10 lean, NO_LEDGER,
  reminder texts) had the same three lines; the new branch keeps `offered_shell` and the Shell texts as they were
  (`src/SpireFlagBridge.cc:803`, `:2330-2346` there; `src/SpireMag.cc:388` there).
- 3e9e2b1 (test only): the case also checks `offered_shell` unchanged by the notice, a Shell run (lean 2, HU
  ShellSteps column) where the EXAM k4 RESULT 0 is told, the ASK k5 offers the same step again and its RESULT 0 is
  silent, and the length against the branch's `SPIRE_RUN_MAG_TICKER_MAX` (61) instead of a literal.
- The text holds for bundle 2: the quest on pso-roguelike spike/route-b-2 (9995c73) has the same retry path
  (make_spire.py:1764, 1772-1774 full -> r135 = 1; 1812-1815 mag_clear asks while r135 != 0, skipped at a boss room;
  1706 mag_exam; 1712, 1748 no request after phase 9; rewards.py:1159 exam, 1289 clear). The lean only picks the
  column (SpireMag.cc spire_mag_column); "mag" in the text names no column, so it is true for Fang/Mind and Shell.
- `./build/newserv run-static-tests` there: exit 0, `-- All static tests passed` at 05:36:13 on the tree committed at
  05:36:25 (`git diff --quiet HEAD` exit 0 after the commit). Binary md5 `c4a4998abf8fc96f47725f6c91a6dde5`.
- Not pushed.

## Round 2: the text promises no retry (5bfe779 on hotfix1, a02f6e3 on route-b-2)

The text above ("... next clear retries") is replaced. Final text, both branches:
`"Pack full: your mag did not grow. Keep a slot free"` (50 characters).

Why one neutral text and not two (the server cannot know whether a next clear comes):

- The RESULT word carries no k: `MAG_TAG | MAG_RESULT << 16` (make_spire.py@95ecec1:1742; 1738 for result 1).
- No server data names a quest's last room. The checkpoint's `LastRoom` is the furthest room cleared
  (SpireFlagBridge.cc:5355, 5472-5479), not the run's end. spire-mag.json `BossRooms` is `[4, 8, 11, 15, 18, 21, 24,
  27]`, both episodes in one list, so its highest entry is not the end of an Ep1 quest. `SpireGameRun`
  (SpireFlagBridge.cc:670-676) holds no room list.
- The premise "De Rol Le is the last boss in bundle 1" does not hold for q929: it plays all of spire.json, 15 rooms
  `[1..15]`, bosses `[4, 8, 11, 15]` (spire.json@95ecec1), so its last boss is Dark Falz (k15). Room quests
  (q940-q959) start at their own room and play on (build.sh@95ecec1:409-414, 566: "a room quest from its own room
  on"), so a practice run in a room quest has a next clear except at the last room, the same rule as the full run.
- A run also ends without a last room: server time up, a death, the base return 'O'; after the end no request is sent
  (make_spire.py@95ecec1:1683, 1717).

Every word of the new text is true in all those cases:

| Words | Evidence |
|---|---|
| "Pack full" | RESULT 0 is sent only after `item_create2` returned FFFFFFFF (make_spire.py@95ecec1:1732-1733, 1741-1742) |
| "your mag did not grow" | that path skips the unequip and the delete, the old mag stays (1733-1741); the server gives the step back (SpireMag.cc `spire_run_mag_result`, `issued_step = step_before_offer`) |
| "Keep a slot free" | advice, no promise: if a retry comes (next room clear 1781-1785, or boss exam rewards.py@95ecec1:1158-1159), it creates the mag only with a free slot after that clear's own rewards (rewards.py@95ecec1:1155-1159, 1270-1289) |

The observation about the last boss above is resolved by this round.

Tests on the committed trees (`git diff --quiet HEAD` exit 0 on both, commits at 05:39:39):

- hotfix1 5bfe779: `./build/newserv run-static-tests` exit 0, `05:39:44 -- All static tests passed`; md5
  `4a52c4c9f9ac9d36ca130b5c31e56f42`.
- route-b-2 a02f6e3: exit 0, `05:39:44 -- All static tests passed`; md5 `d4691c3d9eb8b72b24e97b6364c44437`.

Mutation of the condition per branch (throwaway copy via `git archive HEAD`, `if (!known || !created)`):

| Tree | Build | Tests | Failing line | Restored |
|---|---|---|---|---|
| hotfix1 5bfe779 | exit 0 | exit 134 | `StaticTests.cc:4514: !(spire_run_mag_pack_full_notice(told, 0x64D5C0EF, true, false, 1))` | build 0, tests 0 (05:43:23) |
| route-b-2 a02f6e3 | exit 0 | exit 134 | `StaticTests.cc:4811: !(spire_run_mag_pack_full_notice(told, 0x64D5C0EF, true, false, 1))` | build 0, tests 0 (05:43:22) |

With one text there is no second text branch to mutate; the text itself is pinned by `expect_eq` in the case.

## Round 3 on spike/route-b-2 only (critic routeb2-hotfix1-critic.md N1-N5; commit 7352c39)

The hotfix branch from f27d4f8 is left as it is (B-REGIE-15-10: it does not roll out on its own).

- N1: `SPIRE_RUN_MAG_PACK_FULL_TEXT` = `"Pack full: your mag did not grow. Keep slots free"` (49). Slots, because a clear
  creates its own rewards before the mag (the exam rare, rewards.py:1156-1159; up to five room clear rewards,
  rewards.py:1259-1289), so one free slot can be taken before the retry.
- N2: `SPIRE_RUN_MAG_PACK_FULL_FIRST_TEXT` = `"Pack full: no room for your mag. Keep slots free"` (48), sent when nothing was
  issued before this offer (`step_before_offer < 0`: the kit or a practice run's first ASK). "did not grow" only when
  a mag was issued before, as the RESULT 1 branch already tells reminder from evolve (`before < 0`).
- N3: the handler's glue is now the pure `spire_run_mag_pack_full_decide(told, key, run_id, known, created,
  offered_step, step_before_offer)` (SpireMag.hh/.cc): the record per client key, no notice without a key or run
  id 0 (nothing recorded then), the key-limit wipe, and the text. The handler only passes `spire_run_mag_key(c)`,
  `run->run_id` and `before`, sends `notice.text` when `notice.tell`, and logs `notice.note`. Static case "pack-full
  decision per client key, run and step": two players of one run with step 1 (the lab case, newserv.log:183897 and
  :184006) each get their own notice and are silent the second time; a later step and a new run tell; RESULT 1,
  nothing settled, an empty key and run id 0 tell nothing and record nothing; the first-mag text; a third key below
  the limit wipes nothing; at the limit a known key does not wipe, a new key wipes and the old key is told once more.
- N4: the wipe. `SPIRE_RUN_MAG_PACK_FULL_KEYS_MAX = 1024`: when a new key arrives with 1024 records, every record is
  cleared (the ledger store does the same, SpireFlagBridge.cc spire_run_mag_ledger_for). Effect: anyone can be told
  once more for a step, never less; on a home server it is not reached. (Round 1 wiped at more than 1024, now at
  1024: the store holds at most 1024 records.)
- N5: one B0 ticker line, the newest replaces it (SendCommands.cc:920-924). The notice replaces "Room clear!" and can
  itself be replaced within seconds by a leader's wave text sent to all clients; same exposure as the e5f4748 text,
  no regression. Not changed.

Tests: `./build/newserv run-static-tests` on the committed tree (commit 05:52:02, `git diff --quiet HEAD` exit 0):
exit 0, `05:52:03 -- All static tests passed`. md5 `91e9a3346ba65d3a84e2e089467fcb99`.

Mutations of src/SpireMag.cc in a throwaway copy (`git archive HEAD`), each from the clean file:

| Mutation | Build | Tests | Failing line |
|---|---|---|---|
| `tell = true` after the notice call | 0 | 134 | `StaticTests.cc:4871` (second call for a, step 1) |
| key -> constant (`told["same"]`) | 0 | 134 | `:4870` (player b not told) |
| run id -> 0 in the notice call | 0 | 134 | `:4874` (new run not told) |
| wipe at `>= 1` instead of the limit (`>= 0` does not build: -Werror=type-limits, build exit 2, so not counted) | 0 | 134 | `:4871` |
| first-mag condition inverted | 0 | 134 | `:4868` (wrong text) |
| restored | 0 | 0 | `05:54:55 -- All static tests passed` |

What stays untested: the three handler lines (`spire_run_mag_key(c)`, `run->run_id`, `spire_send_ticker`); no
static test builds a Client.
