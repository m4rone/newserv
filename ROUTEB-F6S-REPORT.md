# Route B F6s: the run mag, server half (newserv)

Branch spike/route-b, base f332e1c. Code commit 4891acc. Contract with the quest: pso-roguelike
`quests/spire/ROUTEB-F6-PROTOCOL.md` at quest commit 18ba60c (TAG `0x5A000000`).

## Premises that did not hold (read first)

1. **The brief answers every sub; the protocol does not.** The brief says "subs 1-4: answer r20-r31 + r41, then
   DONE". The protocol says RESULT and NO_MAG get no r41 ("the server should answer neither in r41"), and only ASK
   and EXAM are awaited. I followed the protocol: RESULT, NO_MAG and LEAN get no answer at all (no r41, no DONE).
2. **issued_step moves at RESULT 1, not at the answer** (protocol, "Requirements on the server"). The plan's rule
   "answer only when issued_step < expected" still holds. A consequence: two EXAMs of k8 with no RESULT in between
   both answer the same step 2 mag. That is by design: a lost answer stays owed. The quest creates once per answer
   and sends RESULT in the same stretch. Only RESULT 1 issues the step, so the ledger issues it once.
3. **The feed mirror hook (`on_feed_mag`) is outside the writeSet.** It sits in Items.cc / ReceiveSubcommands.cc. The
   feed mirror writes into the overlay, so the ledger reads the mirror's result from the overlay at every 'Z' and
   every 'V' (F6S-6). That gives the same data with no hook.
4. **Amendment 3 needs on_61_98 (ReceiveCommands.cc), which is outside the writeSet.** The overlay is marked
   "refreshed by 61" after each of the 7 places where SpireFlagBridge.cc itself asks for a 61. One in-game 61 path is
   not covered: ChatCommands.cc:489. I filed an expansion request for the one-line hook (see Decisions).
5. **The boss room start.** The server has no room-start signal of its own, but the 'V' savestate write comes at
   every room start after the first room, and it carries k. "Your mag shields you" is sent from there for k >= 8 in
   `BossRooms`, so no new quest signal is needed. Not covered: a quest whose first room is the boss room (a resume
   or practice start). Its first room writes no 'V', so it gets no shield text.

## Coverage claim

Covers: the server side of the run mag. That is the step, the bytes, the merge, the refusals, idempotence, the
ledger, the savestate and the texts.
Does NOT cover: the quest's swap code, whether the client accepts and equips a created mag under FORBID_ALL (first
play decides), or balance.

## What changed

- `src/SpireMag.hh/.cc` (new, pure):
  - `spire_mag_parse`, which is strict.
  - `spire_mag_archetype`, `spire_mag_row_bytes` (built as assign_mag_stats does it) and `spire_mag_step_bytes` (the
    step, merged per stat with the start floor and with the ledger's last_mirrored; LL recomputed; at most 200).
  - `spire_mag_expected_step`, `spire_mag_start_step`, `spire_run_mag_refusal` (12 cases) and `spire_run_mag_decide`
    (ASK/EXAM, idempotent).
  - `spire_run_mag_result`, `spire_run_mag_follow_mirror`, the hex helpers and the texts.
- `src/SpireFlagBridge.cc/.hh`:
  - The 'Z' handler `spire_run_mag_handle`, wired in front of the message-text fallthrough.
  - The ledger store per (serial, character id, run id), persisted in the savestate JSON as `RunMag`.
  - A resume adopts the savestate's ledger. After a server restart, the ledger is read back from the savestate file.
  - `spire_savestate_items` now takes the ledger bytes. It keeps one type-02 item with exactly those 12 bytes and
    drops every other type-02 item and the Scape Doll.
  - The resume text is now "Your Spire Mag comes back: equip it.", and it shows only when the savestate brought a
    mag back.
  - The F1 hook `spire_guard_ledger_run_mag` returns the latest run's last_mirrored.
  - The 'I' mark per game, and the in-game-61 mark (`spire_run_mag_note_in_game_61`). Quest load clears both.
  - The kit text after the 'M' START answer, with a 3 s delay, when hp_mat or atp_grind > 0.
  - The shield text at 'V'.
- `src/StaticTests.cc`: a new section "Spire run mag" (9 cases), plus an F6 case in the savestate section. The
  existing savestate and resume-text assertions were moved to the new behaviour.
- `system/spire-mag.json` (new): `Enabled true`, HU/RA/FO lines of 5 steps, start floors and `BossRooms`.
- `CMakeLists.txt`: adds `src/SpireMag.cc`.

## Tests: failing first, then passing

**Red build.** The final tests ran against a stub that stood for f332e1c:
- SpireMag functions return empty answers (parse gives empty lines).
- The f332e1c savestate filter drops every type-02 item.
- The old resume text is back.
- The JSON has no `RunMag`.
- A temporary try/catch per case, so every case reports.

Neither the stub nor the try/catch is committed (the green sources were restored from a scratch copy; I checked that
`/usr/bin/grep -ac "try { fn" src/StaticTests.cc` gives 0). The red run was `./build/newserv run-static-tests`, exit
0 because the scaffold caught the failures:

```
E    F6 (every row of the shipped spire-mag.json) FAILED: failure at src/StaticTests.cc:3778: !(table.enabled)
E    F6 (HU start floor L22) FAILED: failure at src/StaticTests.cc:3835: !(l22 == spire_mag_step_bytes(hu, 0, 14, std::nullopt))
E    F6 (merge: a fed POW 23.50 against the step's 18.00) FAILED: failure at src/StaticTests.cc:3853: 2350 != word(b, 1)
E    F6 (idempotence: EXAM twice, ASK after a resume) FAILED: failure at src/StaticTests.cc:3871: !(d.result)
E    F6 (pack full: the step stays owed) FAILED: failure at src/StaticTests.cc:3921: !(e.result)
E    F6 (the ledger follows the feed mirror) FAILED: failure at src/StaticTests.cc:3943: !(spire_run_mag_follow_mirror(led, {fed}))
E    F6 (refusals) FAILED: failure at src/StaticTests.cc:3965: R::NOT_GC_V3 != with([](auto& st) { st.gc_v3 = false; })
E    F6 (texts) FAILED: failure at src/StaticTests.cc:3991: std::string("Your mag evolves: Varuna. Equip it: Item Pack > Mag.") != spire_run_mag_evolve_text("Varuna")
E    F6 (hex round trip) FAILED: failure at src/StaticTests.cc:4002: std::string("020116004C04E80300006400") != spire_run_mag_bytes_hex(b)
E    F6 (savestate) FAILED: failure at src/StaticTests.cc:4056: 4u != kept.size()
E    F6 (resume text) FAILED: failure at src/StaticTests.cc:4275: std::string("Your Spire Mag comes back: equip it.") != spire_run_resume_mag_ticker(true, true)
```

The scaffold stops each case at its first failing assertion, so for "refusals" only NOT_GC_V3 is shown red. The stub
returns NONE for every state, so each of the other refusal expect_eq calls would fail the same way.

**Green build (4891acc).** `cmake --build build` gave no error or warning line. The full
`./build/newserv run-static-tests` exited 0. I read all 107 lines; none starts with anything but `I `, and none
reports a failure, warning or skip. The last line is `-- All static tests passed`. The F6 lines:

```
-- Spire run mag: step, bytes, merge, refusals, idempotence (F6, route B)
   F6 (every row of the shipped spire-mag.json)
   F6 (HU start floor L22)
   F6 (merge: a fed POW 23.50 against the step's 18.00)
   F6 (idempotence: EXAM twice, ASK after a resume)
   F6 (pack full: the step stays owed)
   F6 (the ledger follows the feed mirror)
   F6 (refusals)
   F6 (texts)
   F6 (hex round trip)
   F6 (savestate: the ledger's run mag kept, any other type-02 item and the Scape Doll dropped)
```

What each acceptance item checks:
- **Shipped json, every row** (steps and floors, HU/RA/FO):
  - The bytes give LL == the sum of the four levels, every stat word is even, and LL <= 200.
  - ZZ exists as `02ZZ00` in system/tables/names-v3.json.
  - Each step is monotone per stat, has a higher LL than the step before and a different ZZ.
  - The same rules hold for every step under start bonus 0/5/10/14.
- **HU start floor L22:** `step_bytes(HU, 0, bonus 14)` == `02 01 16 00 4C 04 E8 03 00 00 64 00`. Bonus 5 and 10
  give Varuna L13 (8/5/0/0). Bonus 0 gives Mag L5. Step 1 under L22 gives 11/18/0/1, LL 30, Varuna.
- **Merge:** a mirrored POW 23.50 against step POW 18.00 gives 2350 and LL 30 (recomputed), and the step's kind.
  When the merge would pass 200, the mirrored stats are kept.
- **Idempotence:**
  - The kit ASK answers step 0 and repeats it while there is no RESULT.
  - After RESULT 1, the next ASK gives nothing.
  - EXAM k8, RESULT 1, then EXAM k8 again: result 0 and issued_step stays 2.
  - A stray RESULT changes nothing.
  - The server's own boss clears {4, 8} change nothing.
  - An ASK after a resume (2 bosses stored, issued_step 2) gives result 0.
  - Step 4 is the cap.
  - A practice run from k9 starts at step 2.
- **Pack full:** RESULT 0 leaves issued_step and the bytes as they were, and the next ASK answers the same mag.
- **Refusals,** each its own expect_eq: NOT_GC_V3, NO_SPIRE_GAME, NO_OVERLAY, RESTORE_PENDING, MARKER, BASE_RETURNED,
  RUN_AGAIN (after 'I'), REFRESHED_BY_61, DISABLED (Enabled false, also with no table), NO_TABLE and TWO_MAGS. Also:
  - 0 mags gives NONE.
  - The q925 base game (no overlay, real mag live) gives NO_OVERLAY (amendment 8).
  - Answer values `0x5A000601` and `0x5A00FF00`.
- **Savestate:**
  - The ledgered HU L22 mag is kept (4 items; the Glass index moves to 2).
  - The same kind with other bytes is dropped.
  - Two exact copies come back as one.
  - The Scape Doll is dropped. With no ledger, there is no mag.
  - The ledger round-trips through the savestate JSON.
  - The resume line shows only with a mag.
- **Restore:** a 61 that still holds the run mag in the real mag's (Pitri) place gives `push_verified` false. The
  real inventory gives true. F1 (f) still covers the hard-stop case.

## spire-mag.json for the lab and pc copies

The file is newserv `system/spire-mag.json` at 4891acc. It must be copied next to spire-shop.json in each newserv
`system/` that runs this build: the lab, and the pc through apply-on-pc.sh. I did not copy it anywhere. Without the
file, every 'Z' answers result 0 (NO_TABLE), and the log says why.

| Class | Step 0 kit | 1 Dragon | 2 De Rol Le | 3 Vol Opt | 4 Dark Falz | Floor +5 | Floor +14 |
|---|---|---|---|---|---|---|---|
| HU | Mag 5/0/0/0 | Varuna 7/18/0/0 | Rudra 9/30/1/0 | Varaha 12/42/1/0 | Kama 14/46/5/5 | Varuna 8/5/0/0 | Varuna 11/10/0/1 |
| RA | Mag 5/0/0/0 | Kalki 6/16/3/0 | Surya 8/27/5/0 | Kama 10/38/7/0 | Madhu 12/46/7/5 | Kalki 5/2/5/0 | Kalki 6/4/8/2 |
| FO | Mag 5/0/0/0 | Vritra 7/0/0/18 | Namuci 9/0/1/30 | Kabanda 12/0/1/42 | Naga 14/5/5/46 | Vritra 8/1/0/4 | Vritra 10/4/1/7 |

BossRooms: 4, 8, 11, 15, 18, 21, 24, 27. Kill switch: `"Enabled": false`, read at every 'Z', no restart.

## Technical decisions (F6S-n)

- F6S-1: RESULT, NO_MAG and LEAN get no answer, and only ASK and EXAM are answered (protocol over brief, premise 1).
  A refusal answers result 0 with old type FF, so the quest does not wait out its 10 s.
- F6S-2: issued_step advances only at RESULT 1. The answer stores `offered` and `offered_step`. RESULT 0 leaves the
  step owed, and the EXAM's boss room stays counted (protocol).
- F6S-3: Boss rooms counted = the ledger's (EXAM k, persisted) united with the server's own boss clears for this
  client in this game (`spire_run_rooms`).
- F6S-4: start_step = the BossRooms before the room of the first ASK. A run from room 1 gets 0. A practice run or a
  resume without a ledger gets the mag of that point.
- F6S-5: The start floor is keyed by the start_level bonus this run copy carries (`spire_start_level_run`), not by
  the live level: MinBonus 5 gives L13 (Lv6/Lv11 starts) and MinBonus 14 gives L22 (Lv15). The kind is the floor's
  only while the floor's level is above the step's (step 0).
- F6S-6: The ledger follows the feed mirror by reading the overlay at every 'Z' and 'V'. It does so only with exactly
  one type-02 item, a mag already issued, and a level not below the ledger's. It never follows after an in-game 61.
- F6S-7: If the merge would pass LL 200, the mirrored stats stay (they are a valid mag) and the step's kind is used.
  Photon blasts are always 0 (data1[3]), because item_create2 cannot set data2.
- F6S-8: Kinds follow Sega's evolution rules (Items.cc player_feed_mag), with an even section ID where the rule
  splits on it. Two exceptions: HU step 4 Kama and RA step 4 Madhu are the odd-section results. With the plan's stats,
  the even-section result would repeat step 3's kind, and the test requires a new kind at every step.
- F6S-9: BossRooms is one flat list over the room counter (protocol: Ep2 k18-k27), not per episode.
- F6S-10: A resume adopts the savestate's ledger, or none. It does not keep a newer in-memory one, because the
  resumed items carry the savestate's mag. Otherwise a crash after an evolve would cost the evolve. After a server
  restart, the savestate file supplies the ledger.
- F6S-11: A 'Z' that comes before 'B' (the kit ASK runs in its own thread) sets the game's run the way 'B' does.
- F6S-12: The first mag (no step issued before) gets the reminder text, not "evolves". A later RESULT 1 gets
  "Your mag evolves: <name>. Equip it: Item Pack > Mag." The name comes from the GC V3 item name index (Sega's
  table); without it, "Mag ZZ".
- F6S-13: The 'I' mark is per game (any client's 'I' push refuses 'Z' for the whole game). It is set when the push
  begins, not only on OK, because a half-done push leaves the live state unknown.
- F6S-14: The kit text goes out 3 s after the 'M' START answer, so the kit items exist when it shows.
- F6S-15: LEAN only stores the value in the ledger (persisted). The bytes still use the default line; picking the
  column is F10.

## Decisions and observations

- Expansion request filed:
  `/home/m4rck/pso-roguelike/new-features/roguelike/STATE/expansion-route-b-F6s-1793055396401.json`. It asks for one
  line in on_61_98 (ReceiveCommands.cc): `if (msg.command == 0x61) spire_run_mag_note_in_game_61(c);`. That covers
  the in-game 61 from ChatCommands.cc:489 (premise 4).
- Observation (not built): the shield text does not show when a quest starts directly in a boss room (premise 5).
  A quest signal at room start would close that gap, and costs quest bytes.
- Observation (not built): a lean (Shell) column in spire-mag.json and its use belong to F10.

## Hard limits kept

- No deploy, no apply-on-pc.sh, no ssh and no newserv restart.
- No write to lab/ or ~/pso-tools/newserv/system/, and no memory card touched.
- Nothing written in the quest repo.
- In /home/m4rck/pso-roguelike, only the expansion-request STATE file, which is the protocol's own channel.
