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

  phosg::log_info_f("-- Spire guard: login confirmation compares the items (v31)");
  {
    auto make = [](size_t n, uint32_t id_base) {
      PlayerInventory inv;
      inv.num_items = n;
      for (size_t z = 0; z < n; z++) {
        inv.items[z].data.data1[0] = 0x00;
        inv.items[z].data.data1[1] = 0x01 + z; // a different weapon per slot
        inv.items[z].data.data1[4] = z;
        inv.items[z].data.data2[0] = 0x10 + z;
        inv.items[z].data.id = id_base + z;
      }
      return inv;
    };
    auto real = make(20, 0x00010000);
    // same items after another login: other item IDs, other order
    auto relogin = make(20, 0x00210000);
    std::swap(relogin.items[0], relogin.items[19]);
    expect(spire_guard_same_inventory(relogin, 0, real, 0));
    // lab 18:11 BidX: same level and EXP, but the run's 7 items on the card
    expect(!spire_guard_same_inventory(make(7, 0x00210000), 0, real, 0));
    // same count, one item different
    auto other = make(20, 0x00210000);
    other.items[5].data.data1[2] = 0x09;
    expect(!spire_guard_same_inventory(other, 0, real, 0));
    // same items, other meseta
    expect(!spire_guard_same_inventory(relogin, 100, real, 0));
    expect(spire_guard_same_inventory(make(0, 0), 5, make(0, 0), 5));
  }

  phosg::log_info_f("-- Spire guard: a run start under AwaitingSave keeps the snapshot unless the data equals it (v32)");
  {
    // arguments: awaiting_save, lower_level, same_state
    expect(!spire_guard_run_start_keeps_snapshot(false, false, false)); // no open marker: fresh snapshot
    expect(spire_guard_run_start_keeps_snapshot(true, true, false));    // lower level: keep (v10)
    expect(!spire_guard_run_start_keeps_snapshot(true, false, true));   // same data: the fresh snapshot is the same
    expect(spire_guard_run_start_keeps_snapshot(true, false, false));   // lab 18:14: same level, the run's 7 items: keep
  }

  phosg::log_info_f("-- Spire guard: the 61 after a push verifies only with the snapshot's items (v32)");
  {
    // arguments: same_stats (level and EXP), same_items (items and meseta)
    expect(spire_guard_push_verified(true, true));
    expect(!spire_guard_push_verified(false, true));
    expect(!spire_guard_push_verified(false, false));
    expect(!spire_guard_push_verified(true, false)); // same level and EXP, other items: not verified
  }

  phosg::log_info_f("-- Spire guard: same state means level, EXP, meseta and items (v32)");
  {
    auto make = [](size_t n, uint32_t level, uint32_t exp, uint32_t meseta) {
      std::pair<PlayerInventory, PlayerDispDataV4> ret;
      ret.first.num_items = n;
      for (size_t z = 0; z < n; z++) {
        ret.first.items[z].data.data1[1] = 0x01 + z;
        ret.first.items[z].data.id = 0x00010000 + z;
      }
      ret.second.stats.level = level;
      ret.second.stats.exp = exp;
      ret.second.stats.meseta = meseta;
      return ret;
    };
    auto snapshot = make(20, 51, 370758, 0);
    auto same = make(20, 51, 370758, 0);
    std::swap(same.first.items[0], same.first.items[19]);
    expect(spire_guard_same_state(same.first, same.second, snapshot.first, snapshot.second));
    auto run = make(7, 51, 370758, 0); // lab 18:11 BidX
    expect(!spire_guard_same_state(run.first, run.second, snapshot.first, snapshot.second));
    auto meseta = make(20, 51, 370758, 300);
    expect(!spire_guard_same_state(meseta.first, meseta.second, snapshot.first, snapshot.second));
    auto exp = make(20, 51, 370759, 0);
    expect(!spire_guard_same_state(exp.first, exp.second, snapshot.first, snapshot.second));
    auto level = make(20, 52, 370758, 0);
    expect(!spire_guard_same_state(level.first, level.second, snapshot.first, snapshot.second));
  }

  phosg::log_info_f("-- Spire guard: the lobby restore waits for the 99 after the 67 and the B1 (v33)");
  {
    using E = SpireGuardLobbyGate::Event;
    SpireGuardLobbyGate gate;
    expect(!gate.on_lobby_loaded());  // a 99 before the 67 went out does not count
    expect(!gate.on_timeout());
    gate.arm();                       // the 67 and the B1 went out
    expect_eq(E::NONE, gate.fired);   // the restore does not start with the 67 itself (the v28 order)
    expect(gate.on_lobby_loaded());   // the 99 starts the restore
    expect_eq(E::LOBBY_LOADED, gate.fired);
    expect(!gate.on_lobby_loaded());  // only once
    expect(!gate.on_timeout());       // the timer that fires later starts nothing

    SpireGuardLobbyGate slow;
    slow.arm();
    expect(slow.on_timeout());        // no 99 within the timeout: the restore starts anyway
    expect_eq(E::TIMEOUT, slow.fired);
    expect(!slow.on_lobby_loaded());  // a 99 that arrives later starts nothing

    slow.arm();                       // the next lobby join arms it again
    expect_eq(E::NONE, slow.fired);
    expect(slow.on_lobby_loaded());
  }

  phosg::log_info_f("-- All static tests passed");
}
