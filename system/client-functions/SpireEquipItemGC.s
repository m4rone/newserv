.meta key="SpireEquipItem"
.meta name="Spire equip item"
.meta description="Equips one item through\nthe game's own equip path"
.meta visibility="hidden"

# Spire (route B bundle 9, ROUTEB9-PROTOCOL.md § 3): newserv sends this function with one item id (label item_id) after
# 'C' 13 (the kit weapon) or 'C' 14 (the run mag), and on a resume for each item that was equipped at the save point. It finds that item in the local player's own pack and equips it the
# way Sega's menu sites do (e.g. 0x8021A008-0x8021A05C): the item's own slot, a 6x26 for the item that is in that slot
# now, then the 6x25 sender. The 6x25 sender sends the subcommand and its send path runs the 6x25 handler locally,
# which equips through TObjPlayer_equip_item (0x801BA3FC, not called here). So the client sends its own 6x25 and 6x26
# and newserv's copy of the inventory stays the client's. This function never writes item data.
#
# Every address and offset below is proven statically on PsoV3.dol of GPOP8P in ROUTEB9-NEWSERV-F2-REPORT.md (cited as
# F2 § n). Only GC EU v1.0 (3OP0) has these addresses; the server also gates on the specific version.
#
# Returns in r3 (the server logs it from the B3):
#   0 = nothing done: no local TObjPlayer, no pack, or the item id is not in the local player's pack
#   1 = 6x25 sent: through the game's sender (that sender can still decline silently, F2 § 6), or for a mag through
#       this function's own copy of that sender's steps (see below)
#   2 = nothing done: the item is already equipped in its slot
#   3 = nothing done: the item is no weapon, frame, barrier, unit or mag, or the slot does not fit it
#   4 = nothing done: a mag while player + 0x324 == 2 (0x800193B4), where the game's own sender sends nothing either
#
# ux-1 (observaties 291, 2026-11-14): also frames, barriers and units, for the resume (newserv spire_resume_equip). A
# frame or a barrier takes the path of a weapon: its own slot from vtable[0x84], the Item Pack's equip_slot 0, and
# TObjPlayer_equip_item puts it in that slot again (F2 § 2). A unit has four slots (9-12), and TObjPlayer_equip_item
# clamps an equip_slot outside 9..12 to 9 (801BA428-801BA438 on PsoV3.dol of GPOP8P, ux-1 static read), so the Item
# Pack's 0 would always push out the unit in slot 9. newserv therefore writes the unit's slot (label equip_slot, 9..12)
# and the function does what Sega's Equip menu routine 0x80277584 does with its explicit slot (80277584-80277628): a
# 6x26 for the item in that slot now, then the 6x25 with that slot. equip_slot 0 for a unit, or a slot for anything
# else, is refused with 3, so a unit never lands in slot 9 by default.
#
# Marck's run (2026-11-14, B-REGIE-22-32/34): every Spire rule block carries ba_set_forbid_mags 0, which the game
# stores as mag rule 1 (FORBID_ALL; rules object 0x8050FA60, byte 3, getter 0x801D7658). The game's 6x25 sender first
# calls its may-equip check 0x801B0F4C, and under that rule the check returns 0 for every item of kind 2 (801B0FCC-
# 801B1028), so the sender sends nothing for any mag, also from the Item Pack. For a mag this function therefore does
# the sender's own steps after that check (801BA5D4-801BA60C): no 6x25 while 0x800193B4 says player + 0x324 == 2, the
# 6x25 constructor 0x801C19A8 on a 12-byte buffer at r1 + 8, the player's client id (0x80019628) at buffer + 2, the
# item id at + 4, equip_slot at + 8, and the byteswap-and-send 0x801C19CC, whose send path runs the 6x25 handler
# 0x801BA51C locally (F2 § 2; that handler and TObjPlayer_equip_item never read the rule). Every other kind keeps the
# game's sender. The rule itself stays: the game wipes every mag of the pack once when the rules are installed
# (0x801B1044 -> 0x801D5114 kind 2, in the same pass that clears weapons and armour under weapon rule 1), and the drop
# generator 0x80106094 makes no mags; those three sites are the only readers of the rule byte.

.versions 3OP0

  .label   TObjPlayer_for_client_id, 0x801BAC08  # F2 § 3: (uint32_t client_id) -> TObjPlayer*, null outside 0..11
  .label   find_pack_node_by_id, 0x801D53D0  # F2 § 4 (and the 6x25 handler 801BA56C-801BA574, F2 § 2): (pack, item_id) -> node
  .label   pack_node_item, 0x801D5C20  # F2 § 4 (handler 801BA580, F2 § 2): (node) -> TItem*
  .label   equip_slots_get, 0x801C6C18  # F2 § 4: (player + 0xD04, slot) -> equipped TItem* or null, slot 0..12
  .label   send_unequip_item_6x26, 0x801BA398  # F2 § 2 (menu site 8021A04C): (TObjPlayer*, uint32_t item_id)
  .label   send_equip_item_6x25, 0x801BA5AC  # F2 § 2: (TObjPlayer*, uint32_t item_id, uint32_t equip_slot)
  .label   player_state_is_2, 0x800193B4  # 801BA5E0: (TObjPlayer*) -> (player + 0x324 == 2); nonzero: no 6x25
  .label   construct_6x25, 0x801C19A8  # 801BA5F0: (buffer) -> 0x25, size 3, item_id -1, equip_slot 0
  .label   player_client_id, 0x80019628  # 801BA5F8: (TObjPlayer*) -> lhz + 0x1C, the client id
  .label   send_6x25, 0x801C19CC  # 801BA60C: (buffer) byteswap and send (0x801E4BAC), handled locally too

entry_ptr:
reloc0:
  .data    start
start:
  stwu     [r1 - 0x30], r1               # r1 + 8 .. r1 + 0x13: a mag's 6x25 (send_mag_6x25), below the saved registers
  mflr     r0
  stw      [r1 + 0x34], r0
  stmw     [r1 + 0x1C], r27              # r27 = equip_slot, r28 = item, r29 = item_id, r30 = player, r31 = slot

  bl       get_args
item_id:
  .data    0                             # written by newserv (SPIRE_EQUIP_ITEM_ID_LABEL)
equip_slot:
  .data    0                             # written by newserv (SPIRE_EQUIP_SLOT_LABEL): 0, or 9..12 for a unit
get_args:
  mflr     r3
  lwz      r29, [r3]
  lwz      r27, [r3 + 4]

  # player = TObjPlayer_for_client_id(local_client_id)
  lwz      r3, [r13 - 0x5220]            # local_client_id: F2 § 3 (144 call sites, e.g. 80026F48) and ExtendedItemInfo.s:174 (3OP0)
  lis      r0, high_word(TObjPlayer_for_client_id)
  ori      r0, r0, low_word(TObjPlayer_for_client_id)
  mtctr    r0
  bctrl
  mr.      r30, r3
  beq      return_0

  # node = find_pack_node_by_id(player->pack, item_id): the game's own walk over this player's own pack, matching the
  # item id at TItem + 0xD8 (F2 § 4). Not the global item list 0x80101CA0, which finds any item in the world.
  lwz      r3, [r30 + 0xD00]             # TObjPlayer + 0xD00 = the pack object: F2 § 2 (handler 801BA56C) and § 4
  cmplwi   r3, 0
  beq      return_0
  mr       r4, r29
  lis      r0, high_word(find_pack_node_by_id)
  ori      r0, r0, low_word(find_pack_node_by_id)
  mtctr    r0
  bctrl
  cmplwi   r3, 0
  beq      return_0
  lis      r0, high_word(pack_node_item)
  ori      r0, r0, low_word(pack_node_item)
  mtctr    r0
  bctrl
  mr.      r28, r3
  beq      return_0

  # The kind: TItem + 0xEC is data1[0], + 0xED data1[1] (ExtendedItemInfo.s:137 for 3OP0; the unit check 0x80178614
  # that F2 § 2 names in TObjPlayer_equip_item reads lbz +0xEC == 1 and lbz +0xED == 3). 0 = weapon, 2 = mag,
  # 1 with 1 = frame, 2 = barrier, 3 = unit.
  lbz      r0, [r28 + 0xEC]
  cmplwi   r0, 0
  beq      own_slot_kind
  cmplwi   r0, 2
  beq      own_slot_kind
  cmplwi   r0, 1
  bne      return_3
  lbz      r0, [r28 + 0xED]
  cmplwi   r0, 1
  beq      own_slot_kind
  cmplwi   r0, 2
  beq      own_slot_kind
  cmplwi   r0, 3
  bne      return_3

  # A unit: only with a unit slot from newserv, as Sega's Equip menu passes it (0x80277584)
  cmplwi   r27, 9
  blt      return_3
  cmplwi   r27, 12
  bgt      return_3
  mr       r31, r27
  b        have_slot

own_slot_kind:
  # A weapon, frame, barrier or mag: no slot from newserv (the Item Pack's equip_slot 0)
  cmplwi   r27, 0
  bne      return_3
  # slot = item->vtable[0x84]() (extsb): F2 § 2, menu site 8021A01C-8021A02C and TObjPlayer_equip_item 801BA45C-801BA46C
  mr       r3, r28
  lwz      r12, [r28 + 0x18]             # TItem vtable at +0x18: F2 § 2 (8021A01C)
  lwz      r12, [r12 + 0x84]
  mtctr    r12
  bctrl
  extsb    r31, r3

have_slot:

  # old = equip_slots_get(player + 0xD04, slot): F2 § 4 (callers pass addi r3, player, 0xD04)
  addi     r3, r30, 0xD04
  mr       r4, r31
  lis      r0, high_word(equip_slots_get)
  ori      r0, r0, low_word(equip_slots_get)
  mtctr    r0
  bctrl
  cmplwi   r3, 0
  beq      send_equip
  cmpl     r3, r28
  beq      return_2                      # already equipped in its slot: nothing to send

  # A 6x26 for the item in that slot now, as the menu sites do (8021A034-8021A04C, F2 § 2)
  lwz      r4, [r3 + 0xD8]               # old->item_id: TItem + 0xD8 (F2 § 4, getter 80099EC0)
  mr       r3, r30
  lis      r0, high_word(send_unequip_item_6x26)
  ori      r0, r0, low_word(send_unequip_item_6x26)
  mtctr    r0
  bctrl

send_equip:
  # A mag: past the game's may-equip check, which refuses every mag under the Spire's mag rule (header)
  lbz      r0, [r28 + 0xEC]
  cmplwi   r0, 2
  beq      send_mag_6x25
  # send_equip_item_6x25(player, item_id, equip_slot): 0 = UNKNOWN, as the Item Pack sites pass (8021A054, F2 § 2); a
  # unit's slot 9..12, as the Equip menu routine passes it (80277624 mr r5, r28)
  mr       r3, r30
  mr       r4, r29
  mr       r5, r27
  lis      r0, high_word(send_equip_item_6x25)
  ori      r0, r0, low_word(send_equip_item_6x25)
  mtctr    r0
  bctrl
  li       r3, 1
  b        return

send_mag_6x25:
  # The game's sender after its may-equip check, 801BA5DC-801BA60C, step by step
  mr       r3, r30
  lis      r0, high_word(player_state_is_2)
  ori      r0, r0, low_word(player_state_is_2)
  mtctr    r0
  bctrl
  cmpwi    r3, 0
  bne      return_4
  addi     r3, r1, 8
  lis      r0, high_word(construct_6x25)
  ori      r0, r0, low_word(construct_6x25)
  mtctr    r0
  bctrl
  mr       r3, r30
  lis      r0, high_word(player_client_id)
  ori      r0, r0, low_word(player_client_id)
  mtctr    r0
  bctrl
  sth      [r1 + 0x0A], r3               # header.client_id
  stw      [r1 + 0x0C], r29              # item_id
  stw      [r1 + 0x10], r27              # equip_slot: 0, as the Item Pack passes it
  addi     r3, r1, 8
  lis      r0, high_word(send_6x25)
  ori      r0, r0, low_word(send_6x25)
  mtctr    r0
  bctrl
  li       r3, 1
  b        return

return_0:
  li       r3, 0
  b        return
return_2:
  li       r3, 2
  b        return
return_3:
  li       r3, 3
  b        return
return_4:
  li       r3, 4
return:
  lmw      r27, [r1 + 0x1C]
  lwz      r0, [r1 + 0x34]
  addi     r1, r1, 0x30
  mtlr     r0
  blr
