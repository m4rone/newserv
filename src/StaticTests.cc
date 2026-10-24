#include <phosg/UnitTest.hh>

#include "QuestScript.hh"
#include "RareItemSet.hh"
#include "SpireFlagBridge.hh"

void run_static_tests() {
  phosg::log_info_f("-- Quest opcode definitions");
  check_default_quest_opcode_definitions();

  phosg::log_info_f("-- RareItemSet rate calculations");
  for (size_t z = 0; z < 0x100; z++) {
    uint32_t expanded = RareItemSet::expand_rate(z);
    uint8_t reencoded = RareItemSet::compress_rate(expanded);
    if (z == 0) {
      expect_eq(reencoded, 0);
    } else if ((reencoded < 0x28) && (z < 0x28)) {
      if ((z & 7) == 0) {
        expect_eq(1, reencoded & 7);
      } else {
        expect_eq(z & 7, reencoded & 7);
      }
    } else if (z == 0x28) {
      expect_eq(7, reencoded);
    } else if ((z & 7) == 0) {
      expect_eq(z - 1, reencoded);
    } else {
      expect_eq(z, reencoded);
    }
  }

  phosg::log_info_f("-- Spire guard: Battle game without a quest (v30)");
  {
    using A = SpireNoQuestAction;
    // arguments: client_in_game, battle_game, has_quest, quest_selection_in_progress, can_run_functions
    expect_eq(A::EXIT, spire_guard_no_quest_action(true, true, false, false, true));   // stranded: back to the lobby
    expect_eq(A::TELL, spire_guard_no_quest_action(true, true, false, false, false));  // no functions: text, guard stays
    expect_eq(A::WAIT, spire_guard_no_quest_action(true, true, false, true, true));    // someone is picking a quest
    expect_eq(A::NONE, spire_guard_no_quest_action(true, true, true, false, true));    // q920/q921 loaded: leave it
    expect_eq(A::NONE, spire_guard_no_quest_action(false, true, false, false, true));  // already left the game
    expect_eq(A::NONE, spire_guard_no_quest_action(true, false, false, false, true));  // normal game: not ours
    expect_eq(A::NONE, spire_guard_no_quest_action(false, false, true, true, false));
  }

  phosg::log_info_f("-- All static tests passed");
}
