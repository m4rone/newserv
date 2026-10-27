# Route B bundle 2, F10 server half: mag lean

Branch `spike/route-b-2`, base 15209bf (phase S). Code commit: **9ee74be** (`feat: mag lean in ASK/EXAM bits 8-15,
Shell column in spire-mag.json`). Measured 2026-10-27 in this worktree.

**Covers:** the server side of the lean: the bits in the ASK/EXAM word, storing them in the ledger, the column choice,
the `ShellSteps` in spire-mag.json, the evolve text, and the static tests.
**Does not cover:** the quest menu (leader and follower), client behaviour on the Wii or Dolphin, and the balance of
Shell against Fang (N12, for the F11 sim). The wiring in SpireFlagBridge.cc (reading the word, the log lines, the
ticker) cannot be reached by a static test; only the pure parts in SpireMag.cc are.

Contract: `pso-roguelike/new-features/roguelike/output/routeb2-f10-randgevallen.md`, section "Besluit na de critic",
points 1, 2, 6, 7 and 8.

## Changes (line numbers at 9ee74be)

### src/SpireMag.hh
- :16-24: the request word is now `'Z' << 24 | sub << 16 | lean << 8 | k`, with the lean rule written out.
- :37-39: `SPIRE_RUN_MAG_LEAN_NONE = 0`, `SPIRE_RUN_MAG_LEAN_FANG = 1`, `SPIRE_RUN_MAG_LEAN_SHELL = 2`. This is the
  encoding that :104 (`lean` in the ledger) already had at f27d4f8 (B2).
- :65-69: `SpireMagLine::shell_steps` and `shell_note` ("" when the column is usable, otherwise the reason).
- :80-88: `SpireMagColumn spire_mag_column(line, lean)`.
- :126: `SpireRunMagLedger::offered_shell` (in memory only, like `step_before_offer`).
- :130-144: `spire_run_mag_lean_bits(value)`, `enum class SpireRunMagLeanStore {NONE, STORED, AFTER_STEP, INVALID}`,
  `spire_run_mag_lean_store(ledger, lean)`, `spire_run_mag_lean_store_name`.
- :193-208: `SpireRunMagHandled` gains `lean_store`, `shell`, `shell_fell_back`; `spire_run_mag_handle` gains a
  required `uint8_t lean_bits`.
- :234: `spire_run_mag_evolve_text(name, bool shell = false)`.

### src/SpireMag.cc
- :59-77: `ShellSteps` is optional and never fatal. If it is missing, the note says "no ShellSteps". If it is not as
  long as `Steps`, the note says "ShellSteps has N rows, Steps M". If it is unreadable (not a list, a missing key, a
  stat above 200), the note says "ShellSteps unreadable: ...". In all three cases `shell_steps` stays empty and the
  table is still parsed: no NO_TABLE.
- :90-103: `spire_mag_column`. Lean 2 with a usable `shell_steps` (as long as `steps`) gives a copy of the line with
  `steps = shell_steps`. Lean 2 without one sets `fell_back`. Any other lean gives the line as it is. StartFloors are
  the same single list for both columns.
- :306-337: `spire_run_mag_lean_bits` (`(value >> 8) & 0xFF`) and `spire_run_mag_lean_store`:
  - 0 gives NONE and stores nothing (a choice made earlier stays);
  - above 2 gives INVALID;
  - `issued_step > 0` gives AFTER_STEP;
  - otherwise the value is stored.
- :339-358: `spire_run_mag_handle` runs in this order:
  1. the start step (unchanged);
  2. **storing the lean, before the decision**;
  3. follow the mirror;
  4. old type;
  5. the column from **the ledger's lean** (not from the bits);
  6. `spire_run_mag_decide` on that column;
  7. `offered_shell` on a result 1.

  The answer word (r41) is built as before from `old_type` and `result`. The lean only chooses the row.
- :376, :387: `offered_shell` is reset together with `offered` (RESULT, and the confirm from the mirror).
- :422-424: the evolve text: `"Your mag evolves: {}{}. Equip it: Item Pack > Mag."` with " (Shell)" only when `shell`.

### src/SpireFlagBridge.cc
- :2290: `lean_bits = spire_run_mag_lean_bits(value)`.
- :2297-2301: sub 5 LEAN no longer stores without a check. It uses the same `spire_run_mag_lean_store` (the same
  `issued_step <= 0` rule), and the log names the outcome.
- :2315, :2336-2339: RESULT 1 sends `spire_run_mag_evolve_text(name, offered_shell)`, and the log adds "(Shell)".
- :2411-2415: `spire_run_mag_handle(..., lean_bits)`. When lean is Shell and the line has no usable ShellSteps, one
  warning line per ASK/EXAM is logged, with `shell_note`.
- :2421-2424: the ASK/EXAM log line carries `lean bits N <outcome>, lean L (Shell column|Steps)`.
- :2431-2432: a refused ASK/EXAM with lean bits logs "lean bits N not stored (refused)".

### system/spire-mag.json
`_comment` and `_what` (per class) updated. `ShellSteps` per class, values DEF/POW/DEX/MIND:

| Step | HU | Id | RA | Id | FO | Id |
|---|---|---|---|---|---|---|
| 0 | 5/0/0/0 | 0 Mag | 5/0/0/0 | 0 Mag | 5/0/0/0 | 0 Mag |
| 1 | 18/7/0/0 | 1 Varuna | 16/6/3/0 | 13 Kalki | 18/0/0/7 | 25 Vritra |
| 2 | 30/9/1/0 | 14 Rudra | 27/8/5/0 | 3 Surya | 30/0/1/9 | 26 Namuci |
| 3 | 42/12/1/0 | 5 Varaha | 38/10/7/0 | 6 Kama | 42/0/1/12 | 30 Kabanda |
| 4 | 46/14/5/5 | 6 Kama | 46/12/7/5 | 38 Madhu | 46/5/5/14 | **36 Bana** |

I checked this myself with a script against the committed file. For every step from 1 on, Shell equals Steps with DEF
swapped with POW (HU/RA) or MIND (FO); step 0 is equal on purpose. The FO Fang Id's in the json are 0/25/26/30/28.
Shell takes them over except step 4, which is 36. Every Id is in `system/tables/names-v3.json` as `02ZZ00`: 020000 Mag,
020100 Varuna, 020E00 Rudra, 020500 Varaha, 020600 Kama, 020D00 Kalki, 020300 Surya, 022600 Madhu, 021900 Vritra,
021A00 Namuci, 021E00 Kabanda, 022400 Bana.

### src/StaticTests.cc
- :4106-4169: "every row of the shipped spire-mag.json" now runs for both columns. It checks, per class:
  - `shell_note == ""` and a ShellSteps as long as Steps;
  - names-v3, an even stat and LL = the sum, per row;
  - monotone per stat, a new Id and a higher LL per step;
  - every step under start bonus 0/5/10/14.
- :4341, :4354-4373: the existing resume round trip ("counting: a resume ...") now carries `lean = 2` through the
  savestate. After the resume, an EXAM at k11 with lean bits 0 gets the Shell row of step 3 (DEF 42), not the Fang row.
  An old savestate without the "Lean" key loads as 0.
- :4545-4699: six new cases, `F6 (F10: ...)`:
  - the bits;
  - lean 0/1/2 (DEF-heavy row, default, the same answer word);
  - the `issued_step` rule, also for sub 5;
  - a practice run (HU k9 gives Rudra 30/9/1/0, FO k16 gives Bana 46/5/5/14);
  - json without, with a short or with a bad ShellSteps;
  - the evolve text.
- The nine existing `spire_run_mag_handle` calls get `lean_bits` 0.

## Red and green

**Build and test** as in ROUTEB2-S-REPORT.md: `cmake --build build -j8`, then `./build/newserv run-static-tests`.

**Green on the committed tree (9ee74be):** the last step of the mutation script restores every file from its copy and
`git diff HEAD --stat` is empty afterwards. The script then printed:
```
final build exit 0
final tests exit 0: I 2511272 2026-10-27 01:59:23 - -- All static tests passed
```
- The build log has 0 lines with "warning".
- There are 66 `--` sections.
- The six F10 cases are lines 174-179 of the log, and the extended resume case is line 165. Log:
  scratchpad `F10S/mut/final.test.log`.

**Without my change:** the tests do not compile on 15209bf (new symbols). The mutations below are therefore the real
red evidence.

**Red under mutation:** script `F10S/mut/run.sh`. Each mutation was made in one file, built, tested and restored from
a copy, and none was committed. Every mutation built with exit 0 and tested with exit 134.

| # | Mutation | Failing line (literal) |
|---|---|---|
| M01 | lean bits `>> 8` -> `>> 16` | `StaticTests.cc:4550: 2 != spire_run_mag_lean_bits(0x5A040204)` |
| M02 | column: Shell for every lean except 0 | `StaticTests.cc:4574: is_shell != e4.shell` |
| M03 | `issued_step > 0` -> `> 1` | `StaticTests.cc:4592: LS::AFTER_STEP != e8.lean_store` |
| M04 | `issued_step > 0` -> `>= 0` | `StaticTests.cc:4605: LS::STORED != spire_run_mag_lean_store(kit, SPIRE_RUN_MAG_LEAN_SHELL)` |
| M05 | no storing at all | `StaticTests.cc:4561: lean ? LS::STORED : LS::NONE != kit.lean_store` |
| M06 | storing after the decision (B1) | `StaticTests.cc:4623: LS::STORED != p9.lean_store` |
| M07 | column from the bits instead of the ledger | `StaticTests.cc:4359: !(e11.decision.result && e11.shell)` (resume) |
| M08 | bad ShellSteps throws (NO_TABLE) | `terminate ... std::runtime_error, what(): HU shell step 1: Def 300 outside 0-200` |
| M09 | parser without the length check | `StaticTests.cc:4667: !(line.shell_steps.empty())` |
| M10 | column without the length check | `StaticTests.cc:4686: !(col.fell_back && !col.shell)` |
| M11 | lean 0 is stored | `StaticTests.cc:4561: lean ? LS::STORED : LS::NONE != kit.lean_store` |
| M12 | lean 3 is accepted | `StaticTests.cc:4614: LS::INVALID != spire_run_mag_lean_store(none, 3)` |
| M13 | evolve text without "(Shell)" | `StaticTests.cc:4695: std::string("Your mag evolves: Varuna (Shell). ...") != spire_run_mag_evolve_text("Varuna", true)` |
| M14 | `offered_shell` never set | `StaticTests.cc:4575: is_shell != led.offered_shell` |
| M15 | json: HU Shell step 2 Id 14 -> 1 | `StaticTests.cc:4143: !(cur[1] != prev[1])` (Id per step) |
| M16 | json: FO Shell step 3 DEF 42 -> 29 | `StaticTests.cc:4141: !(word(cur, z) >= word(prev, z))` (monotone) |
| M17 | json: FO Shell step 4 back to Fang (14/5/5/46) | `StaticTests.cc:4141: !(word(cur, z) >= word(prev, z))` |
| M18 | RESULT does not reset `offered_shell` | `StaticTests.cc:4567: !(!led.offered_shell)` |

**Not killed by a test:** the `offered_shell` reset in `spire_run_mag_follow_mirror` (SpireMag.cc:387). It is
hygiene: every new offer writes the flag again (:355), and the bridge reads it only while `offered` is set. The bridge
wiring is not killed either (see Covers).

## Protocol block for ROUTEB-F6-PROTOCOL.md (quest repo, to copy as is)

```
### Lean (F10): bits 8-15 of every ASK and EXAM word

Word in r248: 'Z' << 24 | sub << 16 | lean << 8 | k
  sub 1 ASK, sub 4 EXAM; k = the room (bits 0-7) as before.
  lean (bits 8-15): 0 = no choice (default), 1 = Fang (HU/RA) or Mind (FO), 2 = Shell.
  The list order: index 0 = Fang/Mind, index 1 = Shell, so the quest sends lean = index + 1 (a cancel gives index 0
  = Fang/Mind = 1; sending 0 is also correct: 0 and 1 both give the default column).
  Example: an EXAM at k4 with Shell = 0x5A040204; the kit ASK at k1 with Shell = 0x5A010201.

Server rule (newserv SpireMag.cc spire_run_mag_lean_store, spire_run_mag_handle):
  - The server reads the lean from the word and stores it in the run mag ledger only when it is 1 or 2 AND the ledger
    has issued no step above 0 (issued_step <= 0: no mag yet, or only the kit's step 0). It does that BEFORE it
    decides the step, so the first ASK of a practice run from k >= 5 already gets the Shell row of its start step.
  - Otherwise it logs and ignores the lean: lean 0 (keeps the stored choice), a value above 2, or any lean once a
    step above 0 is issued. The column of a run never changes after the first evolve.
  - The column follows the stored lean, not the bits: a resumed run (no menu, lean 0 in the word) keeps its column
    from the ledger, which travels in the savestate file ("Lean"). An old savestate without "Lean" loads as 0.
  - A refused ASK/EXAM (any refusal) stores nothing; the next ASK/EXAM carries the lean again.
  - The lean only chooses the row: result, old type and r41 are as without a lean. A lean never creates or deletes
    an item.
  - A newserv from before F10 ignores bits 8-15: the run gets the default column.
  - sub 5 LEAN (low byte = lean) stays on the server with the same rule; no quest sends it.

Quest side: put the lean in EVERY ASK and EXAM word of the run (leader: the kit ASK and later; follower: the ASK of
mirror phase 5 and later). Only the words before the first evolve count; later ones are ignored and cost nothing.
```

## Decisions (route: self, class 2, covered by the dispatch and the frozen contract)

These are proposed `B-E01-<n>` entries; the regie numbers them when it takes them into decisions.md. That file is not
in my writeSet.

1. **Lean only on an unrefused ASK/EXAM.**
   - Date: 2026-10-27. Route: self. Type: technical. Class: 2.
   - Coverage: contract point 1 ("slaat de lean op ... en beslist pas daarna"); a refusal decides nothing.
   - Decision: a refused 'Z' stores no lean and logs "not stored (refused)".
   - Why: a refusal (BASE_RETURNED, RUN_AGAIN, ...) means this is not the live run copy, and the next ASK/EXAM
     carries the bits again.
2. **"(Shell)" in the evolve text follows the column that made the mag, not `lean == 2` alone.**
   - Date: 2026-10-27. Route: self. Type: technical. Class: 2.
   - Coverage: contract point 8 (the text names the choice) together with point 7 (fallback to Steps).
   - Decision: a ledger field `offered_shell`, set at the answer and kept in memory only.
   - Why: with an old json, lean 2 gets a Fang mag; "(Shell)" would then be false. The cost is one bool.
3. **Lean 0 never overwrites a stored lean; 1 after 2 does (before step 1).**
   - Date: 2026-10-27. Route: self. Type: technical. Class: 2.
   - Coverage: contract point 1 ("als ze ≠ 0 zijn") and randgeval 8 (a second choice before step > 0 is accepted).
4. **ShellSteps length checked in the parser and in `spire_mag_column`.**
   - Date: 2026-10-27. Route: self. Type: technical. Class: 2.
   - Coverage: contract point 7 and critic N6 (`expected_step` uses `steps.size()`).
   - Why: a line built by hand must not slip through either. Both checks are killed separately (M09, M10).
5. **`spire_run_mag_handle` gets `lean_bits` as a required parameter; `evolve_text` gets `shell = false` as a
   default.**
   - Date: 2026-10-27. Route: self. Type: technical. Class: 2.
   - Coverage: dispatch item 1 / item 4 ("Fang/Mind ongewijzigd").
   - Why: a required parameter means no caller can drop the lean silently. The default keeps the Fang/Mind text and
     its existing test unchanged.
6. **The fallback warning goes out on every ASK/EXAM whose ledger lean is 2 and whose line has no usable ShellSteps,**
   including one without a new mag.
   - Date: 2026-10-27. Route: self. Type: technical. Class: 2.
   - Coverage: contract point 7 ("één logregel per ASK/EXAM die terugvalt").

## Observations (not built)

- **Pre-existing:** in the unrefused 'Z' path, `spire_run_mag_ledger_for(c, run->run_id, true)` can return nullptr
  (no serial or no character: an empty key), and `*led` is used without a check (SpireFlagBridge.cc:2398-2411 at
  9ee74be). This was already so at f27d4f8. Under refusal NONE an overlay and a character are present, so the risk is
  low; the cost is one check that answers result 0.
- **Not killed by a test:** the `offered_shell` reset in `follow_mirror` (see Red and green). It is hygiene, without
  a behaviour effect.
- **The worktree has no Integratiebranch/Uitrol.** The git-flow hook may report this, as it did for phase S; that is
  outside the writeSet.

## Expansion requests

None.

## Fixronde (2026-10-27, F10S critic N1-N6 and S critic N6)

Base b77fb64. Code commit **48e2d3c** (`fix: run id 0 savestate refused, no-ledger 'Z' refusal, Shell texts`). Line
numbers are those of 48e2d3c. Sources: `output/routeb2-F10S-critic.md` (SHIP, N1-N6) and `output/routeb2-S-critic.md`
N6.

### Per point

1. **N1 (null deref, already on f27d4f8): fixed.**
   - SpireFlagBridge.cc:571-573: `spire_savestate_from_json` throws "savestate has run id 0". `load_logged` catches
     that and logs "unreadable file ...; ignored", so no run is built from it and no ledger either. The run end
     leaves such a file alone (:925-929); before the fix it was skipped as well (no run has id 0).
   - :2402-2414: in the 'Z' path, `led` is now fetched before the work. `!led`, `!overlay` or `!line` gives the new
     refusal `NO_LEDGER` (SpireMag.hh:161, name SpireMag.cc:275). That answers result 0, old FF, and the log carries
     "FAIL (NO_LEDGER); run X: no run mag ledger (run id 0 or no character key)".
   - I checked `overlay` and `line`: under NONE they cannot be null. `has_overlay = !!overlay` and
     `has_table = !!line` come before the refusal. I check them anyway, in the same branch, at no cost.
   - Test StaticTests.cc:4402-4407 (`expect_raises` on a savestate with run id 0) and :4464 (the name).
   - **Red before green:** before the fix it failed with
     `StaticTests.cc:4407: expected exception, but none raised` (log `NSFIX/red2.test.log`).
2. **N2 (floors in the Shell column): fixed.** New case "a Shell run with a start bonus starts on the start floor"
   (StaticTests.cc:4584-4598) checks three things:
   - step 0 with bonus 14 is the same for both columns;
   - a kit ASK with lean 2 and bonus 14 gives Varuna L22 (Id 1, DEF 11, POW 10);
   - Shell step 1 with bonus 14 is DEF 18, POW 10 (the floor wins over the row's 7), MIND 1, Id 1.

   The critic's mutant S1 (copy `steps` only) now dies; see X01.
3. **N3 (step 0 and the swap for RA/FO): fixed.** StaticTests.cc:4117-4128, in the json case, for HU, RA and FO:
   - Shell step 0 equals Steps step 0;
   - from step 1 on, Shell DEF = the Steps main stat (POW, or MIND for FO), Shell main stat = Steps DEF, and the other
     two stats are equal.

   This puts the data claim in the repo instead of in my script.
4. **N4 (reminder at the first mag): fixed.**
   - Text: "Your mag (Shell) is in your pack: equip it (Item Pack > Mag)" (SpireMag.hh:241, 60 characters), used via
     `spire_run_mag_reminder_text(offered_shell)` (SpireFlagBridge.cc:2344).
   - When it appears: on every first mag of a Shell run. That is the kit too, not only a practice run (decision F1
     below).
   - Length, measured: the longest run-mag ticker line is "Your mag evolves: Kabanda (Shell). Equip it: Item Pack >
     Mag." = 61 characters (`SPIRE_RUN_MAG_TICKER_MAX`, SpireMag.hh:245).
   - The test computes the evolve text (with and without Shell) for every Id of the shipped table (Steps, ShellSteps,
     StartFloors, names from names-v3) and both reminder texts. The longest of all of them equals 61
     (StaticTests.cc:4750-4775).
   - The NO_MAG reminder at the boss start is unchanged (not asked, and it says nothing about the lean).
5. **N6 (lean from the savestate): fixed.**
   - `spire_run_mag_saved_lean` (SpireMag.cc:312) keeps 0-2 and turns anything else into 0.
   - SpireFlagBridge.cc:606-611 logs `player_data_log` "Spire savestate: run mag ledger of run X has lean N; read as
     0 (no choice)".
   - Test StaticTests.cc:4386-4401: "2" stays 2; "258" (as a byte that would be 2), "3" and "-1" become 0.
   - **Red before green:** before the fix it failed with `StaticTests.cc:4399: SPIRE_RUN_MAG_LEAN_NONE !=
     with_lean("258")` (log `NSFIX/red.test.log`).
6. **N5 (proof of the wiring after the rollout): not code.** After `apply-on-pc.sh`, read these two lines in the
   newserv log on the pc. The text is literal from SpireFlagBridge.cc:2439-2442 and :2455-2458 (ASK) and :2345-2347
   (RESULT); `<...>` differs per run.
   - The kit ASK of a Shell run from room 1 (HU, no bought levels):
     ```
     Spire run mag: ASK room 1 for serial <serial> in q<920|921>: NEW MAG (NONE); run <run id>, start step 0, boss rooms 0, issued step 0, expected step 0, start bonus 0, 0 mag(s) in the run copy, lean bits 2 stored, lean 2 (Shell column); new mag 02000500F401000000000000; r41=5A00FF01
     ```
     The proof is the substring `lean bits 2 stored, lean 2 (Shell column)`. If it says `lean bits 0 none, lean 0
     (Steps)`, the bits did not arrive: the quest half, or the critic's S4.
     Grep: `grep -a "lean bits 2 stored, lean 2 (Shell column)"`.
   - After the RESULT of that kit mag, the ticker is the Shell reminder, and the log carries
     `RESULT 1 for serial <serial> in q<n>: step 0 confirmed, Mag (Shell) 02000500F401000000000000 (run <run id>)`.
   - After the Dragon (HU Shell step 1 is DEF 18 / POW 7):
     `RESULT 1 for serial <serial> in q<n>: step 1 confirmed, Varuna (Shell) 020119000807BC0200000000 (run <run id>)`.
     I computed the bytes with a script, the same way as `spire_mag_row_bytes`. Only the substring `Varuna (Shell)`
     counts as proof.
7. **S critic N6: fixed.** ROUTEB2-S-REPORT.md:114 now reads 2302-2306 versus 1456 at ab3c819, and 2310-2314 versus
   1464 at 48e2d3c. I measured both with `git show <sha>:src/SpireFlagBridge.cc | grep -n "sub ==
   SPIRE_RUN_MAG_NO_MAG"`. 2288-2292 was the handler's head.

### Red and green

**Green on the committed tree:** script `NSFIX/mut/run.sh`, which restores everything first; `git status` afterwards
shows only the two doc files changed.
```
final build exit 0
final tests exit 0: I 2635377 2026-10-27 02:18:55 - -- All static tests passed
```
The build log has 0 warning lines, there are 66 `--` sections, and the new case is on line 177.

**Mutations:** every one built with exit 0 and tested with exit 134.

| # | Mutation | Failing line (literal) |
|---|---|---|
| X01 | `spire_mag_column` copies steps and shell_steps only, not floors (critic S1) | `StaticTests.cc:4586: !(spire_mag_step_bytes(hu_shell, 0, 14, std::nullopt) == spire_mag_step_bytes(hu, 0, 14, std::nullopt))` |
| X02 | json: RA Shell step 0 DEF 5 -> 6 | `StaticTests.cc:4119: !(spire_mag_row_bytes(full.shell_steps[0]) == spire_mag_row_bytes(full.steps[0]))` |
| X03 | json: FO Shell step 2 DEX 1 -> 0 | `StaticTests.cc:4127: f.dex != h.dex` |
| X04 | json: HU Shell step 1 POW 7 -> 8 | `StaticTests.cc:4125: f.def != fo ? h.mind : h.pow` |
| X05 | reminder without "(Shell)" | `StaticTests.cc:4751: std::string("Your mag (Shell) is in your pack: equip it (Item Pack > Mag)") != spire_run_mag_reminder_text(true)` |
| X06 | saved lean not limited | `StaticTests.cc:4399: SPIRE_RUN_MAG_LEAN_NONE != with_lean("258")` |
| X07 | run id 0 accepted | `StaticTests.cc:4407: expected exception, but none raised` |
| X08 | saved lean without the lower bound | `StaticTests.cc:4401: SPIRE_RUN_MAG_LEAN_NONE != with_lean("-1")` |

**Not reachable by a static test:** the `NO_LEDGER` branch in the bridge, and the reminder call in RESULT. N5 above is
the proof for that.

### Decisions (route: self, class 2)

- **F1. The Shell reminder at every first mag of a Shell run, the kit included.**
  - Coverage: the coordinator's point 4 and contract point 8 ("de tekst noemt de keuze").
  - Why: at the kit the player has just chosen. The text is the same single ticker line as before plus 8 characters,
    so it is not an extra ticker (design critic N10 stays satisfied).
- **F2. A separate refusal `NO_LEDGER` instead of reusing `MARKER`.**
  - Coverage: point 1 ("als weigering antwoordt ... met een logregel").
  - Why: the log names the real cause; the answer is the same refusal answer.
- **F3. `SPIRE_RUN_MAG_TICKER_MAX = 61` as a constant with a test, not a cut in `spire_ticker_line`.**
  - Coverage: point 4 ("houd elke tickerregel ≤ de langste").
  - Why: a cut would break the text; the test catches a longer name in the json.
- **F4. The N3 swap test compares the stat values directly, not the Id's.**
  - Coverage: contract point 6 and design critic N2 ("geen spiegel van Sega's regels").
  - Why: the Id's are tested elsewhere (names-v3, a new Id per step).

### Observations

None new. The critic's S2 (`offered_shell` without the `if`) and S3 (the reset in `follow_mirror`) still survive.
Neither has a behaviour effect (the critic agrees), and both are left as they are.
