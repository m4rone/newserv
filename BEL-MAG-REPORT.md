# BEL-MAG: Run II mag grows at every Episode 2 boss (B-BEL-4)

Worktree /home/m4rck/code/worktrees/newserv/bel-mag, branch spike/bel-mag, base 695844f. Not pushed.

## Verdict

DONE. system/spire-mag.json now has 9 rows (steps 0-8) per class in Steps and in ShellSteps. Rows 0-4 are unchanged:
the diff touches only the trailing comma of row 4, the `_what` texts and one sentence of `_comment`. Run II starts on
k16 at step 4 and gains a step after k18, k21, k24 and k27. The code (src/SpireMag.cc, src/SpireMag.hh) does not fix
the number of steps, so it is unchanged. Only the static tests assumed five steps.

## The finding, measured again on 695844f

- src/SpireMag.cc:158-164: `spire_mag_expected_step` = min(start_step + bosses (+1 for a new EXAM), num_steps - 1).
  num_steps comes from `line.steps.size()`, i.e. from the data.
- src/SpireMag.cc:166-168: `spire_mag_start_step` counts the boss rooms below k. For k16 that is 4 (rooms 4, 8, 11, 15).
- Old data: five rows (0-4) per class and column, BossRooms [4, 8, 11, 15, 18, 21, 24, 27]. Run II at k16 therefore
  started on step 4, the last row, and never grew.
- Places that assumed five steps: only src/StaticTests.cc (`expect_eq(5u, line.steps.size())`, and the case "a boss after
  Dark Falz adds nothing"). The loader (SpireMag.cc:32-88) takes any non-empty number of rows. ShellSteps must be the
  same length as Steps (SpireMag.cc:69), and the new data meets that.

## The Sega rule used (newserv source)

- src/Items.cc:410-411: from level 50 up, Sega re-evolves a mag at every level that is a multiple of 5, as long as its
  evolution_number is below 4. So each step is a Sega re-evolution at L75, L85, L90 and L100 (steps 0-4 were already at
  L5, 25, 40, 55, 70).
- src/ItemData.cc:307-328: the strength flags (0x008 POW highest, 0x010 DEX highest, 0x020 MIND highest, 0x100 a tie).
- src/Items.cc:465-467, hunter, POW highest: odd section ID gives Apsaras 0x08 (DEX < MIND) or Kama 0x06; even gives
  Bhirava 0x0C (DEX < MIND) or Varaha 0x05.
- src/Items.cc:476-477, ranger, POW highest: odd gives Kaitabha 0x0A (DEX < MIND) or Madhu 0x26; even gives
  Bhirava 0x0C or Kama 0x06.
- src/Items.cc:482-487, force, MIND highest: with DEF < 45, odd gives Ila 0x17 (POW < DEX) or Kumara 0x09, and even gives
  Kabanda 0x1E or Naga 0x1C. With DEF >= 45 it gives Bana 0x24. Items.cc:488-493: POW highest with DEF >= 45 gives
  Andhaka 0x23.
- src/Items.cc:413-447: Sega's 4th evolution (the L100 rare mags Deva, Rati, Savitri, Pushan, Rukmin, Diwari, Sato,
  Bhima, Nidra) needs two stats that sum to the other two, so 50/50 at L100. A mag whose main stat is above 50 cannot
  get there, so none of these is used. This is written in the HU `_what`.
- Every Id is a valid mag in names-v3.json (the "every row" test checks `02XX00` for every row). Names:
  system/tables/names-v3.json:1058 Varaha 0205, 1059 Kama 0206, 1061 Apsaras 0208, 1062 Kumara 0209, 1065 Bhirava 020C,
  1076 Ila 0217, 1081 Naga 021C, 1083 Kabanda 021E, 1088 Andhaka 0223, 1089 Bana 0224, 1091 Madhu 0226.

I checked each new row against the rule with a script that copies the branches above (both section-ID parities). Its
output, verbatim:

```
HU Steps 5 12 L75 +1/3/0/1 sega [8, 12] OK
HU Steps 6 5 L85 +1/5/3/1 sega [5, 6] OK
HU Steps 7 8 L90 +0/3/0/2 sega [8, 12] OK
HU Steps 8 6 L100 +1/5/3/1 sega [5, 6] OK
HU ShellSteps 5..8: 12, 5, 8, 6 OK (Sega's HU rule reads no DEF)
RA Steps 5 6 L75 +1/3/1/0 sega [6, 38] OK
RA Steps 6 12 L85 +1/5/0/4 sega [10, 12] OK
RA Steps 7 38 L90 +1/3/1/0 sega [6, 38] OK
RA Steps 8 12 L100 +1/5/1/3 sega [10, 12] OK
RA ShellSteps 5..8: 6, 12, 38, 12 OK
FO Steps 5 9 L75 +1/1/0/3 sega [9, 28] OK
FO Steps 6 30 L85 +1/1/3/5 sega [23, 30] OK
FO Steps 7 28 L90 +0/2/0/3 sega [9, 28] OK
FO Steps 8 23 L100 +1/1/3/5 sega [23, 30] OK
FO ShellSteps 5 35 L75 +3/1/0/1 sega [36] NOT-SEGA
FO ShellSteps 6 36 L85 +5/1/3/1 sega [36] OK
FO ShellSteps 7 35 L90 +3/2/0/0 sega [36] NOT-SEGA
FO ShellSteps 8 36 L100 +5/1/3/1 sega [36] OK
```
(gains are DEF/POW/DEX/MIND compared with the step before; I shortened the Shell lines for HU and RA, every one of
them printed OK)

## Steps 4-8 per class and column (stats DEF/POW/DEX/MIND, level = sum)

| class/column | step 4 (Run I end, unchanged) | 5 after Barba Ray k18 | 6 after Gol Dragon k21 | 7 after Gal Gryphon k24 | 8 after Olga Flow k27 |
|---|---|---|---|---|---|
| HU Fang (Steps) | 6 Kama 14/46/5/5 L70 | 12 Bhirava 15/49/5/6 L75 | 5 Varaha 16/54/8/7 L85 | 8 Apsaras 16/57/8/9 L90 | 6 Kama 17/62/11/10 L100 |
| HU Shell | 6 Kama 46/14/5/5 | 12 Bhirava 49/15/5/6 | 5 Varaha 54/16/8/7 | 8 Apsaras 57/16/8/9 | 6 Kama 62/17/11/10 |
| RA Fang (Steps) | 38 Madhu 12/46/7/5 L70 | 6 Kama 13/49/8/5 L75 | 12 Bhirava 14/54/8/9 L85 | 38 Madhu 15/57/9/9 L90 | 12 Bhirava 16/62/10/12 L100 |
| RA Shell | 38 Madhu 46/12/7/5 | 6 Kama 49/13/8/5 | 12 Bhirava 54/14/8/9 | 38 Madhu 57/15/9/9 | 12 Bhirava 62/16/10/12 |
| FO Mind (Steps) | 28 Naga 14/5/5/46 L70 | 9 Kumara 15/6/5/49 L75 | 30 Kabanda 16/7/8/54 L85 | 28 Naga 16/9/8/57 L90 | 23 Ila 17/10/11/62 L100 |
| FO Shell | 36 Bana 46/5/5/14 | 35 Andhaka 49/6/5/15 | 36 Bana 54/7/8/16 | 35 Andhaka 57/9/8/16 | 36 Bana 62/10/11/17 |

Rules the rows follow, all checked by the static tests or the script: no stat ever goes down; the main stat has the
largest gain at every step; each step adds +5 or +10 levels (step 3 to 4 added +15); step 8 ends at level 100; the Id
changes at every step; the evolve text is at most 61 characters (the longest name is 7 characters, the same as Kabanda,
so SPIRE_RUN_MAG_TICKER_MAX stays 61). The Shell column is Steps with DEF and the main stat swapped, as the existing
test (StaticTests.cc:4190-4198) requires.

## Decisions (for decisions.md; that file is outside my writeSet, so the orchestrator should copy them in)

### B-E-BELMAG-1: L75/85/90/100, alternating within Sega's L50+ group
- **Date:** 2026-10-27 22:25
- **Route:** zelf (self)
- **Type:** technical
- **Class:** reversible
- **Coverage:** B-BEL-4 (decisions.md:1714 "de run-mag krijgt stappen 5-8"); dispatch: Sega mags, monotone stats, jumps no
  larger than 3→4, end around L100
- **Decision:** steps 5-8 sit at L75, 85, 90 and 100, multiples of 5 because that is when Sega re-evolves
  (Items.cc:410-411). Within the class's L50+ group the Id alternates by DEX against MIND (for FO: POW against DEX).
- **Why:** above L50 there are at most four mags per class and column. Alternating is the only way Sega's own rule
  gives a visible evolve at every step, and the L100 rare mags cannot be reached with the main stat above 50.

### B-E-BELMAG-2: FO Shell alternates Andhaka/Bana
- **Date:** 2026-10-27 22:25
- **Route:** zelf (self)
- **Type:** technical
- **Class:** reversible
- **Coverage:** spire-mag.json `_comment` "The Id's are hand-derived along Sega's rules ... not checked against them" plus
  the existing test rule "a visible evolve at every step" (StaticTests.cc, every row case)
- **Decision:** FO Shell steps 5-8 are Andhaka 35, Bana 36, Andhaka 35, Bana 36.
- **Why:** with DEF >= 45, Sega gives a force only Bana or Andhaka (Items.cc:481-500). Sega would keep Bana, because MIND
  stays the highest of POW/DEX/MIND, and that breaks the evolve-every-step rule. Andhaka is Sega's other mag for a
  force with high DEF. Steps 5 and 7 are not what Sega's rule would produce at these stats (the script prints
  NOT-SEGA for them).

### B-E-BELMAG-3: Kaitabha left out for RA
- **Date:** 2026-10-27 22:25
- **Route:** zelf (self)
- **Type:** technical
- **Class:** reversible
- **Coverage:** SpireMag.hh:274-276 (SPIRE_RUN_MAG_TICKER_MAX 61, measured) and the ticker test
- **Decision:** RA uses Kama, Bhirava and Madhu, but not Kaitabha 0x0A.
- **Why:** "Your mag evolves: Kaitabha (Shell). Equip it: Item Pack > Mag." is 62 characters, above the measured maximum.

## Changes

- system/spire-mag.json: rows 5-8 in Steps and ShellSteps for HU, RA and FO (lines 13-16, 24-27, 42-45, 53-56,
  71-74, 82-85). The `_what` of each class names the Id's and the reason; one `_comment` sentence now says that Steps
  holds the kit plus one step per boss room.
- src/StaticTests.cc:4106-4183: new case "B-BEL-4: Run II grows at every Episode 2 boss, Run I unchanged (shipped
  spire-mag.json)". It loads the real system/spire-mag.json, as the other F6 cases do, and runs per class and per
  column (Fang/Mind and Shell):
  - Run I from k1: kit step 0, then after k4, k8, k11 and k15 steps 1-4. Step 4 still has the old Id (Kama, Madhu,
    Naga, Shell Bana) and L70.
  - Run II: the first ASK at k16 gives step 4, then after k18, k21, k24 and k27 steps 5, 6, 7 and 8. The same EXAM k27
    again gives nothing (the cap).
  - A single room k19 starts on step 5.
  - Run I carried on into Episode 2 reaches step 8.
- src/StaticTests.cc:4201: `expect_eq(5u, ...)` is now `expect_eq(table.boss_rooms.size() + 1, line.steps.size())`, so the
  count comes from the data.
- src/StaticTests.cc:4325-4335: the old case "a boss after Dark Falz adds nothing" asserted the very behaviour that
  B-BEL-4 changes. It now checks that EXAM k18 after four bosses gives step 5, and that with eight bosses and step 8
  issued, EXAM k27 gives nothing (expected step 8).
- src/SpireMag.cc and src/SpireMag.hh: unchanged (nothing in them fixes the number of steps).

## Verification (literal)

Build (in the worktree; the dispatched command `cmake --build build -j8 newserv` is rejected by cmake 4.4.4 with
"Unknown argument newserv", so I used `--target newserv`):
```
nohup sh -c 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8 --target newserv; echo $? > build.log.exit' > build.log 2>&1 &
build.log.exit: 0     tail build.log: [100%] Linking CXX executable newserv / [100%] Built target newserv
```

Red, with the new tests on the OLD data (`git diff --quiet system/spire-mag.json` reported "data unchanged (old)"):
```
I ... -    F6 (B-BEL-4: Run II grows at every Episode 2 boss, Run I unchanged (shipped spire-mag.json))
terminate called after throwing an instance of 'phosg::expectation_failed'
  what():  failure at /home/m4rck/code/worktrees/newserv/bel-mag/src/StaticTests.cc:4131: !(e.decision.result)
exit=134
```
StaticTests.cc:4131 is `expect(e.decision.result)` in `run_bosses`. The Run I Episode 1 part (k4-k15) only needs rows
0-4, which the old data has. The first failing call is therefore Run II at k18: expected step min(4+1, 4) = 4, already
issued, so no result.

Green, NEW data, same binary:
```
./build/newserv --config=tests/config.json run-static-tests > st.log 2>&1; echo $?
exit=0
I ... -    F6 (B-BEL-4: Run II grows at every Episode 2 boss, Run I unchanged (shipped spire-mag.json))
I ... -    F6 (every row of the shipped spire-mag.json)
I ... -    F6 (idempotence: EXAM twice, ASK after a resume)
I ... -    F6 (F10: the evolve text names Shell)
I ... - -- All static tests passed
```
The W lines in st.log (Spire guard unreadable files, lean 258/3/-1) are existing negative cases; none of them is new.

## Quest repo: check_mag_swap.py

`python3 /home/m4rck/code/worktrees/pso-roguelike/beloningen/quests/spire/check_mag_swap.py --help` exits 1 with
`FileNotFoundError: [Errno 2] No such file or directory: '--help'`, because the script has no --help and reads every
argument as a source file. The script does not read spire-mag.json at all: there is no json or Steps code in it. It
checks only the swap block in the quest source. In quests/spire, the only readers of the table are pc-build.sh:80
(`git show "$NEWSERV:system/spire-mag.json"`) and apply-on-pc.sh:91,175 (a non-empty check and a cp). Neither assumes a
number of steps, so I changed nothing in the quest repo.

## Notes

- The worktree's newserv repo has no git flow recorded (the werkwijze hook warned "integratiebranch: ontbreekt"). I did
  not merge or push. Merge and deploy go through the orchestrator (pc-build.sh then picks up the new commit).
- Not covered here: auto-equipping the mag (bundle 9), choosing the mag, feeding, and client behaviour on the Wii (what
  the player sees after Barba Ray is a measurement point for Marck's run, per B-BEL-4).
- build/, build.log, build.log.exit, st.log and st-old.log are untracked and not committed.
