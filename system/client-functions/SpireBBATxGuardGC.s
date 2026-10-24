.meta key="SpireBBATxGuard"
.meta name="Spire BBA TX guard"
.meta description="Null-checks the BBA\nTX-done callback"
.meta visibility="hidden"

# Spire (2026-10-24, spoor-a-hangdump, output/hang-dumps-vergelijking.md 2.2-2.5): three identical MEM1 dumps show the
# GC BBA driver's interrupt handler taking the "TX done" path twice for one frame; the second time the callback slot at
# r13-0x296C is already empty and `blrl` jumps to address 0 (PPCHalt). The driver's RX paths null-check their callback
# slots, the TX path does not. This function rewrites the 8 words at 0x80429A78 so an empty slot is ignored:
#
#   before                                  after
#   cmpwi  r29,0 ; beq epilogue             lwz    r12,-0x296c(r13) ; mullw r12,r12,r29  (0 if no TX event or no callback)
#   lwz    r12,-0x296c(r13) ; li r0,0       cmplwi r12,0 ; beq epilogue
#   addi   r3,r28,0 ; stw r0,-0x296c(r13)   subf   r0,r29,r29 (= 0) ; stw r0,-0x296c(r13)
#   mtlr   r12 ; blrl                       mtlr   r12 ; blrl
#
# The LTPS argument (r3) is dropped: the callback (0x80424D94) only forwards it as message content that the TX thread
# never reads. Only GC EU v1.0 (3OP0, GPOP8P) has these addresses; the server also gates on the specific version.
# Returns in r3: 1 = patch written, 2 = patch was already present, 0 = the code at the address matched neither
# (nothing written). The server logs that value from the B3.

.versions 3OP0

entry_ptr:
reloc0:
  .data    start
start:
  mflr     r12
  bl       get_data_ptr
get_data_ptr_ret:
  mflr     r11                       # r11 = original words; r11 + 0x20 = patched words

  lis      r10, 0x8042
  ori      r10, r10, 0x9A78          # r10 = patch address

  # Does the code still hold the original words?
  mr       r3, r10
  mr       r4, r11
  bl       compare_words
  cmplwi   r3, 0
  bne      check_already_patched

  mr       r3, r10
  addi     r4, r11, 0x20
  li       r5, 0x20
  bl       copy_code                 # writes the 8 words and flushes the data and instruction caches
  li       r3, 1
  mtlr     r12
  blr

check_already_patched:
  mr       r3, r10
  addi     r4, r11, 0x20
  bl       compare_words
  cmplwi   r3, 0
  bne      no_match
  li       r3, 2
  mtlr     r12
  blr

no_match:
  li       r3, 0
  mtlr     r12
  blr

# compare_words: r3 = code pointer, r4 = expected words; returns r3 = 0 if all 8 words are equal, 1 otherwise
compare_words:
  li       r5, 8
  mtctr    r5
  subi     r3, r3, 4
  subi     r4, r4, 4
compare_again:
  lwzu     r6, [r3 + 4]
  lwzu     r7, [r4 + 4]
  xor      r0, r6, r7
  cmplwi   r0, 0
  bne      compare_differ
  bdnz     compare_again
  li       r3, 0
  blr
compare_differ:
  li       r3, 1
  blr

copy_code:
  .include CopyCode
  blr

get_data_ptr:
  bl       get_data_ptr_ret
data:
  # original words at 0x80429A78 (GPOP8P, read from the dumps of 2026-10-24)
  .data    0x2C1D0000
  .data    0x4182001C
  .data    0x818DD694
  .data    0x38000000
  .data    0x387C0000
  .data    0x900DD694
  .data    0x7D8803A6
  .data    0x4E800021
  # patched words (encoding checked with capstone, see SpireBBATxGuard.hh)
  .data    0x818DD694
  .data    0x7D8CE9D6
  .data    0x280C0000
  .data    0x41820014
  .data    0x7C1DE850
  .data    0x900DD694
  .data    0x7D8803A6
  .data    0x4E800021
