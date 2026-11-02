# Route B bundle 9, newserv part F3: SpireEquipItemGC.s for 3OP0

Branch spike/route-b-9, base aa1cab0, slice commit cab8652 (`git log --oneline -1` right after the commit:
`cab8652 feat: SpireEquipItem client function for 3OP0, r15 on`). Not pushed. No Dolphin, lab, pc or Wii used.

**Coverage claim.** Covers: the .s for 3OP0 built from the addresses proven in F2, the name constant, r15 and the
tests. Does not cover: whether the client handles the function correctly when it runs (the lab check decides that,
meetpunt 7), the quest, and versions other than 3OP0.

## Status per point

| Point | Status | Commit |
|---|---|---|
| 1 `system/client-functions/SpireEquipItemGC.s` | done | cab8652 |
| 2 `SPIRE_EQUIP_FUNCTION_NAME = "SpireEquipItem"` (same commit as the .s) | done | cab8652 |
| 3 r15 = 1 for 3OP0 with the switch on; tests | done (red without the .s, green with it) | cab8652 |
| 4 precedent research | done: no exact precedent, two partial ones (§ 4) | report commit |

## 1. The function

- Name / `.meta key`: `SpireEquipItem` (`SPIRE_EQUIP_FUNCTION_NAME`, `src/SpireEquipItem.hh`), `.versions 3OP0`,
  `visibility="hidden"`. Argument: label `item_id` (`SPIRE_EQUIP_ITEM_ID_LABEL`), written by newserv.
- Switch: `SpireEquipItemPatch`, default true (`src/DataIndex.cc:762`, `get_bool("SpireEquipItemPatch", true)`;
  `system/config.example.json:1009`). false gives every client r15 = 0 without a rebuild.
- When newserv sends it: on the writer's 'C' 13 (kit weapon, low byte G) or 'C' 14 (run mag after an evolution), only
  when r15 = 1 for that client and the target is found in newserv's copy of its inventory (the run copy during a run):
  `spire_equip_prompt_handle` -> detached `spire_equip_item_send` -> `prepare_client_for_patches` +
  `send_function_call(c, fn, {{"item_id", id}})`; the B3 answer is logged as "Spire equip: ... client answered N"
  (`src/SpireFlagBridge.cc` ~2600-2650, unchanged apart from the constant's name).
- Return value (r3, logged from the B3): 0 = no local TObjPlayer, no pack, or the id is not in the local player's pack;
  1 = 6x25 handed to the game's sender; 2 = already equipped in its slot; 3 = neither weapon nor mag.

### Every address and offset with its source

| In the .s | Value | Proven in |
|---|---|---|
| `TObjPlayer_for_client_id` | 0x801BAC08 | F2 § 3 (disassembly 801BAC08-801BAC44, 144 call sites after `lwz r3,-0x5220(r13)`) |
| local_client_id | `r13 - 0x5220` | F2 § 3 (e.g. 80026F48); `ExtendedItemInfo.s:174` (8th VERS = 3OP0) |
| pack object | TObjPlayer + 0xD00 | F2 § 2 (handler 801BA56C) and § 4 |
| `find_pack_node_by_id` | 0x801D53D0 | F2 § 4 (801D53E0-801D5BFC) and the 6x25 handler 801BA574 (F2 § 2) |
| `pack_node_item` | 0x801D5C20 | F2 § 4 (801D5C34) and handler 801BA580 (F2 § 2) |
| data1[0] (type byte) | TItem + 0xEC | `ExtendedItemInfo.s:137` (3OP0 in its `.versions`); the unit check 0x80178614 that F2 § 2 names inside TObjPlayer_equip_item reads `lbz r0,0xec(r3)` == 1 and `lbz r0,0xed(r3)` == 3 (disassembled again for this part, see § 3) |
| TItem vtable | TItem + 0x18 | F2 § 2 (menu site 8021A01C `lwz r12,0x18(r3)`) |
| item slot | vtable entry 0x84, `extsb` | F2 § 2 (8021A020-8021A02C; TObjPlayer_equip_item 801BA45C-801BA46C) |
| `equip_slots_get` | 0x801C6C18 on TObjPlayer + 0xD04 | F2 § 4 (801C6C18-801C6C38, callers `addi r3,player,0xd04`) |
| item id | TItem + 0xD8 | F2 § 4 (getter 80099EC0 `lwz r3,0xd8(r3)`) |
| `send_unequip_item_6x26` | 0x801BA398 `(player, item_id)` | F2 § 2 (menu site 8021A04C; 801BA398-801BA3F8) |
| `send_equip_item_6x25` | 0x801BA5AC `(player, item_id, 0)` | F2 § 2 (801BA5AC-801BA60C; slot 0 as menu site 8021A054) |

No address outside F2 is used. 0x801BA3FC (TObjPlayer_equip_item) is deliberately NOT called: the 6x25 send path
equips locally (F2 § 2), and the static test asserts the code does not load it.

### The .s (`system/client-functions/SpireEquipItemGC.s`, 137 lines)

Flow: stack frame, save r28-r31 (`stmw`) -> read `item_id` -> `player = 0x801BAC08(lwz r13-0x5220)`, null -> 0 ->
`pack = lwz player+0xD00`, null -> 0 -> `node = 0x801D53D0(pack, id)`, null -> 0 -> `item = 0x801D5C20(node)`, null ->
0 -> `lbz item+0xEC` not 0 and not 2 -> 3 -> `slot = extsb(item->vtable[0x84]())` -> `old = 0x801C6C18(player+0xD04,
slot)`; `old == item` -> 2; `old` non-null -> `0x801BA398(player, lwz old+0xD8)` -> `0x801BA5AC(player, id, 0)` -> 1.
Every call is `lis r0 / ori r0 / mtctr r0 / bctrl` (the relocatable form of `GetExtendedPlayerInfoGC.s`). It writes
nothing but its own stack frame.

The assembled code, from `./build/newserv assemble-all-client-functions --skip-encrypted <scratch>/equip3-fns` (exit 0,
`SpireEquipItem.3OP0.std.bin`, 364 bytes), disassembled with capstone:
```
+000  9421ffe0  stwu r1, -0x20(r1)
+00C  bf810010  stmw r28, 0x10(r1)
+010  48000009  bl 0x18
+014  00000000  .word                    item_id
+01C  83a30000  lwz r29, 0(r3)
+020  806dade0  lwz r3, -0x5220(r13)
+024  3c00801b  lis r0, -0x7fe5
+028  6000ac08  ori r0, r0, 0xac08       0x801BAC08
+034  7c7e1b79  or. r30, r3, r3
+03C  807e0d00  lwz r3, 0xd00(r30)
+04C  3c00801d  lis r0, -0x7fe3
+050  600053d0  ori r0, r0, 0x53d0       0x801D53D0
+064  3c00801d  lis r0, -0x7fe3
+068  60005c20  ori r0, r0, 0x5c20       0x801D5C20
+07C  881c00ec  lbz r0, 0xec(r28)
+094  819c0018  lwz r12, 0x18(r28)
+098  818c0084  lwz r12, 0x84(r12)
+0A4  7c7f0774  extsb r31, r3
+0A8  387e0d04  addi r3, r30, 0xd04
+0B0  3c00801c  lis r0, -0x7fe4
+0B4  60006c18  ori r0, r0, 0x6c18       0x801C6C18
+0C8  7c03e040  cmplw r3, r28
+0D0  808300d8  lwz r4, 0xd8(r3)
+0D8  3c00801b  lis r0, -0x7fe5
+0DC  6000a398  ori r0, r0, 0xa398       0x801BA398
+0F0  38a00000  li r5, 0
+0F4  3c00801b  lis r0, -0x7fe5
+0F8  6000a5ac  ori r0, r0, 0xa5ac       0x801BA5AC
+120  bb810010  lmw r28, 0x10(r1)
+130  4e800020  blr
```
(excerpt; the full listing has 77 words, all branch targets land on the intended labels: +038/+044/+060/+078 -> +10C
return 0, +08C -> +11C return 3, +0C4 -> +0E8 send, +0CC -> +114 return 2.)

## 2. Name constant and r15

- `src/SpireEquipItem.hh`: `SPIRE_EQUIP_ITEM_FUNCTION_KEY` -> `constexpr const char* SPIRE_EQUIP_FUNCTION_NAME =
  "SpireEquipItem";`, uses in `src/SpireFlagBridge.cc` (4) and `src/StaticTests.cc` (1) renamed, header state comment
  updated. Same commit as the .s.
- r15 needs no code change: `spire_equip_item_available_for` (SpireFlagBridge.cc:785) now finds the function in the
  index for 3OP0, so r15 = 1 for a 3OP0 client with the switch on and function calls that run code; every other
  version still 0.

pc-build check, read-only, with lines 81-91 of `quests/spire/pc-build.sh` copied verbatim into a scratch script
(`diff` against the original shows only my added `echo` lines), NS_REPO = this worktree, NEWSERV = HEAD:
```
$ cd /home/m4rck/code/worktrees/pso-roguelike/route-b-9 && grep -rhoE 'SPIRE_[A-Z0-9_]+_FUNCTION_NAME = "[A-Za-z0-9]+"' /home/m4rck/code/worktrees/newserv/route-b-9/src
SPIRE_BBA_TX_GUARD_FUNCTION_NAME = "SpireBBATxGuard"
SPIRE_EQUIP_FUNCTION_NAME = "SpireEquipItem"
$ bash equip3-pcb.sh
== NEWSERV cab8652a6508eb4f8d0a235e3421aa48b56e1718
== required.txt:
SpireBBATxGuard true
SpireEquipItem true
== copied:
required.txt
SpireBBATxGuardGC.s
SpireEquipItemGC.s
OK SpireBBATxGuard: a Spire*.s carries .meta key="SpireBBATxGuard"
OK SpireEquipItem: a Spire*.s carries .meta key="SpireEquipItem"
check exit 0
exit=0
```

## 3. Tests

Changed in `src/StaticTests.cc` (section "Spire bundle 9 (ux-pass): server equip ..."):
- the r15 case: the comment "no SpireEquipItemGC.s (bundle 9 today)" now reads "the function not in the index"; the
  assertions were already pure (function_loaded parameter), none expected r15 = 0 for 3OP0 from the real index.
- new case "SpireEquipItem assembles for 3OP0, is in the function index, and r15 = 1 for 3OP0": loads
  `ClientFunctionIndex("system/client-functions", true)`, gets SpireEquipItem for 3OP0 (POWERPC, hidden), label
  `item_id` present and in range, `generate_client_command({{"item_id", 0x00210017}})` non-empty, the six F2 call
  targets present as `lis r0 / ori r0` pairs, 0x801BA3FC absent, `spire_equip_item_available(3OP0, true, true, true)`,
  and `index.get` throws for 3OE0, 3OE1, 3OE2, 3OJ5, 3SP0.

Red first, the .s held aside (`equip3-red.log`, exit 134):
```
[100%] Built target newserv
   B9 (SpireEquipItem assembles for 3OP0, is in the function index, and r15 = 1 for 3OP0)
E ... B9 (SpireEquipItem assembles for 3OP0, is in the function index, and r15 = 1 for 3OP0) FAILED: unordered_map::at
  what():  failure at /home/m4rck/code/worktrees/newserv/route-b-9/src/StaticTests.cc:5844: 0u != b9_failed
```
Green with the .s, `cmake --build build -j16 && ./build/newserv run-static-tests` (`equip3-green.log`, exit 0):
```
-- Spire bundle 9 (ux-pass): server equip, r15, 'C' 13/14, the kit and evolution texts
   B9 (r15: 1 only for 3OP0 with the switch on, function calls that run code and the function loaded)
   B9 (SpireEquipItem assembles for 3OP0, is in the function index, and r15 = 1 for 3OP0)
   B9 ('C' 13: the newest unequipped weapon with grind G)
   B9 ('C' 14: the one run mag)
   B9 (kit ticker and evolution text in both r15 states, one line within 90 characters)
-- All static tests passed
```
Logs: `/tmp/claude-1000/-home-m4rck-pso-roguelike/aac0f14a-268c-400e-a78d-2c85802b6c62/scratchpad/equip3-*`.

## 4. Precedent: a client function that enters a client send path from its own call

Searched every `.s` under `system/client-functions` for `bctrl` outside installed hooks and for send/dispatch labels.

**No exact precedent.** No existing function calls 0x801E4BAC (or any 6x sender) from inside its own B2 call.

Partial precedents, both active on 3OP0:
- `GetExtendedPlayerInfoGC.s` (3OP0 in `.versions`): from its own call it sends a network command through
  `root_protocol->send` (vtable index 0x0A) after `TProtocol_wait_send_drain`; newserv sends it routinely at login
  (`src/SendCommands.cc` ~2471). That covers "network send from a B2 call", not the local dispatch.
- `CallProtectedHandler.s` (3OP0 in `.versions`, RcvPsoData2 = 0x801E4698): from its own call it runs the game's
  receive path for a 60/62 command; in the image, 0x801E4698 calls 0x801E45E0 at 0x801E4A60, the same local
  subcommand dispatcher the 6x25 send path reaches (F2 § 2). newserv uses it through `send_protected_command` (forced
  e.g. for the 6x0C healing ring in `send_remove_negative_conditions` and the infinite-HP revive). That covers "local
  dispatch to a subcommand handler from a B2 call", not the network queueing of 0x801DC6C0. Whether it has run on this
  Wii is not measured here.
- Not a precedent but a cross-check: `EnemyDamageSyncGC.s:19` names 0x801E4BAC `send_and_handle_60` for 3OP0 (8th
  VERS), the same address and meaning F2 found; it calls it from an installed hook, i.e. from game context.

Open point of F2 stays open: the labcheck (breakpoint 0x801BA3FC, F2 § 6) decides whether the combination is safe.

## Decisions (B-E entries; this worktree has no epic decisions.md, so they stand here for the orchestrator)

### B-E09-F3-1 — Weapon/mag filter on TItem + 0xEC
- **Datum:** 2026-10-27
- **Route:** zelf
- **Type:** technisch
- **Klasse:** 2
- **Dekking:** dispatch "Alleen wapens en mags; schrijf nooit itemdata"; `ExtendedItemInfo.s:137` (3OP0); 0x80178614 from F2 § 2 reads +0xEC/+0xED
- **Beslissing:** `lbz item+0xEC` must be 0 (weapon) or 2 (mag), otherwise return 3 without sending.
- **Waarom:** the type byte is the one offset proven on 3OP0 both in this tree and in the image; the slot number's meaning is not in F2.

### B-E09-F3-2 — Return codes 0/1/2/3
- **Datum:** 2026-10-27
- **Route:** zelf
- **Type:** technisch
- **Klasse:** 2
- **Dekking:** F2 § 5 sketch (0 not found, 1 sent, 2 already equipped); the server logs the B3 value (SpireFlagBridge.cc ~2607)
- **Beslissing:** the sketch's three values plus 3 = not a weapon or mag.
- **Waarom:** the log line then tells the lab check which branch ran.

### B-E09-F3-3 — Null check on the pack pointer, direct +0xD8 read
- **Datum:** 2026-10-27
- **Route:** zelf
- **Type:** technisch
- **Klasse:** 2
- **Dekking:** F2 § 4 (801D53E8 dereferences pack+0x1C4; getter 80099EC0 is `lwz r3,0xd8(r3)`)
- **Beslissing:** return 0 when TObjPlayer+0xD00 is null; read old->item_id with `lwz +0xD8` instead of calling 0x80099EC0.
- **Waarom:** the server call can come at a moment the menu never sees; the getter is that one instruction.

### B-E09-F3-4 — Red run by holding the .s aside
- **Datum:** 2026-10-27
- **Route:** zelf
- **Type:** technisch
- **Klasse:** 2
- **Dekking:** dispatch "Laat de nieuwe assemble-test eerst falen zonder de .s (toon het)"
- **Beslissing:** the red build ran with the .s moved to the scratchpad, then restored; one commit carries .s, rename and tests.
- **Waarom:** pc-build needs the rename and the .s in the same commit.

## Gezien maar niet gebouwd

- as gebruiksvriendelijkheid · the B3 log prints only the number; "0 not found / 1 sent / 2 already / 3 not weapon or
  mag" in words would save the lab reader a lookup · `src/SpireFlagBridge.cc:2607` · one string table, a few lines.
- as datakwaliteit · return 1 cannot tell a silent no-send inside 0x801BA5AC (0x801B0F4C special mode, player+0x324
  == 2; F2 § 6) from a real send; only the 6x25 arriving at newserv shows it · newserv's 6x25 log · nothing to build
  until the lab check says it matters.
- as datakwaliteit (pre-existing, from part F) · 'C' 13 matches the kit weapon without data1[0-2] · ROUTEB9-PROTOCOL.md:63 vs `src/SpireEquipItem.hh` spire_equip_find_kit_weapon.
- as toekomstbestendigheid · the function is 3OP0 only; other GC versions need their own F2-style address proof ·
  `SpireEquipItemGC.s` `.versions` · one research part per version.
- The F-report's "Gezien maar niet gebouwd" items stand unchanged except the first one (a test against the real index
  now exists and asserts r15 = 1 for 3OP0).

## Blockers

None for this part. Effect to know before deploying: with this newserv on the pc, r15 = 1 for Marck's 3OP0 client, so
a quest that reads r15 takes the bundle 9 kit path (no Monogrinders, the weapon already ground +G) and relies on this
function being correct at run time, which is unmeasured. A silent failure leaves the weapon or mag unequipped (no item
lost; equipping by hand still works); a freeze is the risk the lab check rules out. `SpireEquipItemPatch: false` in
the pc config turns it off without a rebuild.
