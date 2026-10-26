# Route B F8: better server drops (newserv)

Branch spike/route-b, base 06cbc9c (v59). Commits: 78676d8 (red tests + stubs), c0adbc8 (implementation + config
example), d6597df (S-rank / kill-count guard), plus this report.

**Coverage claim.** Covers: drop-byte correctness of boss and picked weapons (grind, Hit, attribute, special) and
config fallback. Does NOT cover: drop balance in play, the client's acceptance of the bytes on a Wii, or the
quest-side reward pool.

## Premises that were wrong

1. **There is no Spire drop config json under `system/` in the newserv repo.** The only tracked config is
   `system/config.example.json`, which had no `SpireDrops` key. The live `SpireDrops` line is in the untracked
   `~/pso-tools/newserv/system/config.lab.json:557` (LevelsAhead 5, PityDrops 8) and in the pc's own config. I
   documented the new keys and the route B values as a commented example in `config.example.json`; the live configs
   still need the line below (not in this worktree, not mine to write).
2. **`quests/spire/spire.json` has no `drops.newserv` key.** `json.load(...).get('drops')` returns `None`, and no
   file in `/home/m4rck/pso-roguelike` (outside `lab/runs`) mentions `SpireDrops`, `LevelsAhead` or `PityDrops`. There
   is no quest-side mirror to keep in step; the config line is the only place the values live.
3. **The drop filter had no room k.** The quest does not send k with a drop. k is now derived from the receiving
   player's floor through `SpireCheckpointRooms` (highest K of that floor; a boss floor's `Room -1` entry). This needs
   `SpireCheckpointRooms` in the live config; it is in `config.lab.json`. Without it k is 0 and nothing is added.

## What changed

- `src/SpireDrops.hh/.cc`
  - `SpireDropsConfig`: keys `BossGrindBase`, `BossGrindPerK`, `BossHitBase`, `BossHitPerK`, `PickFromK` (0-255,
    missing = 0 = no grind, no Hit). `LevelsAhead`/`PityDrops` defaults unchanged (5/8).
  - `spire_drops_boss_boost(cfg, k)`: grind `base + perK*k`, Hit `min(50, base + perK*k)`; k 0 gives nothing.
  - `spire_drops_pick_boost(cfg, k)`: from `PickFromK` on grind `1+k`, Hit `min(25, 5+2k)` (constants).
  - `spire_drops_boost_weapon(pmt, item, boost)`: grind `min(MaxGrind, boost)`, Hit in the type-5 slot, else the
    first empty slot; never lowers grind/Hit, never a second slot of one type, never touches the special byte, never
    writes into an S-rank name or a kill-count slot.
  - `spire_drops_floor_k(rooms_by_episode, episode, floor)`.
  - `spire_drops_filter_item(..., k)`: the boss weapon (Sega's own fitting drop or the picked one) gets the boss
    boost; a picked weapon outside a boss (item 25-26 replacement, item 27 pity) the pick boost.
  - `spire_drops_filter`: computes k from `c->floor` and logs `room k`.
- `src/StaticTests.cc`: new section "Spire drops F8".
- `system/config.example.json`: commented `SpireDrops` example with the F8 keys.

## Tests

Command: `cmake --build build -j6 && ./build/newserv run-static-tests` (full run each time; output read whole,
`grep -iE "error|warning|failed|expectation|terminate|skip"` empty on base and green).

| Run | Sections (`- -- ` lines) | Exit | Last line |
|---|---|---|---|
| Base 06cbc9c | 58 + "All static tests passed" | 0 | `-- All static tests passed` |
| Red (tests + no-op stubs, 78676d8) | stops in the F8 section | 134 | see below |
| Green (d6597df) | 59 + "All static tests passed" | 0 | `-- All static tests passed` |

Red output:

```
I 1213282 2026-10-26 22:55:56 - -- Spire drops F8: boss and picked weapons carry grind and Hit (route B, plan section 6)
terminate called after throwing an instance of 'phosg::expectation_failed'
  what():  failure at /home/m4rck/code/worktrees/newserv/route-b/src/StaticTests.cc:2502: 2u != cfg.boss_grind_base
```

phosg aborts on the first failed expectation, so the red run shows only the first one (config parsing); every later
F8 expectation also depends on code the stubs did not have.

The F8 section asserts:
- Config: `{}` gives 0 for every new key and 5/8 for LevelsAhead/PityDrops; the route B line parses to 2/2/10/4/5,
  8/6; out-of-range values throw.
- The plan's boss table: k4 +10/26, k8 +18/42, k11 +24/50, k15 +32/50; picked: nothing at k4, k5 +6/15, k10 +11/25,
  k15 +16/25.
- Every common weapon, groups 01-0C, every index, k1-k15, boss and picked boost: grind = `min(MaxGrind, formula)`
  and at most MaxGrind, special byte 0, Hit = formula in a type-5 slot, at most 50 (picked at most 25), no duplicate
  attribute type, never a mag, never a Scape Doll (`spire_drop_is_forbidden`). More than 1000 cases.
- Boost keeps higher own grind/Hit and other attributes, fills its own type-5 slot, gives no Hit with three full
  non-Hit slots, leaves an S-rank name and non-weapons alone.
- Floor k on the lab's `SpireCheckpointRooms`: floor 1 -> 2, 3 -> 5, 11 -> 4, 12 -> 8, 13 -> 11, 14 -> 15, Ep2 14 ->
  18, unlisted / Pioneer 2 / Ep4 -> 0.
- Filter, all 12 classes, k1-k15: boss drop from nothing, monomate, mag, Scape Doll or Sword is a weapon with the
  boss grind/Hit, never a mag or Scape Doll; replaced and pity weapons carry the pick grind/Hit; a non-boss mag or
  Scape Doll is removed. Sega's own fitting Saber at k4 becomes +10 / Hit 26. Missing keys: as before F8 (Sega's
  fitting drop kept unchanged, picked boss weapon without grind or Hit). k 0: nothing added.
- The existing forbidden-drop tests (`spire_drop_is_forbidden`, item 30) are unchanged and pass.

## Config line the live configs need (regie routes it)

Replace the `SpireDrops` line in `~/pso-tools/newserv/system/config.lab.json` and in the pc's
`~/pso-tools/newserv/system/config.json` with:

```
"SpireDrops": {"DropMode": "SERVER_PRIVATE", "OnlyRunItems": true, "LevelsAhead": 8, "PityDrops": 6, "BossWeaponPerPlayer": true, "BossGrindBase": 2, "BossGrindPerK": 2, "BossHitBase": 10, "BossHitPerK": 4, "PickFromK": 5},
```

`SpireCheckpointRooms` must stay present (it gives k). Taking only the binary without this line changes nothing at
drops: missing keys keep today's behaviour, and the old line's explicit 5/8 keep LevelsAhead/PityDrops.

## Decisions taken (for the epic's decisions.md; I do not write in pso-roguelike)

- **F8-1 config location:** `config.example.json` got a commented example; the live line goes to the regie.
  Why: the only Spire drop config is untracked and outside this worktree (premise 1).
- **F8-2 room k:** from the player's floor via `SpireCheckpointRooms`, highest K of the floor; k 0 adds nothing.
  Why: the drop carries no k, and the boss floors all have a `Room -1` entry.
- **F8-3 Sega's own boss drop:** when it already fits the player it is kept and gets the boss grind/Hit too; its
  special byte is left as Sega made it (never added, never cleared). Why: plan "boss weapon per player" carries grind
  and Hit, and clearing a Sega special would lower what Sega gave.
- **F8-4 picked formula and caps are constants** (`1+k`, `5+2k`, caps 50/25), not config keys. Why: the brief and plan
  list only Boss* and PickFromK as keys.
- **F8-5 three full slots without Hit:** no Hit is added, nothing replaced; the grind still applies.
- **F8-6 S-rank / kill count:** Hit is never written into an S-rank name or a kill-count slot (defensive; GC Ep1&2
  bosses do not drop those).

## Observations (not built)

- `src/DataIndex.hh:200` still describes the SpireDrops example without the F8 keys (outside the writeSet).
- The commit hook warned that the newserv worktree records no integration branch / rollout (`.claude/omgeving.json`
  or `CLAUDE.md § Git flow`). Not mine to decide; for the regie.
- Plan F8 line "spire.json drops.newserv kept in step" points at a key that does not exist (premise 2).
