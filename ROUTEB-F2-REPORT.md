# Route B F2: splinters per room plus pacts, and the F3/F8 loose ends (newserv side)

Branch spike/route-b, base df5d37b. Step A code commit a94a8c0, step B code commit 1bc0bc1.

## Premises checked first

1. **"Meta file first, records second" does not apply literally to a credit.** A room credit writes no meta.json. I read
   it as "the other file first, records.json last", as BUY does: the checkpoint file is written first, then
   records.json. A records.json write that fails is caught up at 'F' from the rooms of this game (F2-4).
2. **"Credit only when `rooms.emplace(k).second`" would lose a boss.** An arrival stores a room with `boss = false`. If
   a boss room was ever stored that way before its clear, the clear's `emplace` would return false and PerBoss would
   never be paid. The gate is therefore "new room or new boss room in this game". The durable check is per kind: k in
   `Rooms` pays PerRoom once, and k in `Bosses` pays PerBoss once (F2-2). Today no arrival floor is a boss floor, so
   this is a safety margin, not a live bug.
3. **The F3 handler path cannot be reached from StaticTests.** `spire_shop_handle` needs a live `Client`: its
   `character_file`, the records and meta files keyed by its character id, and `spire_send_register` over its channel.
   No static test constructs a Client (`/usr/bin/grep -a -n "make_shared<Client>" src/StaticTests.cc` finds no match).
   The evidence for the wiring is a source check. The decision itself is already covered by the F3 tests.

## Coverage claim

Covers: live kit_tier refusal, drop k from the drop's floor, the k==0 warning, server-side splinter credit/dedupe/payout and the F pacts decode.
Does NOT cover: the quest's pacts bits, records.json races under real concurrent BUYs on the pc, how the payout feels.

## Step A (commit a94a8c0)

- `src/SpireFlagBridge.cc` `spire_shop_handle`: `bool kit2 = spire_meta_kit2_owned(meta, char_class)` is computed once
  and passed to `spire_shop_buy_refusal(..., kit2)` (BUY) and `spire_shop_next_price(..., kit2)` (LIST/START prices).
  `char_class` moved up; it is the same value as before.
- `src/SpireDrops.hh/.cc`: `spire_drops_filter(c, l, item, is_boss, uint8_t floor)` takes k from `floor`, not from
  `c->floor`. The new pure helper `spire_drops_boss_k_warning(is_boss, k, episode, floor)` returns one line for a boss
  drop with k 0, and the filter logs it as `warning_f` with the player name.
- `src/ReceiveSubcommands.cc:3091`: `spire_drops_filter(c, l, res.item, is_boss, cmd.floor)`. This is the floor the
  item is added on (`l->add_item(cmd.floor, ...)`).
- `src/StaticTests.cc`: the M1 case (a box on floor 3 gives room 5, whatever the receiver's floor 0 would give) and the
  M2 block (an empty SpireCheckpointRooms gives k 0 on floor 0E, which gives one warning line naming the floor and the
  key; no warning for a non-boss drop or for a boss with k 15).

Failing, then passing:

```
# source checks before the change (HEAD df5d37b)
$ /usr/bin/grep -a -c "spire_meta_kit2_owned(meta, char_class)" src/SpireFlagBridge.cc      -> 0
$ /usr/bin/grep -a -n "spire_drops_filter(c, l, res.item, is_boss, cmd.floor)" src/ReceiveSubcommands.cc   -> exit=1
$ /usr/bin/grep -a -n "c->floor" src/SpireDrops.cc   -> 363: uint8_t k = spire_drops_floor_k(..., c->floor);
# after
-> 1 ; 3091: spire_drops_filter(c, l, res.item, is_boss, cmd.floor); ... ; c->floor exit=1

# M2 test with spire_drops_boss_k_warning stubbed to return "" (build exit 0)
$ ./build/newserv run-static-tests   -> exit 134
  what():  failure at .../src/StaticTests.cc:2736: !(warning.find("floor 0E") != std::string::npos)
# real body
$ ./build/newserv run-static-tests   -> exit 0, 87 lines, 61 sections, 0 lines that are not "I ", "All static tests passed"
```

## Step B (commit 1bc0bc1)

- `system/spire-shop.json`: a new `Payout` block. The `_comment` no longer says "earns 18 splinters".
- `src/SpireMeta.hh/.cc`:
  - `SpireShopPayout` and `SpireShopCatalogue::payout` (optional).
  - The strict parse throws on any of these: a block that is not a dict; a missing PerRoom, PerBoss or PactTenths; an
    unknown key that does not start with `_`; a value that is not an integer; PerRoom or PerBoss outside 0-1000;
    PactTenths outside 0-10.
  - `SpireMetaFinish::pacts = min((F >> 19) & 7, 4)`. Nothing else in the decode changed.
- `src/SpireFlagBridge.hh/.cc`:
  - `SpireRecords` moved to the header, so the static tests can reach it. `spire_records_load` and `spire_records_save`
    are no longer static, and they now also read and write `Credited`.
  - New functions: `spire_records_credit_room`, `spire_records_credit_room_file` and
    `spire_records_finish_splinters`.
  - `spire_checkpoint_store` reads the catalogue on every store. For each client, after its checkpoint file, it calls
    `spire_checkpoint_credit`: no credit without a 'B' in this game, and none for a practice run.
  - The 'F' path calls `spire_records_finish_splinters` with the decoded pacts and the game's run id. It logs the
    warning when the result carries one.
- `src/StaticTests.cc`:
  - The old fixed-18 block ("photon splinters (rooms + 5 per boss) * (1 + 0.1 per heat)") is replaced by the section
    "Spire records route B F2". The fallback formula is still checked inside it: 20 for 10 rooms and 2 bosses, and it
    never wraps.
  - New section "Spire shop route B F2" for the Payout parse.
  - The F round trip is added to "Spire meta: the flags a finished run sets".

records.json after a run (shape):
```json
"Credited": [{"Run": 286331153, "Rooms": [1,2,3,4,5,6,7,8,9,10], "Bosses": [4,8], "Base": 50, "Finished": true}]
```

### Acceptance, written first, failing on the step-A code, then green

The tests were written first. The header API was added with `.cc` stubs that kept the pre-F2 behaviour: no credit,
'F' always paid the old V, no Payout parse and no pacts decode. Each piece was then implemented in turn:

```
# stage 0: stubs (build exit 0)
  what():  failure at .../src/StaticTests.cc:553: 28u != credit_run(0x11111111, 6)                 exit 134
# stage 1: credit, dedupe, Credited in records.json, finish (build exit 0)
  what():  failure at .../src/StaticTests.cc:659: !(repo.payout.has_value())                        exit 134
# stage 2: strict Payout parse + Payout block in spire-shop.json (build exit 0)
  what():  failure at .../src/StaticTests.cc:2487: std::min<uint32_t>(pacts, 4) != static_cast<uint32_t>(d.pacts)   exit 134
# stage 3: pacts decode + live wiring in spire_checkpoint_store and 'F' (build exit 0)
$ ./build/newserv run-static-tests   -> exit 0
  88 output lines, 62 test sections, 0 lines that are not "I " (no W/E), last line "-- All static tests passed"
  -- Spire records route B F2: 3 per room and 10 per boss when the checkpoint is stored, once per run and k, pacts at 'F'
  -- Spire shop route B F2: the Payout block (PerRoom 3, PerBoss 10, PactTenths 1), parsed strictly
```

What the green sections assert, mapped to the acceptance criteria:

- **10 rooms + 2 bosses = 50; 4 pacts = 70.** Credited through the file function, k1-k10 with bosses at k4 and k8: 50.
  'F' with 4 pacts adds 20 (balance 70). With 2 pacts the result is 60; with 0 pacts 'F' adds 0.
- **The same k twice pays 3 once; a new run_id pays again; practice pays 0.** k3 and k4 (boss) a second time add 0
  after a reload from the file. Run 0x22222222 pays 3 for k3. A practice run pays 0, and so does run id 0.
- **k1-k6, boss k4, no 'F' = 28.** The balance and SplintersTotal in records.json are both 28. A resume with the same
  run id pays only k7-k10 (+22).
- **Full run plus 'F' with 4 pacts = exactly 70, not 120.** 'F' gets the game's rooms 1-10 and the old-V inputs
  (10, 2): `added` 20, `caught_up` 0, `legacy` false, balance 70. A second 'F' adds 0, and a late room after 'F' adds 0.
- **No Payout block = the old 20 plus a warning.** `finish(nullptr, …, 10, 2)` gives 20, `legacy` true and a non-empty
  warning. The shipped json with the block removed parses to `payout == nullopt`.
- **The F round trip.** It covers seconds {0, 0xFFFF} x exams 0-2 x deathless x won x pacts 0-7 (192 values). Bit 22 is
  0. won, exams, deathless and the low 16 bits are unchanged, and the flags are the same as without pacts. Pacts 7
  decodes to 4 with won false (bit 23 is left alone).
- **Extra cases:**
  - A boss room first stored without the boss flag still pays PerBoss at its clear.
  - The catch-up at 'F' pays 19 for k2-k4 plus boss k4.
  - With no run id, 'F' pays the old V.
  - When the block is gone after rooms were paid, 'F' adds 0 and never the old V on top.
  - At most 16 runs are kept in `Credited`, and the oldest is dropped first.
  - The strict parse refuses 8 bad blocks (list, missing key, unknown key, -1, 2.5, "3", 1001, PactTenths 11), and
    accepts a block with a `_what` key.

## Json for the lab and pc copies

Copy `system/spire-shop.json` from 1bc0bc1 (sha256 `41e495c085ff0fd1ce50191381ac202e03b969da7605d8c5b9d72f2b614220b8`).
It contains the F3 changes (603713f) plus this block, which goes between `Items` and `MetaHeat`:

```json
"Payout": {
  "_what": "Route B F2: photon splinters per room. ...",
  "PerRoom": 3,
  "PerBoss": 10,
  "PactTenths": 1
},
```

Order: if the new binary runs with a json that has no Payout block, it parses fine and pays the old (rooms + 5 per
boss) at 'F', with one warning line per 'F' in the journal. If the json has a malformed Payout block, the whole
catalogue fails to parse: the shop closes and no room is paid. Ship the json together with the binary. Nothing in the
lab or on the pc was touched.

## Technical decisions (F2-n)

- **F2-1:** `Credited` is a JSON list of `{Run, Rooms, Bosses, Base, Finished}`, oldest first, capped at 16 runs. A
  list is used instead of a dict keyed by run id, because phosg's dict is an `unordered_map` and pruning needs the order.
  16 is enough because the savestate resumes only the latest run of a character. Cover: plan § 3 "deduplicated with
  `Credited[run_id]` = set of k" (same meaning), invariant 6.
- **F2-2:** The durable dedupe is per kind: PerRoom once per (run, k) in `Rooms`, and PerBoss once per (run, k) in
  `Bosses`. The in-memory gate is "new room or new boss room in this game", not `rooms.emplace(k).second` alone
  (premise 2). Cover: plan § Numbers "a boss room counts as a room", invariant 6.
- **F2-3:** 'F' pays the pact bonus once per run. `Finished` is set at 'F'; after it a second 'F' and any late room pay
  nothing. The base is the stored `Base` (the splinters actually paid), not a count multiplied by today's PerRoom, so a
  Payout edit mid-run does not rewrite history. Cover: invariant 6, plan § 3 "floor(credited_base * …)".
- **F2-4:** At 'F' with a Payout block, rooms of this game that are not in `Credited` yet are paid first ("caught up"),
  then the bonus. This repairs a records.json write that failed, or a catalogue that was unreadable at a checkpoint.
  Cover: the goal "a freeze keeps what was earned" and the 70-not-120 criterion (the caught-up rooms are deduped).
- **F2-5:** The fallback to the old V happens only when the run has no `Credited` entry: either there is no Payout
  block, or there is no run id (no 'B' in the game). If the block is gone at 'F' but the rooms were already paid, 'F'
  adds nothing and logs a warning. It never pays the old V on top. Cover: plan § 3 "Without a Payout block: the old …"
  and the 70-not-120 criterion.
- **F2-6:** A credit needs the client's 'B' in this game and the game's run id. A room before 'B' is not paid, which
  matches the old behaviour: 'B' already cleared the game's rooms for the 'F' count. Practice comes from the client run,
  as at 'F'. Cover: plan § 3 "not a practice run".
- **F2-7:** The catalogue is re-read on every checkpoint store and every 'F' (as the shop does on every 'M'). An
  unreadable file is one warning line, and no room is paid at that moment. Cover: plan § 3 "re-read on every request".
- **F2-8:** Strict parse bounds: PerRoom and PerBoss 0-1000, PactTenths 0-10, and only integers. `_`-prefixed keys
  are allowed, as in the rest of the file. Cover: brief "parsed strictly".
- **F2-9 (step A):** The M2 warning is logged once per boss drop item with k 0. For one boss that is at most one line
  per player. A per-game once-only set was not built: it adds state and saves at most 3 lines per boss. Cover: brief
  step A.3 "log one warning line when a boss drop resolves k == 0".
- **F2-10 (step A):** M1 is verified by a source check, plus a static case showing the floor-to-k difference. The glue
  needs a live Client and Lobby (premise 3).

## Observations (not built)

- **Until the quest sends pacts:** 'F' bonus is 0, so an average run pays 50 and never 60/70. The quest half (score.py
  `pacts << 19` before the score_keep jump) is on the quest line.
- **A run started before this deploy and resumed after it** loses the rooms played before the freeze. The old server
  paid only at 'F'; the new one has no `Credited` entry for them and does not use `rooms_before_resume` with Payout.
  This is a one-time edge at the deploy.
- **Log volume:** every store for a client without 'B' logs one info line ("room k not paid: no run started"). This is
  harmless, but it is new noise in the journal at a quest's first arrival.
- **Pre-existing:** a repeated 'F' (a resend) still raises `Runs`/`Won`/best time twice. That is not new, and F2 only
  makes the splinters idempotent.
- **F3 report:** the expansion request for the kit_tier wiring is done by step A.
- **Records races:** concurrent BUY and credit on the same records.json are not tested (this is in the "does not cover"
  list). Whether they share one io thread was not verified.
