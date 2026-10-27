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
