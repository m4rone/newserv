# Route B F3: shop and heat (newserv side)

Branch spike/route-b, base f93e223, code commit 603713f.

## Premises checked first

1. **The shop json is tracked.** `system/spire-shop.json` is in this repo (`git ls-files`), so it was edited in place;
   no reference copy was needed. The lab copies `lab/ns-ep2/system/spire-shop.json` and `lab/ns-heat/system/spire-shop.json`
   are byte-identical to the f93e223 version (`cmp`). They and the pc copy need the new file (see "Json for the lab and
   pc copies"). Nothing in the lab or on the pc was touched.
2. **The plan's file list cannot wire the kit_tier refusal into the live BUY.** The only caller of
   `spire_shop_buy_refusal` / `spire_shop_next_price` is `spire_shop_handle` in `src/SpireFlagBridge.cc` (around lines
   1783 and 1836), which is outside the F3 writeSet. F3 adds the rule and a pure helper; the caller still passes the
   default `false`, so on a running server kit_tier is **not yet refused**. The two-line caller change is the
   expansion request below. I could not file it with `expansion-request.js`, because that writes under
   `/home/m4rck/pso-roguelike` and the brief says not to write there.

## Coverage claim

Covers: start-level bonus table, heat cap on Normal, kit_tier double-buy refusal (pure decision), shop json parse.
Does NOT cover: the quest side of buying, how the levels feel in play, or the live kit_tier refusal (it needs the
SpireFlagBridge.cc wiring below).

## Changes

- `system/spire-shop.json`: start_level `Levels [0,5,10,14]`, Max 3, OnSale 3, Price [50,60,75], P [0,0,0,0]. HAuto
  (Normal) every value `min(v, 2)`, nulls kept, 9 columns per class unchanged. New `MetaHeat._cap` note. kit_tier,
  hp_mat and atp_grind are unchanged (they keep their P).
- `src/SpireMeta.hh/.cc`:
  - `SpireShopItem::levels`, parsed from optional `"Levels"` (must have Max + 1 entries; without it, level n gives n).
  - `spire_start_level_bonus` returns `Levels[min(owned, Max)]`. Without a catalogue it uses
    `SPIRE_START_LEVEL_FALLBACK_LEVELS = {0, 5, 10, 14}`. `SPIRE_START_LEVEL_FALLBACK_MAX` is kept (now 3) because
    SpireFlagBridge.cc:2478 logs it.
  - `spire_shop_buy_refusal` and `spire_shop_next_price` get a trailing `bool kit2_owned = false`. kit_tier with
    `kit2_owned` is refused with `ALREADY_OPEN` (8). This check runs before the balance, and the list price is 0.
  - `spire_meta_kit2_owned(file, char_class)`: the archetype's 0x388/0x389/0x38A flag is set on Normal.
- `src/DataIndex.hh`: comment only. The SpireDrops description now names BossGrindBase, BossGrindPerK, BossHitBase,
  BossHitPerK and PickFromK, with their meaning taken from SpireDrops.cc (I did not change SpireDrops).
- `src/StaticTests.cc`: the old start-level block now expects the new table (owned 0..5 -> 0,5,10,14,14,14, with and
  without a catalogue). There is one extra rule-block patch at +14. A new section, "Spire shop route B F3", checks the
  shipped json values, bonus 5/10/14/14, start_level P 0, the other items' P unchanged, heat <= 2 for 12 classes at
  P 0..16 and 9 columns, an old catalogue without Levels, a short Levels list refused, kit_tier refused with 8 and price
  0, the flag affecting kit_tier only, and `spire_meta_kit2_owned` per class, cleared, and Hard-only.

## Failing first, then passing

The tests were written first. Header signatures were added, and `.cc` stubs kept the f93e223 behaviour (the
`kit2_owned` parameter was ignored and `spire_meta_kit2_owned` returned false). After that, each fix was applied in
turn:

```
$ cmake --build build -j6   # exit 0
$ ./build/newserv run-static-tests        # stage 1: f93e223 behaviour
I ... - -- Spire start level: overlay level = quest level + owned start_level, rule block raised once (v52)
terminate called after throwing an instance of 'phosg::expectation_failed'
  what():  failure at .../src/StaticTests.cc:745: shipped_bonus[owned] != spire_start_level_bonus(&repo, {{"start_level", owned}})
exit=134

# stage 2: Levels parse + bonus + json start_level (rebuild exit 0)
I ... - -- Spire shop route B F3: +5 start levels per purchase, bought levels add no heat, kit_tier once
  what():  failure at .../src/StaticTests.cc:831: !(spire_shop_heat(repo, cls, p) <= 2)
exit=134

# stage 3: HAuto Normal capped at 2 in the json (no rebuild)
  what():  failure at .../src/StaticTests.cc:860: !(R::ALREADY_OPEN == spire_shop_buy_refusal(repo, 1, {}, 9999, none_open, true))
exit=134

# stage 4: kit_tier refusal + spire_meta_kit2_owned, DataIndex.hh comment (rebuild exit 0, no warnings/errors in log)
$ ./build/newserv run-static-tests
exit=0
87 output lines, 61 test sections, no W/E log lines
I ... - -- Spire shop: debit, balance, cap, sale, rung threshold and the heat table (v50, B-BASIS-2/-3/-13)
I ... - -- Spire start level: overlay level = quest level + owned start_level, rule block raised once (v52)
I ... - -- Spire shop route B F3: +5 start levels per purchase, bought levels add no heat, kit_tier once
...
I ... - -- All static tests passed
```

## Start-level pattern patch: exactly 1 match

The static test covers this with synthetic bins (1 match is patched, 2 or 0 are not), and the new +14 case passes. I
also measured it on the real built quests, reading the laptop newserv's files without changing them:

```
$ ./build/newserv decompress-prs ~/pso-tools/newserv/system/quests/battle/q921-gc-e.bin q921.dec
q921-gc-e 156316 bytes; matches of 49 <level 0> F8 71: 1   (only 49 xx xx xx xx F8 71 at 85095, level 0)
q920-gc-e 115188 bytes; matches of 49 <level 0> F8 71: 1   (at 78079)
```

The bonus value does not change the pattern (it matches the base level), so +14 patches it exactly once (Lv1 -> Lv15).

## Json for the lab and pc copies

Copy `system/spire-shop.json` from 603713f (sha256 `e0a7d1b7dac872280f41841cbc8f927e6e8883ed0313427228e5a0134b02cabd`)
over the old copy. The old copy is identical to f93e223. Only these two blocks differ (plus the `_what` and `_cap` text):

```json
"start_level": {
  "Levels": [0, 5, 10, 14],
  "Max": 3,
  "OnSale": 3,
  "Price": [50, 60, 75],
  "P": [0, 0, 0, 0]
},
```

```json
"HAuto": {
  "HUmar": [0, 0, 0, 1, 2, 2, 2, 2, 2],
  "HUnewearl": [0, 0, 1, 1, 2, 2, 2, 2, 2],
  "HUcast": [0, 0, 1, 2, 2, 2, 2, 2, 2],
  "HUcaseal": [0, 1, 1, 2, 2, 2, 2, 2, null],
  "RAmar": [0, 0, 0, 0, 0, 1, 2, 2, 2],
  "RAmarl": [0, 0, 1, 1, 2, 2, 2, 2, 2],
  "RAcast": [0, 0, 0, 1, 1, 2, 2, 2, 2],
  "RAcaseal": [0, 1, 1, 2, 2, 2, 2, 2, 2],
  "FOmar": [0, 0, 2, 2, 2, 2, 2, 2, 2],
  "FOmarl": [0, 0, 2, 2, 2, 2, 2, 2, 2],
  "FOnewm": [0, 0, 0, 0, 1, 1, 2, 2, 2],
  "FOnewearl": [0, 1, 2, 2, 2, 2, 2, 2, 2]
},
```

Order matters: if the new newserv binary runs with the old json, it parses fine (Levels is optional) and keeps the
old +1-per-level start levels, the old prices, the old P and the uncapped heat. Ship the json together with the binary.

## Expansion request (not filed as a file; see premise 2)

- kind: expansion. writeSet: `src/SpireFlagBridge.cc`.
- Reason: wire the kit_tier refusal into the live BUY and LIST. In `spire_shop_handle`, compute
  `bool kit2 = spire_meta_kit2_owned(meta, character->disp.visual.sh.char_class);` before the BUY branch and pass it
  as the last argument to `spire_shop_buy_refusal(...)` (around line 1783) and `spire_shop_next_price(...)` (around
  line 1836). Optional: the warning at line 2478 says "start_level capped at {}" with FALLBACK_MAX (now 3). The bonus
  is the fallback table, so the wording could become "start levels from the built-in table".

## Technical decisions (F3-n)

- **F3-1:** `Levels` is optional per strength item. Without it, level n gives n. With it, it must have Max + 1 entries
  or the parse throws. Why: an older catalogue keeps its own meaning, prices, P and heat together (never half old,
  half new), and the existing synthetic test catalogues stay valid. Cover: plan § 4 Shop ("Levels [0,5,10,14]").
- **F3-2:** without a readable catalogue, the bonus comes from `SPIRE_START_LEVEL_FALLBACK_LEVELS = {0,5,10,14}` (the
  shipped table), so a broken file never takes away bought levels. `SPIRE_START_LEVEL_FALLBACK_MAX` stays as 3 for the
  existing log line in SpireFlagBridge.cc.
- **F3-3:** the heat cap is data only (the json HAuto values). There is no clamp in code. Why: the plan defines it as
  catalogue data ("the heat-row layout is unchanged"), and the catalogue is data by design (SpireMeta.hh v50). The
  static test pins the shipped table, not a code rule. Nulls are kept: HUcaseal P 8 falls back to P 7 = 2.
- **F3-4:** the kit_tier refusal is a trailing `bool kit2_owned = false` on the two pure functions, so the
  out-of-writeSet caller still compiles unchanged. The check sits in `spire_shop_item_refusal` after MAX_LEVEL and
  NOT_ON_SALE, before the balance, and so also gives list price 0.
- **F3-5:** "already set" means the archetype's flag is set on Normal (difficulty 0). The flag is per difficulty and
  kit_tier is not. Every run starts on Normal, and a flag earned only on Hard leaves Normal runs without the kit, so
  buying it there is no double buy. A cleared entry (false) counts as not set.

## Observations (not built)

- **Pre-existing, now stale text:** open_hard `_what` in spire-shop.json says "H_auto is 4-7" at P 8. With the cap it
  is at most 2. PNeeded 8 itself is sim-derived, and F3 does not change it. All strength items on sale give P 9
  (kit_tier 2 + hp_mat 2 + atp_grind 5), so Hard stays reachable (the existing test passes).
- **Behaviour change for existing owners:** a character that bought start_level level 1 under the old meaning (+1, 15
  splinters, P 1) now gets +5 and P 0 for it. This only affects meta files that already have `"start_level": 1`.
  OnSale was 1, so no file can have more.
- **Quest side:** any quest-side shop text that says "+1 level" for start_level is outside F3 (Q line). I did not
  check it.
- **Heat rows:** the Normal heat rows H3-H7 in the quests can no longer be reached from H_auto on Normal. This is
  harmless, and the layout is unchanged as the plan requires.
- **decisions.md:** the plan's F3 file list includes "a regie line for the partial reversal of B-BASIS-3 and
  B-BASIS-59". B-BASIS-67 already says this. I did not write in pso-roguelike.
