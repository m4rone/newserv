# Route B F1: guard hardening (newserv side)

Branch spike/route-b, base 8ddedba. Code commit 172fd98.

## Premises checked first

1. **(d) cannot fail on 8ddedba.** The B-BASIS-60 case (snapshot M4ia Lv162 / 27 items, candidate Lv17 / 3 items)
   is already refused by the v56 rule `spire_guard_snapshot_not_worse` (d0bb211). The test is a regression guard. In
   the red run it showed `PASSED` (see below), with and without a stored print.
2. **(c) expects true, and 8ddedba also returns true.** Its red failure comes from the new "no print stored, snapshot
   stays" sub-check (F1-1). The main assertions of (c) are guards against over-blocking.
3. **(f) and the savestate fixture describe behaviour that 8ddedba already has.** The restore verification compares
   all 12 data1 bytes (`spire_guard_same_inventory`, v31/v32). The savestate still drops every type-02 item. Both
   passed in the red run. They become meaningful once F6 changes the savestate and the restore. Each test contains a
   false assertion next to a true one (pending vs verified, run mag dropped vs weapon kept), so neither is vacuous.
4. **Amendment 1 says "the type-02 item becomes the ledgered run mag; a second test drops an unledgered one".** The
   ledger arrives with F6. As the brief says, in F1 the fixture is a run mag (HU L22 bytes) that is still dropped, with
   the measured fact in the comment. F6 must add the "ledgered mag kept" case.
5. Plan line references (cc:2364-2391, cc:461-462, StaticTests.cc:2979-2989) come from d0bb211. At 8ddedba the same
   code is at SpireFlagBridge.cc:2447-2551, :462 and StaticTests.cc:3537-3552. I located them by symbol.

## Coverage claim

Covers: the retake decision at both callers (print, run-mag bytes, missing-real-mag signature), the savestate
comment/fixture fix, the hard-stop restore test.
Does NOT cover: the 'Z' handler and ledger (F6), the base game's card-write gap at join, client behaviour.

## What changed

- `src/SpireFlagBridge.hh`: `SpireRunCopyPrint {exists, level, exp, num_items, hash}`, and the field
  `SpireGuardSnapshot::run_copy_print`. New declarations: `spire_guard_run_copy_print`, `spire_guard_matches_print`,
  `SpireRunMagBytes` (= `std::optional<std::array<uint8_t, 12>>`), `spire_guard_holds_run_mag`,
  `spire_guard_run_copy_signature` and `spire_guard_retake_ok`. `spire_guard_run_again_retake` and
  `spire_guard_base_exit_follow` take a `const SpireRunMagBytes&` with no default. The `SPIRE_RESUME_MAG_TEXT` comment
  no longer says the run copy holds the real mag. The text itself is unchanged; F6 replaces it.
- `src/SpireFlagBridge.cc`:
  - `spire_guard_retake_ok = not_worse && print stored && !matches_print && !holds_run_mag && !run_copy_signature`.
    It replaces `spire_guard_snapshot_not_worse` in both callers.
  - `'O'` (`spire_base_return`, in-game push path, inside the restore lock): before `spire_guard_run_restore`, it takes
    the print from the overlay (`c->character_file(false, true)`) and saves it to the snapshot file, then logs
    `run copy print stored ... hash`.
  - The snapshot file has a new `RunCopyPrint {Level, EXP, NumItems, Hash}` block. Hash is a 16-digit hex string.
  - `spire_guard_run_start_apply` clears the print once the marker is set (FRESH and KEPT_UNCONFIRMED).
  - The ledger hook is `static spire_guard_ledger_run_mag(c)`, which returns `std::nullopt` until F6. Both callers
    pass it explicitly. Both refusal logs now show the print match and the signature.
  - The `spire_savestate_items` comment states the measured fact: server strip at Client.cc:696-697 under FORBID_ALL,
    client strip from the MEM1 dumps and the journal of 2026-10-26 20:47:39-41.
- `src/StaticTests.cc`: a new section "a run copy never becomes the snapshot at either re-take (F1, route B)" with
  cases a-f plus a print case. Existing calls pass `std::nullopt`. The v49 and v51 fixtures store a run-copy print, as
  'O' now always does. The savestate fixture holds a run mag, not "the real mag kept by forbid_mags 0".

## Tests: failing first, then passing

Red build: the final tests, with `spire_guard_retake_ok` stubbed to the 8ddedba rule (`return
spire_guard_snapshot_not_worse(...)`). A temporary try/catch around each F1 case let all cases report. Neither the
stub nor the try/catch is committed. `./build/newserv run-static-tests`, exit 0 (the scaffold caught the failures):

```
E ...    F1 (a: a stronger run copy with the stored print) FAILED: !(!spire_guard_retake_ok(run_copy.first, run_copy.second, snap, std::nullopt)) at line 562
E ...    F1 (b: a different print, but holding the ledger's run mag) FAILED: !(!spire_guard_retake_ok(run_copy.first, run_copy.second, snap, run_mag)) at line 588
E ...    F1 (c: the real character after a verified restore plus one bank item) FAILED: !(!spire_guard_retake_ok(with_bank_item.first, with_bank_item.second, snap, std::nullopt)) at line 618
I ...    F1 (d: the live case of 2026-10-26 20:47:39 (B-BASIS-60)) PASSED
E ...    F1 (e: the run copy signature (amendment 6)) FAILED: !(!spire_guard_retake_ok(stronger.first, stronger.second, snap, std::nullopt)) at line 644
I ...    F1 (print: kept in the run, cleared by the next run start) PASSED
I ...    F1 (f: hard stop mid-run, the login 61 is the run copy's save form (amendment 7)) PASSED
```

The line numbers are from the red build; the scaffold added 5 lines, so in 172fd98 they are 557, 583, 613 and 639.
Line 562 is the (a) candidate Lv19 / 12 items with the print equal. On 8ddedba `retake_ok` is true there; the line
before it asserts that `spire_guard_snapshot_not_worse` alone lets this candidate in. Line 588 is (b) with the ledger
mag bytes. Line 618 is (c) without a stored print. Line 644 is (e), Kama in the snapshot and no 02 06 in the candidate.
The print case passed because clearing at run start was already built. That field does not exist on 8ddedba.

Green build (172fd98), full `./build/newserv run-static-tests`, exit 0. I read all 96 lines. Every section is
listed. There is no `E ` line, no warning and no skip. The last line is `-- All static tests passed`. The build log
has no `warning` or `error` line.

```
I ... -- Spire guard: a run copy never becomes the snapshot at either re-take (F1, route B)
I ...    F1 (a: a stronger run copy with the stored print)
I ...    F1 (b: a different print, but holding the ledger's run mag)
I ...    F1 (c: the real character after a verified restore plus one bank item)
I ...    F1 (d: the live case of 2026-10-26 20:47:39 (B-BASIS-60))
I ...    F1 (e: the run copy signature (amendment 6))
I ...    F1 (print: kept in the run, cleared by the next run start)
I ...    F1 (f: hard stop mid-run, the login 61 is the run copy's save form (amendment 7))
...
I ... -- Spire savestate: written from the run copy, replaced by the next, deleted only at a true run end (v44)
I ... -- All static tests passed
```

What each case asserts:
- (a) Both callers return false for Lv19 / 12 items with the print equal. They also return false for the save form
  (4 items, print level and EXP) and for the same items at another level (hash only). The snapshot stays unchanged.
- (b) The print differs and the candidate holds the exact 12 bytes: false at both callers. The same mag kind with
  other stat bytes is not the ledger mag. Without a ledger, `holds_run_mag` is false.
- (c) Real character plus one bank item, print different, with the real mag: true at both callers, and the snapshot
  takes 21 items. With no print stored: false, and the snapshot is unchanged.
- (d) Lv162 / 27 items (Pitri) against Lv17 / 3 items: false, with and without a print.
- (e) Kama in the snapshot, stronger candidate without 02 06: false. With a run mag of another kind: false. At the base
  exit: false. Kama fed in the base (same data1[0-1], other bytes): no signature, true. A snapshot without a mag: the
  signature never fires, true.
- (f) Run copy save form = Lv17, 3 items and an equipped run mag of Pitri's kind. Under AwaitingSave alone it is no
  confirmation, so RestorePending is set. After a push it is not verified when it still shows the save form, or when
  it has the real level, EXP and count but the run mag in Pitri's place. It is verified only when Pitri's 12 bytes are
  back (in another order with other IDs). A re-take of the save form is false.

## Technical decisions

- F1-1: A re-take needs a stored print. No print means doubt, and the snapshot stays. Both callers need `'O' OK`
  proven in this game, and that path always stores a print first. A missing print therefore means something went
  wrong. The v49 and v51 fixtures were given a run-copy print.
- F1-2: The print is stored only in the in-game push path of `'O'` (refusal NONE), which is the only path that leads
  to `base_returned`. The base-game path (SpireBaseQuest, BASE_GAME) reaches neither caller, so its flow is unchanged.
  If the print cannot be saved, a warning is logged and the push goes on. No re-take can follow that `'O'` (F1-1), so
  the failure errs safe and the `'O'` flow is unchanged.
- F1-3: The print is cleared at every run start that sets the marker (FRESH, KEPT_UNCONFIRMED), so a print from an
  earlier run never stands in for this run's print.
- F1-4: The hash is fnv1a64 over the sorted 16-byte keys (data1 + data2) and then the meseta (u32). It ignores item
  order and IDs, like `spire_guard_same_inventory`. In the file it is a hex string, because a u64 does not fit a signed
  JSON integer.
- F1-5: The signature fires when any type-02 item in the snapshot has a kind (data1[1]) that no type-02 item in the
  candidate has. This covers characters with two mags, such as M4ia (Pitri plus a Mag LV5). It errs safe: a mag sold or
  swapped in the base keeps the stored snapshot.
- F1-6: The ledger hook is one static function, so F6 fills one place. The callers have no default argument, so a new
  caller cannot leave the hook out.
- F1-7: The same wrong claim in the `SPIRE_RESUME_MAG_TEXT` comment (hh) was corrected. The player text was left
  as is, because F6 owns it.
- F1-8: (f) mirrors the checks `spire_guard_verify_restore` makes (stats, `same_inventory`, `same_materials`,
  `push_verified`), as the existing v32 tests do. `verify_restore` itself was not refactored.

## Observations (not built)

- `new-features/roguelike/output/roguelike-patronen-2026-10-26.md:12` and `:197` still state the wrong
  "real mag in the run copy" claim (amendment 1). That file is outside this writeSet and is for the regie.
- F6 must: return the ledger's `last_mirrored` from `spire_guard_ledger_run_mag`; add the savestate case "ledgered run
  mag kept, unledgered dropped"; and replace `SPIRE_RESUME_MAG_TEXT`.
- The `'O'` print is not exercised end-to-end: no static test can build a `Client`. The wiring is the source at
  `spire_base_return` plus the log line `run copy print stored`.

## Hard limits kept

No deploy, no ssh, no newserv restart, no write to `lab/`, `~/pso-tools/newserv/system/` or
`/home/m4rck/pso-roguelike`, no memory card touched.
