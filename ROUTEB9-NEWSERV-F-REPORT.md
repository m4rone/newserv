# Route B bundle 9, newserv part F (server equip): report

Branch spike/route-b-9-equip, base 5e374c8, slice commit 11a425e (`git log --oneline -1` after the commit). Not pushed.

**Coverage claim.** Covers: § 3 on the newserv side, r15, 'C' 13/14 and the client function as far as it can be
grounded statically. Does not cover: whether the client actually runs the function correctly (only Dolphin or the Wii
decides that), the quest, 'C' 12 and the save point notice.

## Status per point

| Point | Status | Commit |
|---|---|---|
| 1 client function `SpireEquipItemGC.s` | NOT written (hard rule: no guessed address; see below) | - |
| 2 sending + r15 + switch | done; r15 = 0 for every client until the .s exists | 11a425e |
| 3 'C' 13 / 'C' 14 | done (find, log, send when r15 = 1) | 11a425e |
| 4 texts + 'C' comment block | done | 11a425e |
| 5 static tests | done, 4 cases; red against stubs, green with the code | 11a425e |

## 1. Why the client function is not written

The function needs two things on 3OP0: the game's own equip routine behind Item Pack > Equip (the one that sets the
equipped state and sends the 6x25), and a way from the local player to its live TItem objects. What this tree has:

| What | 3OP0 value | Source | Usable? |
|---|---|---|---|
| local_client_id | `r13 - 0x5220` | `system/client-functions/ExtendedItemInfo.s:174`, `HungryMagSound.s:30` (8th of `.versions 3OJ2 3OJ3 3OJ4 3OJ5 3OE0 3OE1 3OE2 3OP0`) | yes |
| TItem item_id | `+0xD8` | `ExtendedItemInfo.s:186` | yes |
| TItem data1 | `+0xEC` | `ExtendedItemInfo.s:137` | yes |
| TItemEquipBase_v16 | `0x8010B414` | `BugFixes.s:578` (8th VERS entry; `.versions` at :521) | address yes, meaning no |
| TItemEquipBase_v17 | `0x8010B298` | `BugFixes.s:579` | address yes, meaning no |
| TObjPlayer for a client id | none for 3OP0 | only 3OE1 `0x801BA59C` (`Debug/PlayerFlags.s:12`, `.versions 3OE1`) | no |
| TObjPlayer_objs / export_inventory | none for 3OP0 | only 3OE2 (`notes/ar-codes.txt:1022-1042`) | no |
| Item Pack "Equip" routine (sends 6x25) | none, in any version | `grep -rn -i equip system/client-functions` finds only BugFixes.s:578-579 | no |

The only use of TItemEquipBase_v16/v17 (`BugFixes.s:586-615`, g5_hook4) calls v17(item, 1) then v16(item, 1) for each
of 13 pointers at `TObjPlayer + 0xD04` and restores `+0x32C` around it: a stat re-apply. Nothing in the tree says they
set the equipped flag or send a 6x25, so calling them would at best change stats without the server seeing a 6x25, at
worst desync or freeze. That is a guess, so per the dispatch the function is not written.

**Measurement on Dolphin that decides it** (GPOP8P, a test character on a copied card):
1. Debugger breakpoint on the 6x25 send: equip a weapon through Item Pack > Equip, break where the subcommand byte 0x25
   is written into the send buffer (memory breakpoint on the outgoing 60 buffer, or on the function newserv logs as
   "Equipped item"), read the call stack: the frame above the send is the equip routine; note its address and
   arguments (TObjPlayer*, TItem* or item id).
2. The 3OP0 address of TObjPlayer::for_client_id: find the 3OE1 function at 0x801BA59C by its byte pattern in a 3OP0
   RAM dump (the same technique the VERS lists in this tree came from).
3. Where the player's item list lives (are the 13 slots at TObjPlayer + 0xD04 the equipped items or the whole pack),
   read from the same dump with the known TItem offsets above.
With those three, the .s is: local_client_id -> TObjPlayer -> walk its items -> match `+0xD8` == argument -> call the
equip routine; never a write to item data.

## 2. Sending, r15 and the switch (commit 11a425e)

- Name: `.meta key` "SpireEquipItem" = `SPIRE_EQUIP_ITEM_FUNCTION_KEY` (`src/SpireEquipItem.hh`). Deliberately NOT
  `SPIRE_EQUIP_FUNCTION_NAME`: `quests/spire/pc-build.sh:85-91` puts every `SPIRE_<X>_FUNCTION_NAME = "<name>"` into
  `required.txt` and refuses the build when no `Spire*.s` carries that key. Rename it in the commit that adds the .s.
  Check: `grep -rhoE 'SPIRE_[A-Z0-9_]+_FUNCTION_NAME = "[A-Za-z0-9]+"' src` -> only
  `SPIRE_BBA_TX_GUARD_FUNCTION_NAME = "SpireBBATxGuard"`.
- Switch `SpireEquipItemPatch`, default true (`src/DataIndex.cc`, `get_bool("SpireEquipItemPatch", true)`; field in
  `src/DataIndex.hh`; example in `system/config.example.json`).
- r15 (`SPIRE_EQUIP_REGISTER`) = 1 only when the specific version is 3OP0, the switch is on, the client runs function
  calls (`HAS_SEND_FUNCTION_CALL` and `SEND_FUNCTION_CALL_ACTUALLY_RUNS_CODE`) and the function index has
  SpireEquipItem for 3OP0 (`spire_equip_item_available_for`, SpireFlagBridge.cc). Written in EVERY 'M' answer right
  before r41 and DONE, also on a refusal; the 'M' log line ends with `; r15=<0|1>`. Without the .s it is 0 everywhere.
- Grep that newserv wrote no r15-r19 before: `grep -n -E "REGISTER = 1[5-9];|send_register\(c, 1[5-9]\b" src/*.hh src/*.cc`
  -> no hits; the 'M' levels are r1-r7 and prices r8-r14 (static test asserts r14 < r15).
- When newserv sends the function: on the writer's 'C' 13 or 'C' 14, only with r15 = 1 for that client and a target
  found, via `prepare_client_for_patches` + `send_function_call(c, fn, {{"item_id", id}})` in a detached coroutine; the
  B3 answer is logged ("Spire equip: ... client answered N"). The .s must therefore expose the argument as label
  `item_id` (`SPIRE_EQUIP_ITEM_ID_LABEL`).

## 3. 'C' 13 and 'C' 14 (commit 11a425e)

`spire_equip_prompt_handle` (SpireFlagBridge.cc), called before the text lookup, so neither code gets a text. Code
layout `'C' << 24 | point << 8 | low`, bits 16-23 must be 0. 'C' 13 stores G in the client's run state (for the
ticker). Search in `c->character_file(false, true)` (the run copy during a run), pure functions in
`src/SpireEquipItem.hh`:
- 'C' 13: the last (newest; a 6x2B appends) unequipped, unwrapped weapon (data1[0] = 00) with data1[3] = G.
- 'C' 14: the one type-02 item, when exactly one exists and it is not equipped.
- r15 = 0: log "r15 = 0 for this client, nothing sent". Not found: log "no such item in the inventory copy, nothing
  sent". One line each.

## 4. Texts (commit 11a425e)

- Kit ticker (`spire_kit_use_text`, SpireMag.cc): r15 = 0 -> `SPIRE_KIT_USE_TEXT` unchanged. r15 = 1 ->
  "Your kit weapon is ground +<G>. Use your HP Materials now (Item Pack)." G = the client's 'C' 13 low byte, or when
  that has not come within the 3 s: 2 x atp_grind (the quest's G; max 10, below every kit weapon's MaxGrind of 25+).
  hp_mat 0 -> only the grind sentence; G 0 -> only the HP Materials sentence.
- Evolution text (`spire_run_mag_evolve_text(name, shell, server_equips)`): r15 = 1 drops " Equip it: Item Pack > Mag."
- Comment block at the 'C' kinds in `src/SpireFlagBridge.hh` updated (13-14 listed).

## 5. Static tests (commit 11a425e)

New section "Spire bundle 9" in `src/StaticTests.cc`, 4 cases: r15 decision (1 and every 0 reason, other versions,
register free); 'C' 13 found (newest of two, equipped and wrapped skipped), not found, r15 = 0, other point; 'C' 14
found, no mag, two mags, already equipped, r15 = 0; both texts in both r15 states, one line, <= 90 characters.

Red first, against stubs (functions returning false / nullopt / the old texts), log `equip-stub2.log`, exit 134:
```
B9 (r15: 1 only for 3OP0 ...) FAILED: failure at .../src/StaticTests.cc:5555: !(spire_equip_item_availabl...
B9 ('C' 13: the newest unequipped weapon with grind G) FAILED: failure at .../src/StaticTests.cc:5575: !(id.has_value())
B9 ('C' 14: the one run mag) FAILED: failure at .../src/StaticTests.cc:5592: 0x00210034u != spire_equip_target(...)
B9 (kit ticker and evolution text ...) FAILED: failure at .../src/StaticTests.cc:5607: std::string("Your kit weapon is ground +1...
what():  failure at .../src/StaticTests.cc:5620: 0u != b9_failed
```
Green with the code, `cmake --build build -j16 && ./build/newserv run-static-tests`, log `equip-real.log`, exit 0:
```
-- Spire bundle 9 (ux-pass): server equip, r15, 'C' 13/14, the kit and evolution texts
   B9 (r15: 1 only for 3OP0 ...)
   B9 ('C' 13: the newest unequipped weapon with grind G)
   B9 ('C' 14: the one run mag)
   B9 (kit ticker and evolution text in both r15 states, one line within 90 characters)
-- All static tests passed
```
No test for assembling the .s or for required.txt: the .s does not exist.

## Decisions (B-E entries; this worktree has no epic decisions.md, so they stand here for the orchestrator to carry over)

### B-E09-F-1 — Client function not written, r15 always 0
- **Datum:** 2026-10-27
- **Route:** zelf
- **Type:** technisch
- **Klasse:** 2
- **Dekking:** dispatch "Harde regel: gok geen adres ... schrijf de functie dan niet: bouw punt 2-4 met r15 altijd 0"
- **Beslissing:** no SpireEquipItemGC.s; r15 = 0 follows from the missing function in the index, no hard-coded 0.
- **Waarom:** no 3OP0 source for the equip routine or the player's item list (table above).

### B-E09-F-2 — Name constant not in the pc-build form
- **Datum:** 2026-10-27
- **Route:** zelf
- **Type:** technisch
- **Klasse:** 2
- **Dekking:** pc-build.sh:89-91 refuses a build with a name but no .s; dispatch fallback B-E09-F-1
- **Beslissing:** `SPIRE_EQUIP_ITEM_FUNCTION_KEY` instead of the contract's `SPIRE_EQUIP_FUNCTION_NAME`, until the .s lands.
- **Waarom:** the contract name would break the pc build while the .s is missing.

### B-E09-F-3 — Kit weapon matched without data1[0-2]
- **Datum:** 2026-10-27
- **Route:** zelf
- **Type:** technisch
- **Klasse:** 2
- **Dekking:** ROUTEB9-PROTOCOL.md § 1 ('C' 13 carries only G) and § 3; newserv does not carry the quest's kit table
- **Beslissing:** newest unequipped unwrapped weapon with grind G; data1[0-2] not compared.
- **Waarom:** the code gives newserv no way to know the kit weapon's type; right after the kit it is the one new
  unequipped weapon. Contract gap reported below.

### B-E09-F-4 — Kit ticker variants and G fallback
- **Datum:** 2026-10-27
- **Route:** zelf
- **Type:** technisch (wording within the contract text)
- **Klasse:** 2
- **Dekking:** ROUTEB9-PROTOCOL.md § 3 texts; CLAUDE.md standing mandate (wording is not Marck's)
- **Beslissing:** contract text when G > 0 and hp_mat > 0; one sentence when one of them is 0; G from 'C' 13, else
  2 x atp_grind.
- **Waarom:** "ground +0" or "Use your HP Materials" without any would be false.

## Gezien maar niet gebouwd

- A static test that loads the real function index and asserts r15 = 0 for 3OP0 today (flips when the .s lands).
- 'C' 13 carries only G; the contract's "kit weapon's data1[0-2]" needs bits 16-23 or another code if the match must
  be stricter (contract gap, ROUTEB9-PROTOCOL.md:23 vs :63).
- `prepare_client_for_patches` mid-run (as ExitAnywhere does at SpireFlagBridge.cc ~1197) is not measured for this use.
- Pre-existing (from the research): `SpireMag.hh` comment at `spire_kit_use_text_due` says atp_grind no longer makes it
  due; the code and test say it does.

## Blockers

- The client function: needs the Dolphin measurement in § 1. Until then r15 = 0 and the quest keeps the bundle 8 kit.
- The `[werkwijze]` hook warned that this newserv worktree has no Integratiebranch/Uitrol recorded; only noted, nothing
  pushed.
