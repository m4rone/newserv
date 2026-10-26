# Route B fix round 2: run-mag step counting, repeated 'F'

Regie: B-REGIE-14-10 (counting rule) and the fix round 1 observations. Base 8facabf.
Commits (all pushed to m4rone/spike/route-b): item 1 = 2fef847, item 3 = 3fb71c6, item 2 = 90eea8e, then this report.

## Premises (read first)

All four premises of the brief held on 8facabf:
- The server counted a step at RESULT 1 (`spire_run_mag_result`, `issued_step = max(issued_step, offered_step)`).
- `spire_meta_on_finish` set the payout flags at every won 'F' ("each win opens them again").
- Without a Payout block a repeated not-won 'F' paid the old V again. A repeated *won* 'F' already paid nothing, because
  L4's stats create the run's credit entry after the first one.
- The 'I' item-count term is there (see Observations).

Two caveats on "every new test seen failing":
1. **Item 1, "RESULT 1 then the next boss's EXAM".** On 8facabf this case fails only at its check "counted at the
   answer" (`issued_step == 1` right after the answer). The rest of the case (confirm, then the next step) also passes on
   the old code.
2. **Items 2 and 3.** The section logs every check before it asserts, as in fix round 1. Three checks discriminate.
   The other seven pass on old and new code and serve as regression guards: first payout, partner-left win, another
   run's win, run id 0, first V.

## Coverage claim

Covers: run-mag step counting and its persistence, repeated-'F' idempotence of payouts and unlocks.
Does NOT cover: the quest's mag code, client behaviour, balance.

## Item 1 (2fef847): a step counts at the answer

| Where | Change |
|---|---|
| `SpireMag.cc` `spire_run_mag_decide` | An answer with result 1 sets `issued_step = expected_step`. It first stores the previous value in the new field `step_before_offer`. |
| `spire_run_mag_result` | RESULT 1 only confirms (`last_mirrored = offered`). RESULT 0 restores `issued_step = step_before_offer`, so the step is owed again and the boss room stays counted. |
| `spire_run_mag_follow_mirror` | If the overlay holds exactly one type-02 item equal to the outstanding offered mag, the offer counts as confirmed, as RESULT 1 would confirm it. That item is the lost-RESULT case after a successful create. |
| `SpireMag.hh` | Comments now give the rule. The ASK comment says "non-boss room clear" (it said "room start"). |
| `SpireFlagBridge.cc` 'Z' RESULT | The first-mag or evolve text keys on `step_before_offer`. The RESULT 0 log says "step N given back". The RESULT 1 log says "confirmed". |
| `StaticTests.cc` | 4 new F6 cases. In the old idempotence case, two lines that asserted the old rule were moved to the new rule: "issued -1 after the answer" and "the same ASK answers again". |

Persistence: `IssuedStep` was already in the savestate JSON. It now carries the value from the answer. The savestate is
written at every 'V' (room start), so a RESULT lost before that 'V' is persisted as issued. `step_before_offer` and
`offered` stay in memory only. A RESULT never refers to an answer from before a resume.

Red run on 8facabf logic. Each f6_case was wrapped in a temporary try/catch so every case reports; the wrapper is not
committed: `/usr/bin/grep -ac "try { fn" src/StaticTests.cc` gives 0. Exit 0, because the wrapper caught the failures:
```
E    F6 (counting: answer, the RESULT lost, then ASK) FAILED: failure at src/StaticTests.cc:4160: 0 != led.issued_step
E    F6 (counting: answer, RESULT 0, then ASK gives the same step) FAILED: failure at src/StaticTests.cc:4187: 1 != led.issued_step
E    F6 (counting: answer, RESULT 1, then the next boss's EXAM gives the next step) FAILED: failure at src/StaticTests.cc:4205: 1 != led.issued_step
E    F6 (counting: a resume with the issued step persisted issues nothing again) FAILED: failure at src/StaticTests.cc:4232: 2 != back.run_mag->issued_step
```

## Item 3 (3fb71c6): the legacy V once per run

`spire_records_finish_splinters`, legacy path with a run id: after paying the V it creates the run's credit entry with
these values:
- `Finished` set
- rooms and bosses of this game marked paid
- `Base` = the V
- `BonusPaid` 0xFFFFFFFF

A repeated 'F' of that run then takes the existing "came before" branch and pays 0. A Payout block that appears before
a later 'F' pays no room twice and no pact bonus. Run id 0 (no 'B') pays as before.

## Item 2 (90eea8e): no payout flag reopened by a repeated won 'F'

- **New pure function.** `spire_meta_apply_finish(flags, finish_flags, difficulty, ladder_run, open_payout)` is the
  flag loop taken out of `spire_meta_on_finish`. It sets a payout flag (0x3A0-0x3A2) only when `open_payout` is true.
  Every other flag is set as before, and a flag that is already set is not named again.
- **New parameter.** `spire_meta_on_finish` takes `open_payout`. The 'F' handler passes `stats.won_counted`: true only
  at the first won 'F' of a run (L4's per-run `Won`), and true every time for run id 0, as FX-7 already does. A
  partner-left not-won 'F' followed by the resumed win still opens the payout once.
- **Log.** A skipped flag shows in the log as `03A0(repeat: not set)`. No "Reward waiting" text is shown for a repeat.

Red run on 8facabf logic. The function was first taken out with the old behaviour (`open_payout` ignored), and the
legacy path was unchanged. Exit 134:
```
R2 check legacy: the same not-won 'F' again pays nothing: FAILS
R2 check legacy: a later 'F' with a Payout block pays no room twice: FAILS
R2 check meta: the same won 'F' again reopens no payout flag: FAILS
(other 7 R2 checks: ok, see premise caveat 2)
```

## Green: the full static run on 90eea8e

The build log has no error or warning line (`build 0`). `./build/newserv run-static-tests` exited 0. I read all 173
lines:
- 66 section headers.
- 50 "check ...: ok" lines (F1 fix, F2 fix, R2) and 0 FAIL lines.
- All 14 F6 cases are listed, including the 4 new "counting" cases.
- The only W lines are the 7 expected ones from the F1 C4/C5 checks (missing dir errno 2, unreadable Overlay or
  RunCopyPrint), the same as in fix round 1.
- The last line is `-- All static tests passed`.

The item-3-only tree (3fb71c6) was also built and run on its own: exit 0, 5/5 R2 checks ok.

## Decisions (FX2)

- FX2-1: The give-back value lives in a new in-memory field, `step_before_offer`. An earlier offer whose RESULT never
  came stays issued, so a RESULT 0 of the next answer returns to that step and not further back. Dekking: B-REGIE-14-10
  sentences 1-3.
- FX2-2: `last_mirrored` moves at RESULT 1 (or at a mirrored copy of the offer), never at the answer. If it moved at the
  answer, a lost RESULT 0 would make the savestate filter drop the old mag (it keeps only the ledger's bytes).
- FX2-3: Follow-mirror confirms an offer whose exact bytes are the only type-02 item in the overlay. Without this, a
  lost RESULT on the first (kit) mag would leave `last_mirrored` empty, and every later savestate would drop the kit
  mag. Assumption: the overlay mirrors the quest's item_create2, as the feed mirror does (F6S-6). This is not measured
  on a client.
- FX2-4: Item 2 reuses L4's per-run `Won` flag (`won_counted`) as the "first won 'F'" signal. It adds no new field in
  meta.json.
- FX2-5: The legacy entry marks this game's rooms and bosses paid and its bonus paid in full (as FX-6 does for old
  entries). Rooms cleared before a resume are not in that set, because the legacy V counts them only as a number.
- FX2-6: Two red runs. The first used a test-only try/catch wrapper. The second used the extracted function with its
  old behaviour, in the same tree. Neither scaffold is committed.

Nothing was written in `/home/m4rck/pso-roguelike`, so no `decisions.md` entry and no expansion request. The decisions
above are for the regie to log.

## Observations (not built)

- Item 4, pre-existing, legacy path: `spire_guard_retake_ok` (src/SpireFlagBridge.cc:2953) calls
  `spire_guard_snapshot_not_worse`, whose term `inventory.num_items >= snapshot.inventory.num_items`
  (src/SpireFlagBridge.cc:2880) can refuse the real character at 'I'. It does so when that character's 61 lists only
  equipped items (the save form, routeb-mag-vraag E3). With SpireBaseQuest set, 'I' is not used, so the path is out of
  service.
- Known degrade, now as the protocol states it: a lost RESULT 0 leaves the step issued while the client kept the old
  mag. That step's evolve is lost; the next EXAM issues the later step, which is absolute. No item is lost or doubled.
- Item 1 changes when the evolve text is shown. After a lost RESULT 1 the player gets no "Your mag evolves" text; the
  mag is there. Cosmetic.
- The legacy entry does not cover rooms cleared before a resume (FX2-5). It matters only if a Payout block appears
  between two 'F's of one resumed run.

## Hard limits kept

No deploy, no apply-on-pc.sh, no ssh, no newserv restart, no write in lab/ or ~/pso-tools/newserv/system/, no memory
card touched, no write in /home/m4rck/pso-roguelike. Static tests only; no client measured.
