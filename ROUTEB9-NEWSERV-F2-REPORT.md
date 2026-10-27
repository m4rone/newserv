# Route B bundle 9, newserv part F2: static research SpireEquipItem on 3OP0

Branch spike/route-b-9-equip, base 8c8c1ab. Not pushed. No .s written, no newserv code changed.

**Coverage claim.** Covers: the three addresses on 3OP0 as far as they can be proven statically from the game
executable, with calibration. Does not cover: whether the client handles the function correctly at run time (only
Dolphin or the Wii decides that), the .s itself, and versions other than 3OP0.

Tooling: capstone 5.0.7 in a scratch venv; executable and flat memory image in
`/tmp/claude-1000/-home-m4rck-pso-roguelike/aac0f14a-268c-400e-a78d-2c85802b6c62/scratchpad/re/` (not in git).
Every fragment below is `address  bytes  instruction` straight from that image.

## 0. Which executable (deviation from the dispatch)

The disc header points (u32 at 0x420 = 0x1E000) to `main.dol`, but that DOL is 0x321E0 bytes with text only at
0x8000C000-0x80036AC0: it is the boot switcher, not the game. The disc FST holds the game as `/PsoV3.dol`
(ISO offset 0x5668FE20, size 0x4FE9C0; also `switcher.dol`, `iwanaga.dol`, debug variants). Loaded by its own DOL
header: text0 0x8000C000-0x8000E520, text1 0x8000F740-0x804331C0, data sections up to 0x805DD440, bss 0x804FEFE0 +
0xDE484, entry 0x8000C040. All addresses below are in PsoV3.dol. Disc title in the header reads
"PSO Ep I&II EU (own chars)", game id GPOP8P.

## 1. Calibration (passed)

Six addresses newserv already uses for 3OP0 sit on exactly the code newserv expects:

| newserv source | 3OP0 address | what newserv expects | found |
|---|---|---|---|
| `BugFixes.s` g5_hook4_call | 0x801BA20C | function start whose first instruction is `stwu r1,-0x10(r1)` (g5_hook4_start replays it, then `b g5_hook4_ret` = +4) | yes |
| `BugFixes.s:578` TItemEquipBase_v16 | 0x8010B414 | function start | yes |
| `BugFixes.s:579` TItemEquipBase_v17 | 0x8010B298 | function start | yes |
| `ItemPickup.s:89` hook_call | 0x801B60F0 | `mr r3,r28` (hook replays it, returns to +4, skip target +0x14) | yes |
| `ItemPickup.s:96` | 0x8024D5D0 | `li r4,…` with button mask (patched to `li r4,0x0008`) | yes, `li r4,0x108` |
| `SpireBBATxGuardGC.s` | 0x80429A78 | the 8 original words in its own comment | yes, all 8 |

```
801BA208  4e800020  blr                       <- end of previous function
801BA20C  9421fff0  stwu r1, -0x10(r1)        <- g5_hook4_call: exactly the replayed instruction
801BA210  7c0802a6  mflr r0                   <- g5_hook4_ret

8010B410  4e800020  blr
8010B414  9421ffd0  stwu r1, -0x30(r1)        <- TItemEquipBase_v16
8010B294  4e800020  blr
8010B298  9421ffe0  stwu r1, -0x20(r1)        <- TItemEquipBase_v17

801B60F0  7f83e378  mr r3, r28                <- ItemPickup hook_call (hook does `mr r3,r28; b 0x801B60F4`)
801B60F4  388000e0  li r4, 0xe0
801B6104  3be00000  li r31, 0                 <- ItemPickup skip target

8024D5D0  38800108  li r4, 0x108              <- ItemPickup "Z opens menu" patch site

80429A78  2c1d0000  cmpwi r29, 0              <- SpireBBATxGuard: the 8 words from its comment, in order
80429A7C  4182001c  beq 0x80429a98
80429A80  818dd694  lwz r12, -0x296c(r13)
80429A84  38000000  li r0, 0
80429A88  387c0000  addi r3, r28, 0
80429A8C  900dd694  stw r0, -0x296c(r13)
80429A90  7d8803a6  mtlr r12
80429A94  4e800021  blrl
```

## 2. Question 1: the equip routine that sends 6x25

**Answer: `send_equip_item_6x25` = 0x801BA5AC, signature `(TObjPlayer* player, uint32_t item_id, uint32_t
equip_slot)`. The Item Pack passes equip_slot 0 (UNKNOWN). It sends the 6x25 and the game's own send path runs the
6x25 handler locally, which equips through `TObjPlayer_equip_item` = 0x801BA3FC `(TObjPlayer*, TItem*, int slot)`.**

Found by searching text for `li rX,0x25` with `li rY,3` and two `stb` nearby: exactly one hit, the 6x25 constructor.

```
801C19A8  3800ffff  li r0, -1
801C19AC  38a00000  li r5, 0
801C19B0  90030004  stw r0, 4(r3)          item_id    = 0xFFFFFFFF
801C19B4  38800025  li r4, 0x25
801C19B8  38000003  li r0, 3
801C19BC  90a30008  stw r5, 8(r3)          equip_slot = 0
801C19C0  98830000  stb r4, 0(r3)          subcommand 0x25
801C19C4  98030001  stb r0, 1(r3)          size 3 words (= G_EquipItem_6x25, 0x0C bytes)
801C19C8  4e800020  blr
```

It has exactly two callers: 0x801BA534 (the receive handler) and 0x801BA5F0 (the sender).

The sender, 0x801BA5AC:
```
801BA5AC  9421ffd0  stwu r1, -0x30(r1)
801BA5BC  7c7d1b78  mr r29, r3             r29 = player
801BA5C0  7c9e2378  mr r30, r4             r30 = item_id
801BA5C4  7cbf2b78  mr r31, r5             r31 = equip_slot
801BA5C8  4bff6985  bl 0x801b0f4c          may-equip check (player, item_id); 0 -> return
801BA5D4  281d0000  cmplwi r29, 0
801BA5DC  7fa3eb78  mr r3, r29
801BA5E0  4be5edd5  bl 0x800193b4          returns (player+0x324 == 2); nonzero -> return
801BA5EC  38610008  addi r3, r1, 8
801BA5F0  480073b9  bl 0x801c19a8          6x25 constructor
801BA5F4  7fa3eb78  mr r3, r29
801BA5F8  4be5f031  bl 0x80019628          lhz r3,0x1c(r3): the player's client id
801BA5FC  b061000a  sth r3, 0xa(r1)        header.client_id
801BA604  93c1000c  stw r30, 0xc(r1)       item_id
801BA608  93e10010  stw r31, 0x10(r1)      equip_slot
801BA60C  480073c1  bl 0x801c19cc          byteswap + send (0x801E4BAC)
```
`0x801B0F4C` returns 1 unless a special mode applies (it first calls 0x801B13A4; when that is 0 it branches to
`801B102C li r3,1`); in that mode it looks the item up in the player's pack and checks it (0x80102E34).

The send path runs the subcommand locally as well:
```
801E4BC0  808dafc0  lwz r4, -0x5040(r13)   send queue
801E4BDC  4bff7ae5  bl 0x801dc6c0          queue for the network (if queue present)
801E4BE0  38000001  li r0, 1
801E4BE8  900db890  stw r0, -0x4770(r13)
801E4BEC  4bfff9f5  bl 0x801e45e0          local dispatch through the subcommand table
```
`0x801E45E0` walks the table at 0x804C5724 (entries `u16 subcommand, u16 flags, handler`) and calls the handler when
`flags & mask[state]` (mask bytes at 0x804C5700, index from `lwz r3,-0x45fc(r13)`) is nonzero. The 6x25 entry:
```
804C5924  00250006 801BA51C                subcommand 0x25, flags 0x06, handler 0x801BA51C
804C592C  00260006 801BA2C8                subcommand 0x26 (unequip), handler 0x801BA2C8
```
The mask table holds 0x04 or 0x02 for every state except one (0x01), so flags 0x06 dispatches in all but one state.

The handler 0x801BA51C (the same code that equips for a remote player's 6x25):
```
801BA55C  a061000a  lhz r3, 0xa(r1)        client_id
801BA560  480006a9  bl 0x801bac08          TObjPlayer_for_client_id
801BA56C  807f0d00  lwz r3, 0xd00(r31)     player's pack object
801BA570  8081000c  lwz r4, 0xc(r1)        item_id
801BA574  4801ae5d  bl 0x801d53d0          find pack node by item id
801BA580  4801b6a1  bl 0x801d5c20          node -> TItem*
801BA58C  80a10010  lwz r5, 0x10(r1)       equip_slot
801BA590  7fe3fb78  mr r3, r31
801BA594  4bfffe69  bl 0x801ba3fc          TObjPlayer_equip_item(player, item, slot)
```
`TObjPlayer_equip_item` 0x801BA3FC: unit (0x80178614) -> slot clamped to 9..12; otherwise slot = `item->vtable[0x84]()`
(the item's own slot). Then `old = 0x801C6C18(player+0xD04, slot)`; if `old` is another item it calls 0x801BA20C
(player, old) (the unequip that BugFixes g5 hooks), stores the item with 0x801BFDA8(player+0xD04, slot, item) and
calls `item->vtable[0x50]()`.

Who calls the sender: 0x80104880, 0x8021A05C, 0x8021B640, 0x80220400, 0x80277628, 0x802791F4. The four menu-style
sites do the same Sega sequence, e.g. 0x8021A008-0x8021A05C:
```
8021A00C  8061003c  lwz r3, 0x3c(r1)       item_id
8021A010  4bee7c91  bl 0x80101ca0          TItem* by id (global sorted list, compares +0xD8)
8021A01C  81830018  lwz r12, 0x18(r3)
8021A020  818c0084  lwz r12, 0x84(r12)     slot = item->vtable[0x84]()
8021A028  4e800421  bctrl
8021A02C  7c640774  extsb r4, r3
8021A030  807e0024  lwz r3, 0x24(r30)      player
8021A034  4bee8e4d  bl 0x80102e80          old = equipped item in that slot (player+0xD04)
8021A040  4be7fe81  bl 0x80099ec0          old->item_id (+0xD8)
8021A04C  4bfa034d  bl 0x801ba398          send 6x26 for the old item
8021A050  807e0024  lwz r3, 0x24(r30)      player
8021A054  38a00000  li r5, 0               equip_slot = UNKNOWN
8021A058  8081003c  lwz r4, 0x3c(r1)       item_id
8021A05C  4bfa0551  bl 0x801ba5ac          send 6x25
```
newserv's own comment (`src/ItemData.hh:13-16`) says the Item Pack sends UNKNOWN; these sites pass 0, consistent.
Which of the four is the Item Pack itself is not named in the binary; it does not matter: every 6x25 the client sends
goes through 0x801BA5AC (the constructor has no other sender).

`send_unequip_item_6x26` = 0x801BA398 `(TObjPlayer*, uint32_t item_id)`: same shape (0x800193B4 check, constructor
0x801C1904, client id, item id, send 0x801C1894), then 0x801B9AF0(player).

## 3. Question 2: from the local player to his TObjPlayer

**Answer: `TObjPlayer_for_client_id` = 0x801BAC08 `(uint32_t client_id) -> TObjPlayer*`, reading the array
`TObjPlayer_objs` at 0x8050FA30 (12 entries, bss).**

```
801BAC08  3c030000  addis r0, r3, 0
801BAC0C  2800ffff  cmplwi r0, 0xffff
801BAC10  4082000c  bne 0x801bac1c
801BAC14  38600000  li r3, 0               0xFFFF -> null
801BAC1C  2c030000  cmpwi r3, 0
801BAC20  4180000c  blt 0x801bac2c
801BAC24  2c03000c  cmpwi r3, 0xc
801BAC28  4180000c  blt 0x801bac34         0..11 only, else null
801BAC34  3c808051  lis r4, -0x7faf
801BAC38  5460103a  slwi r0, r3, 2
801BAC3C  3864fa30  addi r3, r4, -0x5d0    0x8050FA30
801BAC40  7c63002e  lwzx r3, r3, r0        TObjPlayer_objs[client_id]
801BAC44  4e800020  blr
```
Why this is the function: (a) 969 call sites, 144 of them directly after `lwz r3,-0x5220(r13)` (local_client_id per
`ExtendedItemInfo.s:174`), e.g.
```
80026F48  806dade0  lwz r3, -0x5220(r13)
80026F4C  48193cbd  bl 0x801bac08
```
(b) the 6x25 handler feeds it the subcommand's client_id (801BA55C-801BA560 above); (c) it sits 0x9FC after
g5_hook4_call on 3OP0 (0x801BAC08 - 0x801BA20C), exactly as on 3OE1 (0x801BA59C - 0x801B9BA0 = 0x9FC,
`Debug/PlayerFlags.s:12` and `BugFixes.s`); (d) the array 0x8050FA30 is 0x1020 above the 3OE2 TObjPlayer_objs
0x8050EA10 (`notes/ar-codes.txt:1027-1028`), same kind of version shift.

## 4. Question 3: the player's items

**Answer: the 13 pointers at TObjPlayer + 0xD04 are the EQUIPPED items, one per EquipSlot (0..12), not the pack. The
pack is the object at TObjPlayer + 0xD00; the game's own lookup is `find_pack_node_by_id` 0x801D53D0 `(pack,
item_id)` followed by `pack_node_item` 0x801D5C20 `(node) -> TItem*`. TItem + 0xD8 = item id is confirmed by its
getter.**

+0xD04 is a slot array of 13:
```
801C6C18  2c040000  cmpwi r4, 0            get(slots, slot): 0 <= slot < 13
801C6C20  2c04000d  cmpwi r4, 0xd
801C6C30  5480103a  slwi r0, r4, 2
801C6C34  7c63002e  lwzx r3, r3, r0
801BFDA8  5480103a  slwi r0, r4, 2         set(slots, slot, item)
801BFDAC  7ca3012e  stwx r5, r3, r0
801BFDB4  3800000d  li r0, 0xd             clear: 13 words to 0
```
callers pass `addi r3, player, 0xd04` (801BA474, 801BA48C, 80102E88, 802791C4).

The pack lookup:
```
801D53E0  908db828  stw r4, -0x47d8(r13)   wanted id
801D53E8  806301c4  lwz r3, 0x1c4(r3)      pack + 0x1C4: root of its item nodes
801D53EC  480007a1  bl 0x801d5b8c          recursive walk
801D5B9C  83e30014  lwz r31, 0x14(r3)      first child
801D5BB8  48000069  bl 0x801d5c20          node -> TItem*
801D5BC4  4bec42fd  bl 0x80099ec0          TItem item id
801D5BC8  800db828  lwz r0, -0x47d8(r13)
801D5BCC  7c001800  cmpw r0, r3            match -> return node
801D5BFC  83ff000c  lwz r31, 0xc(r31)      next sibling
801D5C34  8063001c  lwz r3, 0x1c(r3)       node + 0x1C = TItem* (returned if 0x80229EB0 says valid)
80099EC0  806300d8  lwz r3, 0xd8(r3)       TItem::item_id = +0xD8
```
The handler (801BA56C-801BA580) and the may-equip check (801B0F88-801B0FA4) use exactly this pair on +0xD00. The
function therefore does not need to walk pointers itself.

Not to use for ownership: 0x80101CA0 (the menu's lookup) is a binary search over a global sorted TItem list
(`r13-0x51CC`, count `r13-0x51D0`) and finds any item in the world, not only the local player's.

## 5. Sketch of SpireEquipItem (no code)

1. `player = TObjPlayer_for_client_id(lwz r13-0x5220)` (0x801BAC08); null -> return 0.
2. `node = 0x801D53D0(lwz player+0xD00, item_id)`; null -> return 0. `item = 0x801D5C20(node)`; null -> return 0.
   This is the "match on +0xD8" step, done by the game's own walker over the player's own pack.
3. Like the Sega menu sites: `slot = item->vtable[0x84]()` (extsb); `old = 0x801C6C18(player+0xD04, slot)`;
   `old == item` -> return 2 (already equipped); `old` non-null -> `0x801BA398(player, lwz old+0xD8)` (6x26).
   Weapons and mags only take this path; units choose slots 9-12 differently (0x80178614) and are not in § 3.
4. `0x801BA5AC(player, item_id, 0)`: sends 6x25 and, through 0x801E4BAC -> 0x801E45E0 -> 0x801BA51C, equips locally
   with 0x801BA3FC. Return 1. The function writes no item data.

Server side matches: newserv's `on_equip_item` -> `equip_item_index` treats slot 0 as "default slot" and unequips the
slot's occupant itself, so 6x26 + 6x25 and a lone 6x25 both leave newserv's copy equal to the client's.

## 6. Open (only Dolphin decides)

- **Re-entrancy of the local dispatch inside a B2 function call.** The send path 0x801E4BAC is normally entered from
  menu code; the function would enter it from the B2 handler. Breakpoint: Dolphin, GPOP8P, test character, send
  SpireEquipItem with a known weapon id; breakpoint on **0x801BA3FC**: hit with r3 = local TObjPlayer and r4 = the
  TItem whose +0xD8 is the id, and the game continues without freezing.
- **Silent no-send cases in 0x801BA5AC:** 0x801B0F4C in its special mode (0x801B13A4 nonzero) and `player+0x324 == 2`.
  The meaning of those two states is not named statically; the same breakpoint (not hit) would show it.
- Which of the menu sites is the Item Pack itself: not needed for the function, not resolved.
