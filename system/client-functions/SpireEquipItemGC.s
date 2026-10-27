.meta key="SpireEquipItem"
.meta name="Spire equip item"
.meta description="Equips one weapon or\nmag through the game's\nown equip path"
.meta visibility="hidden"

# Spire (route B bundle 9, ROUTEB9-PROTOCOL.md § 3): newserv sends this function with one item id (label item_id) after
# 'C' 13 (the kit weapon) or 'C' 14 (the run mag). It finds that item in the local player's own pack and equips it the
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
#   1 = 6x25 sent through the game's sender (that sender can still decline silently, F2 § 6)
#   2 = nothing done: the item is already equipped in its slot
#   3 = nothing done: the item is neither a weapon nor a mag

.versions 3OP0

  .label   TObjPlayer_for_client_id, 0x801BAC08  # F2 § 3: (uint32_t client_id) -> TObjPlayer*, null outside 0..11
  .label   find_pack_node_by_id, 0x801D53D0  # F2 § 4 (and the 6x25 handler 801BA56C-801BA574, F2 § 2): (pack, item_id) -> node
  .label   pack_node_item, 0x801D5C20  # F2 § 4 (handler 801BA580, F2 § 2): (node) -> TItem*
  .label   equip_slots_get, 0x801C6C18  # F2 § 4: (player + 0xD04, slot) -> equipped TItem* or null, slot 0..12
  .label   send_unequip_item_6x26, 0x801BA398  # F2 § 2 (menu site 8021A04C): (TObjPlayer*, uint32_t item_id)
  .label   send_equip_item_6x25, 0x801BA5AC  # F2 § 2: (TObjPlayer*, uint32_t item_id, uint32_t equip_slot)

entry_ptr:
reloc0:
  .data    start
start:
  stwu     [r1 - 0x20], r1
  mflr     r0
  stw      [r1 + 0x24], r0
  stmw     [r1 + 0x10], r28              # r28 = item, r29 = item_id, r30 = player, r31 = slot

  bl       get_item_id
item_id:
  .data    0                             # written by newserv (SPIRE_EQUIP_ITEM_ID_LABEL)
get_item_id:
  mflr     r3
  lwz      r29, [r3]

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

  # Weapons and mags only: TItem + 0xEC is data1[0] (ExtendedItemInfo.s:137 for 3OP0; the unit check 0x80178614 that
  # F2 § 2 names in TObjPlayer_equip_item reads lbz +0xEC == 1 and lbz +0xED == 3). 0 = weapon, 2 = mag.
  lbz      r0, [r28 + 0xEC]
  cmplwi   r0, 0
  beq      type_ok
  cmplwi   r0, 2
  bne      return_3
type_ok:

  # slot = item->vtable[0x84]() (extsb): F2 § 2, menu site 8021A01C-8021A02C and TObjPlayer_equip_item 801BA45C-801BA46C
  mr       r3, r28
  lwz      r12, [r28 + 0x18]             # TItem vtable at +0x18: F2 § 2 (8021A01C)
  lwz      r12, [r12 + 0x84]
  mtctr    r12
  bctrl
  extsb    r31, r3

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
  # send_equip_item_6x25(player, item_id, 0): equip_slot 0 = UNKNOWN, as the menu sites pass (8021A054, F2 § 2)
  mr       r3, r30
  mr       r4, r29
  li       r5, 0
  lis      r0, high_word(send_equip_item_6x25)
  ori      r0, r0, low_word(send_equip_item_6x25)
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
return:
  lmw      r28, [r1 + 0x10]
  lwz      r0, [r1 + 0x24]
  addi     r1, r1, 0x20
  mtlr     r0
  blr
