# Route B fix round 1: F1 (guard) and F2 (splinters)

Reviews: `new-features/roguelike/output/routeb-f1-critic.md`, `routeb-f2-critic.md`. Regie: B-REGIE-14-9.
Base 584686e. Step A = 068cced, step B = 14fdee3 (both pushed to m4rone/spike/route-b).

## Premise corrections (read first)

1. **Not every new test can fail on the old code.** The C2 JSON round trip of the guard file was correct on
   584686e, so its round-trip checks pass before and after: they are a regression guard (a key typo or a base-10
   `stoull` would make "C2 round trip: print" fail, by construction; not measured with a mutation). Likewise the C4
   checks "storing no print succeeds", "storing the print succeeds", "the new print is in the file" and "a failed save
   is reported" pass on the old logic; the C4 check that separates old from new is "no stale print in the file".
2. **M2 cannot fail on correct code.** It is a test-strength fixture; its failing evidence is the critic's mutation
   (`e->base` -> `rec.splinters`) injected in the before-run build only (behind an env var, never committed).
3. **L4: Runs and best time do not double.** Runs is counted at 'B' (`spire_run_counts_as_new`), never at 'F'; best
   score and best time are a maximum and a minimum, so a repeat is a no-op. Only Won and the difficulty's Won doubled.
4. **The before-runs needed a shim** because two new tests call API that did not exist: `spire_guard_load/save`
   made non-static (no behaviour change) and `spire_guard_store_print` / `spire_records_finish_stats` written with the
   old inline logic of the 'O' print step and the 'F' stats. The shims were replaced by the real code before commit.

## Coverage claim

Covers: the retake decision, print lifecycle and guard-file load robustness, the in-game-61 mark, room credit after
a partner-left 'F', the pact bonus with a prior balance, repeated 'F' stats. Does NOT cover: the mag swap on the quest
side, client behaviour, balance.

## Step A (068cced): F1 fixes

| Finding | Change |
|---|---|
| C1 | `spire_guard_retake_ok` requires `level == snapshot` and `exp == snapshot` (one rule for both callers; the duplicate in `spire_guard_base_exit_follow` is gone). `spire_guard_matches_print` is hash only (FX-1). |
| C3 | `spire_guard_run_copy_signature` removed with its hh claim; the hh now states why (bank mag swap). Case e rewritten: the 98 at the real level/EXP without the mag is now accepted. |
| C2 | New discriminating checks: print level + other EXP; snapshot level + 1 EXP; round trip of the guard file with a hash above 2^63. |
| C4 | `spire_guard_store_print`: saves without the old print, then with the new one; false on any save failure. The 'O' caller skips the push when it is false (answer PUSH_NOT_VERIFIED). Log lines now match the file. |
| C5 | `spire_guard_load`: Overlay, QuestNumber, SnapshotTime and RunCopyPrint read as absent with a `player_data_log` warning; Hash must be exactly 16 hex digits. |
| C6 | `SpireRunCopyPrint.exp` and `.num_items` removed (JSON keys EXP/NumItems no longer written; old files load fine, the keys are ignored). |
| 61 mark | `src/ReceiveCommands.cc` on_61_98, after `decode_from_client`: `if (msg.command == 0x61) spire_run_mag_note_in_game_61(c);` plus one comment line. |

Before (step-A tests on 584686e logic, `testA1`; exit 134):
```
F1 fix check C1 retake_ok refuses the save form: FAILS
F1 fix check C1 'I' refuses the save form: FAILS
F1 fix check C1 the snapshot stays: FAILS
F1 fix check C1 'I' refuses the save form (snapshot with a plain mag): FAILS
F1 fix check C2 'I' refuses the snapshot's level with one EXP point more: FAILS
F1 fix check C2 'I' takes the real character with a bank item: ok
F1 fix check C3 'I' takes the swapped mag: FAILS
F1 fix check C3 the snapshot holds Sato, not Kama: FAILS
F1 fix check C3 the 98 after 'O' takes the swapped mag: FAILS
F1 fix check C3 the exit restore pushes Sato, not Kama: FAILS
F1 fix check C2 round trip: markers / character / print: ok (see premise 1)
F1 fix check C4 no stale print in the file: FAILS
F1 fix check C5 no throw for {"Level": 18, "Hash": "XYZ"}: FAILS   (and for "Hash": 7 and "Level": "abc")
F1 fix check C5 reads as no print, the snapshot stays, ...: FAILS   (all three)
```
C3 before/after: on 584686e the bank mag swap (deposit Kama, withdraw Sato, same level/EXP/count) was refused at
'I' and at the 98, so the exit restore pushed Kama back (duplicate with the bank copy) and Sato was lost. After: both
callers take the swap, and the snapshot (what the exit restore pushes) holds Sato and not Kama.

After (068cced): `./build/newserv run-static-tests` exit 0, `-- All static tests passed`, 64 sections, 24/24 F1 fix
checks ok. The only W lines are the expected ones from the C4/C5 checks (missing dir errno 2, unreadable Overlay or
RunCopyPrint).

## Step B (14fdee3): F2 fixes

| Finding | Change |
|---|---|
| M1 | `Finished` gate on room credit removed. `SpireRunCredit.bonus_paid` stored as `BonusPaid`; each 'F' pays `max(0, floor(base * pacts * tenths / 10) - BonusPaid)`. A repeated 'F' with Payout logs "came before: paid only the difference". |
| M2 | Fixture with `Splinters` 100 before the run: 'F' with 4 pacts adds 20 (balance 170), not 60. |
| L4 | `spire_records_finish_stats` (pure, tested): Won and the difficulty's Won count a run id once (`Won` flag in the credit entry); called after the splinters in the 'F' handler. |
| existing test | "a late room of a finished run pays nothing" now asserts the M1 behaviour (pays 3). |

Before (step-B tests on 068cced logic, `testB0-plain` / `testB0-mutant`; exit 134 each):
```
plain:  M1 the resumed k7-k10 pay 22 after the 'F': FAILS
        M1 the second 'F' pays only the difference floor(50 * 2 / 10) - 5 = 5: FAILS
        M1 balance 28 + 5 + 22 + 5 = 60: FAILS
        M1 BonusPaid 10 is in the file: FAILS
        M1 a resent 'F' pays 0: FAILS
        M2 'F' with 4 pacts adds 20, not 60: ok        (correct code, premise 2)
        L4 the same 'F' again (after a reload) counts nothing: FAILS
        L4 another run's won 'F' counts: FAILS
        L4 a not-won 'F' counts no win: FAILS
        L4 the resumed run's win counts once: FAILS
mutant: M2 'F' with 4 pacts adds 20, not 60: FAILS   (e->base -> rec.splinters)
```
After (14fdee3): exit 0, `-- All static tests passed`, 65 sections, 16/16 F2 fix checks ok, 24/24 F1 fix checks ok;
same expected W lines only.

## Decisions (FX)

- FX-1 — the print's level term is dropped as well; only the hash term stays. With exact level and EXP the level
  term catches only a run copy whose EXP equals the real one to the point, but it refuses the real character whenever
  the run reached the real level, and a refusal makes the exit restore push base changes away (the C3 loss shape).
  The hash stays: it truly matches a 98 (full inventory) of the run copy (case a `equal_stats`), and a real character
  equal to the mirror in every item and the meseta is not realistic. Print level is kept for the log only.
  Dekking: B-REGIE-14-9 ("hooguit als extra op level"), brief step A C1.
- FX-2 — the equality sits in `spire_guard_retake_ok`, so both re-take callers share one rule.
- FX-3 — a print that cannot be saved refuses the push through the existing PUSH_NOT_VERIFIED answer (no new quest
  register value).
- FX-4 — C5 tolerance covers the advisory fields (Overlay, QuestNumber, SnapshotTime, RunCopyPrint). RunInProgress,
  RestorePending, AwaitingSave, Inventory and Disp stay strict: a corrupt character cannot be "kept", and refusing
  the login there is the step that leaves the card untouched.
- FX-5 — `spire_guard_load` and `spire_guard_save` are no longer static (declared in the hh) for the round-trip test.
- FX-6 — a credit entry saved before this fix (`Finished` true, no `BonusPaid`) loads with BonusPaid 0xFFFFFFFF:
  it never pays a bonus again (the old rule), at the cost of no difference for such a run if it was resumed.
- FX-7 — L4's per-run Won flag lives in the credit entry (`Won`), created if needed; run id 0 (no 'B') counts every
  time as before. The stats run after `spire_records_finish_splinters` so the new entry cannot divert the legacy V.

## Observations (not built; for the regie)

- pre-existing, SpireMeta.cc `spire_meta_on_finish`: a repeated won 'F' sets the payout flags again ("each win opens
  them again"), so after q925 cleared them a resend would reopen a payout. Outside this writeSet's need.
- pre-existing, legacy path (no Payout block): a repeated not-won 'F' with no credit entry pays the old V again.
  Only when the catalogue is broken.
- pre-existing, 'I' re-take: if the real character's 61 at 'I' is the save form (equipped items only, routeb-mag-vraag
  E3), the not-worse item-count term refuses it. The path is out of service with SpireBaseQuest (critic premise 1).
- The new on_61_98 line duplicates the explicit `spire_run_mag_note_in_game_61` calls after `send_get_player_info`
  in SpireFlagBridge.cc if those 61s pass through on_61_98: idempotent, one extra log line at most.
- F2 critic L1, L2, L3, L5 and the live-glue test gap (M2 second part) were not in this brief; untouched.

## Verification note

Static tests only; nothing deployed, no newserv restarted, no client measured.
