#include <phosg/Filesystem.hh>
#include <phosg/UnitTest.hh>

#include <filesystem>
#include <format>

#include "DataIndex.hh"
#include "ExpressionEnvironment.hh"
#include "QuestScript.hh"
#include "RareItemSet.hh"
#include "ClientFunctionIndex.hh"
#include "SpireBBATxGuard.hh"
#include "SpireDraw.hh"
#include "SpireFlagBridge.hh"
#include "SpireMeta.hh"

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

  phosg::log_info_f("-- Spire PvP: player targets stripped from attacks, support techs untouched (v42)");
  {
    auto make_targets = []() {
      // two players (C-0, C-1), an enemy and an object; the hitbox index must survive the strip
      return std::vector<TargetEntry>{{0x0001, 0x0003}, {0x1005, 0x0002}, {0x4002, 0x0001}, {0x0000, 0x0000}};
    };
    auto expect_enemy_and_object_kept = [](const std::vector<TargetEntry>& targets) {
      expect_eq(2, targets.size());
      expect_eq(0x1005, targets[0].entity_id);
      expect_eq(0x0002, targets[0].hitbox_index);
      expect_eq(0x4002, targets[1].entity_id);
      expect_eq(0x0001, targets[1].hitbox_index);
    };

    // 6x46 attack strikes: players out, count and size follow
    {
      G_AttackFinished_Header_6x46 h;
      h.header = {0x46, (sizeof(h) + 4 * 4) / 4, 0x0000};
      h.target_count = 4;
      auto targets = make_targets();
      expect_eq(2, spire_pvp_strip_player_targets(h, targets));
      expect_enemy_and_object_kept(targets);
      expect_eq(2, h.target_count);
      expect_eq((sizeof(h) + 2 * 4) / 4, h.header.size);
    }
    // 6x46 with only the partner as target: forwarded with zero targets (the swing still shows)
    {
      G_AttackFinished_Header_6x46 h;
      h.header = {0x46, (sizeof(h) + 4) / 4, 0x0000};
      h.target_count = 1;
      std::vector<TargetEntry> targets{{0x0001, 0x0000}};
      expect_eq(1, spire_pvp_strip_player_targets(h, targets));
      expect_eq(0, targets.size());
      expect_eq(0, h.target_count);
      expect_eq(sizeof(h) / 4, h.header.size);
    }
    // 6x46 on enemies only: nothing changes
    {
      G_AttackFinished_Header_6x46 h;
      h.header = {0x46, (sizeof(h) + 4) / 4, 0x0000};
      h.target_count = 1;
      std::vector<TargetEntry> targets{{0x1005, 0x0002}};
      expect_eq(0, spire_pvp_strip_player_targets(h, targets));
      expect_eq(1, targets.size());
      expect_eq(1, h.target_count);
      expect_eq((sizeof(h) + 4) / 4, h.header.size);
    }
    // 6x47 attack techs (Foie through Razonde, Grants, Jellen, Zalure, Megid): players out
    for (uint8_t tech : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 18}) {
      expect(!spire_pvp_tech_is_support(tech));
      G_CastTechnique_Header_6x47 h;
      h.header = {0x47, (sizeof(h) + 4 * 4) / 4, 0x0000};
      h.technique_number = tech;
      h.level = 15;
      h.target_count = 4;
      auto targets = make_targets();
      expect_eq(2, spire_pvp_strip_player_targets(h, targets));
      expect_enemy_and_object_kept(targets);
      expect_eq(2, h.target_count);
      expect_eq((sizeof(h) + 2 * 4) / 4, h.header.size);
      expect_eq(tech, h.technique_number);
      expect_eq(15, h.level);
    }
    // 6x47 support techs cast on the partner (Deband, Shifta, Ryuker, Resta, Anti, Reverser): untouched
    for (const char* name : {"Resta", "Shifta", "Deband", "Anti", "Reverser", "Ryuker"}) {
      uint8_t tech = technique_for_name(name);
      expect(spire_pvp_tech_is_support(tech));
      G_CastTechnique_Header_6x47 h;
      h.header = {0x47, (sizeof(h) + 4 * 4) / 4, 0x0000};
      h.technique_number = tech;
      h.level = 15;
      h.target_count = 4;
      auto targets = make_targets();
      expect_eq(0, spire_pvp_strip_player_targets(h, targets));
      expect_eq(4, targets.size());
      expect_eq(0x0001, targets[0].entity_id);
      expect_eq(0x0003, targets[0].hitbox_index);
      expect_eq(0x0000, targets[3].entity_id);
      expect_eq(4, h.target_count);
      expect_eq((sizeof(h) + 4 * 4) / 4, h.header.size);
    }
    // 6x49 photon blast: players out
    {
      G_ExecutePhotonBlast_Header_6x49 h;
      h.header = {0x49, (sizeof(h) + 4 * 4) / 4, 0x0000};
      h.pb_number = 3;
      h.target_count = 4;
      h.pb_meter_value = 100;
      auto targets = make_targets();
      expect_eq(2, spire_pvp_strip_player_targets(h, targets));
      expect_enemy_and_object_kept(targets);
      expect_eq(2, h.target_count);
      expect_eq((sizeof(h) + 2 * 4) / 4, h.header.size);
      expect_eq(3, h.pb_number);
      expect_eq(100, h.pb_meter_value);
    }
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

  phosg::log_info_f("-- Spire guard: a run start under AwaitingSave keeps the snapshot unless the data equals it (v32, v39)");
  {
    // arguments: awaiting_save, same_state (v39: no level argument; a lower and a higher level both mean !same_state)
    expect(!spire_guard_run_start_keeps_snapshot(false, false)); // no open marker: fresh snapshot
    expect(!spire_guard_run_start_keeps_snapshot(true, true));   // same data: the fresh snapshot is the same
    expect(spire_guard_run_start_keeps_snapshot(true, false));   // lab 18:14: the run's 7 items, or another level: keep
  }

  phosg::log_info_f("-- Spire guard: a run copy stronger than the real character never becomes the snapshot (v39, v40)");
  {
    // BidX Lv52 (level 51 zero-based) with 20 items is the real character; the run copy is Lv150 with more EXP, more
    // meseta, other items and other material counters (start level, training rooms)
    auto make = [](size_t n, uint8_t kind, uint32_t level, uint32_t exp, uint32_t meseta, uint16_t power) {
      std::pair<PlayerInventory, PlayerDispDataV4> ret;
      ret.first.num_items = n;
      for (size_t z = 0; z < n; z++) {
        ret.first.items[z].data.data1[0] = kind;
        ret.first.items[z].data.data1[1] = 0x01 + z;
        ret.first.items[z].data.id = 0x00010000 + z;
      }
      ret.first.items[8].extension_data2 = power;
      ret.second.stats.level = level;
      ret.second.stats.exp = exp;
      ret.second.stats.meseta = meseta;
      return ret;
    };
    auto real = make(20, 0x00, 51, 370758, 1000, 125);
    auto copy = make(30, 0x01, 149, 9000000, 50000, 0);
    auto stored = [&](bool run_in_progress, bool restore_pending, bool awaiting_save) {
      SpireGuardSnapshot ret;
      ret.run_in_progress = run_in_progress;
      ret.restore_pending = restore_pending;
      ret.awaiting_save = awaiting_save;
      ret.has_character = true;
      ret.quest_number = 920;
      ret.snapshot_time_usecs = 1;
      ret.inventory = real.first;
      ret.disp = real.second;
      return ret;
    };
    auto is_real = [&](const SpireGuardSnapshot& snap) {
      return snap.has_character && (snap.disp.stats.level == 51) && (snap.disp.stats.exp == 370758) &&
          spire_guard_same_state(snap.inventory, snap.disp, real.first, real.second) && (snap.snapshot_time_usecs == 1);
    };
    // the server's character data holds the run copy at a run start under each open marker: the Lv52 snapshot stays
    for (auto [run, restore, awaiting] : std::vector<std::tuple<bool, bool, bool>>{
             {true, false, false}, {false, true, false}, {true, true, false}, {false, false, true}}) {
      auto snap = stored(run, restore, awaiting);
      auto action = spire_guard_run_start_apply(snap, copy.first, copy.second, 921, 2);
      expect(action != SpireGuardRunStart::FRESH);
      expect(is_real(snap));
      expect(snap.run_in_progress || snap.restore_pending); // a marker stays open
    }
    {
      auto snap = stored(false, false, true);
      expect_eq(SpireGuardRunStart::KEPT_UNCONFIRMED, spire_guard_run_start_apply(snap, copy.first, copy.second, 921, 2));
      expect(snap.run_in_progress && !snap.awaiting_save);
      expect_eq(921u, snap.quest_number);
    }
    // a login 61 with the Lv150 run copy under AwaitingSave (no fingerprint kept, v14): no confirmation, push again
    // (the v12 rule "more EXP than the snapshot is real progress" took the copy as the real character and cleared the
    // marker, so the next run start would have taken its snapshot of the copy)
    {
      auto snap = stored(false, false, true);
      expect(!spire_guard_awaiting_save_login_apply(snap, copy.first, copy.second));
      expect(snap.restore_pending && !snap.awaiting_save);
      expect(is_real(snap));
      // and the run start that follows while the push is still open keeps the Lv52 snapshot
      expect_eq(SpireGuardRunStart::KEPT_OPEN_RUN, spire_guard_run_start_apply(snap, copy.first, copy.second, 920, 3));
      expect(is_real(snap));
    }
    // the same login 61 with the real character confirms the restore
    {
      auto snap = stored(false, false, true);
      expect(spire_guard_awaiting_save_login_apply(snap, real.first, real.second));
      expect(!snap.restore_pending && !snap.awaiting_save && !snap.run_in_progress);
      expect(is_real(snap));
    }
    // a run copy with the real level and EXP but the run's items or material counters is no confirmation either
    {
      auto items = make(7, 0x00, 51, 370758, 1000, 125);
      auto snap = stored(false, false, true);
      expect(!spire_guard_awaiting_save_login_apply(snap, items.first, items.second));
      auto power = make(20, 0x00, 51, 370758, 1000, 0);
      snap = stored(false, false, true);
      expect(!spire_guard_awaiting_save_login_apply(snap, power.first, power.second));
    }
    // real progress: the 98 of a normal game under AwaitingSave alone moves the snapshot along and (v40) closes
    // AwaitingSave, since the client writes its card when it leaves the game; under any other marker the 98 changes
    // nothing
    {
      auto progress = make(21, 0x00, 52, 380000, 1500, 125);
      auto snap = stored(false, false, true);
      snap.has_overlay = true;
      snap.overlay_level = 149;
      expect(spire_guard_follow_real_progress(snap, progress.first, progress.second));
      expect(!snap.awaiting_save && !snap.has_overlay && !snap.run_in_progress && !snap.restore_pending);
      expect(spire_guard_same_state(snap.inventory, snap.disp, progress.first, progress.second));
      expect_eq(52u, snap.disp.stats.level.load());
      // the login 61 that follows shows that data and finds no open marker: no push (the login path's early return)
      expect(!snap.run_in_progress && !snap.restore_pending && !snap.awaiting_save);
      expect(spire_guard_same_state(progress.first, progress.second, snap.inventory, snap.disp));
      // a second 98 after the close changes nothing (the marker is no longer AwaitingSave)
      auto again = snap;
      expect(!spire_guard_follow_real_progress(snap, copy.first, copy.second));
      expect(!snap.awaiting_save && spire_guard_same_state(snap.inventory, snap.disp, again.inventory, again.disp));
      for (auto [run, restore] : std::vector<std::tuple<bool, bool>>{{true, false}, {false, true}, {false, false}}) {
        auto other = stored(run, restore, false);
        expect(!spire_guard_follow_real_progress(other, copy.first, copy.second));
        expect(is_real(other));
      }
      // RunInProgress or RestorePending next to AwaitingSave: nothing follows, AwaitingSave stays
      for (auto [run, restore] : std::vector<std::tuple<bool, bool>>{{true, false}, {false, true}, {true, true}}) {
        auto other = stored(run, restore, true);
        expect(!spire_guard_follow_real_progress(other, copy.first, copy.second));
        expect(is_real(other) && other.awaiting_save);
      }
    }
    // no open marker: the run start takes the server's data, which no open marker can hold as a run copy
    {
      SpireGuardSnapshot snap;
      expect_eq(SpireGuardRunStart::FRESH, spire_guard_run_start_apply(snap, real.first, real.second, 920, 1));
      expect(snap.run_in_progress && is_real(snap));
    }
  }

  phosg::log_info_f("-- Spire guard: the restore check finds a different item count or material counter (v32, v38, v39)");
  {
    PlayerInventory real;
    real.num_items = 20;
    for (size_t z = 0; z < 20; z++) {
      real.items[z].data.data1[1] = 0x01 + z;
    }
    real.items[8].extension_data2 = 125;
    real.hp_from_materials = 2 * 37;
    PlayerDispDataV4 disp;
    disp.stats.level = 51;
    disp.stats.exp = 370758;
    expect(spire_guard_same_state(real, disp, real, disp));
    PlayerInventory fewer = real; // the same first 19 items, one missing
    fewer.num_items = 19;
    expect(!spire_guard_same_inventory(fewer, 0, real, 0));
    expect(!spire_guard_same_state(fewer, disp, real, disp));
    expect(!spire_guard_push_verified(true, spire_guard_same_inventory(fewer, 0, real, 0) && spire_guard_same_materials(fewer, real)));
    PlayerInventory more = real;
    more.num_items = 21;
    more.items[20].data.data1[1] = 0x30;
    expect(!spire_guard_same_state(more, disp, real, disp));
    for (size_t z = 8; z <= 12; z++) {
      PlayerInventory mat = real;
      mat.items[z].extension_data2++;
      expect(spire_guard_same_inventory(mat, 0, real, 0)); // the items alone are equal
      expect(!spire_guard_same_state(mat, disp, real, disp));
      expect(!spire_guard_push_verified(true, spire_guard_same_inventory(mat, 0, real, 0) && spire_guard_same_materials(mat, real)));
    }
    PlayerInventory hp = real;
    hp.hp_from_materials = 0;
    expect(!spire_guard_same_state(hp, disp, real, disp));
    PlayerInventory tp = real;
    tp.tp_from_materials = 2;
    expect(!spire_guard_same_state(tp, disp, real, disp));
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

  phosg::log_info_f("-- Spire guard: the restore checks compare the material counters too (v38)");
  {
    PlayerInventory real, back;
    real.num_items = back.num_items = 20;
    real.items[8].extension_data2 = back.items[8].extension_data2 = 125; // Power at the limit (Omgzors)
    real.hp_from_materials = back.hp_from_materials = 2 * 37;
    expect(spire_guard_same_materials(real, back));
    for (size_t z = 8; z <= 12; z++) { // each of Power, Mind, Evade, Def, Luck
      PlayerInventory off = back;
      off.items[z].extension_data2++;
      expect(!spire_guard_same_materials(off, real));
    }
    PlayerInventory hp = back;
    hp.hp_from_materials += 2;
    expect(!spire_guard_same_materials(hp, real));
    PlayerInventory tp = back;
    tp.tp_from_materials = 2;
    expect(!spire_guard_same_materials(tp, real));
    PlayerInventory stamp = back; // items[4..7] hold the save time and [13..15] are unknown: not compared
    stamp.items[4].extension_data2 = 0x55;
    stamp.items[13].extension_data2 = 1;
    expect(spire_guard_same_materials(stamp, real));
    PlayerDispDataV4 disp;
    expect(spire_guard_same_state(back, disp, real, disp));
    PlayerInventory used = back;
    used.items[9].extension_data2 = 3;
    expect(!spire_guard_same_state(used, disp, real, disp)); // same items, level, EXP and meseta: not the same state
    expect_eq(std::string("pow 125 mind 0 evade 0 def 0 luck 0 hp 37 tp 0"), spire_guard_materials_str(real));
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

  phosg::log_info_f("-- Spire BBA TX guard: version gate, encoding and the compiled client function");
  {
    // Only GC EU v1.0 (3OP0, GPOP8P) has the driver at these addresses
    expect(spire_bba_tx_guard_applies(0x334F5030)); // 3OP0
    for (uint32_t other : {0x334F4A32u, 0x334F4A33u, 0x334F4A34u, 0x334F4A35u, 0x334F4530u, 0x334F4531u, 0x334F4532u,
             0x33534A54u, 0x33534A30u, 0x33534530u, 0x33535030u, 0x334F5031u, 0x33000000u, 0u, 0xFFFFFFFFu}) {
      expect(!spire_bba_tx_guard_applies(other));
    }

    // The encoding: the three words the driver keeps (stw, mtlr, blrl) and the new words that carry the null check
    const auto& orig = SPIRE_BBA_TX_GUARD_ORIGINAL;
    const auto& patched = SPIRE_BBA_TX_GUARD_PATCHED;
    expect_eq(0x818DD694, patched[0]); // lwz r12,-0x296c(r13): same load the driver did at +8
    expect_eq(orig[2], patched[0]);
    expect_eq(0x7D8CE9D6, patched[1]); // mullw r12,r12,r29
    expect_eq(0x280C0000, patched[2]); // cmplwi r12,0
    expect_eq(0x41820014, patched[3]); // beq +0x14
    expect_eq(SPIRE_BBA_TX_GUARD_ADDRESS + 0x20, SPIRE_BBA_TX_GUARD_ADDRESS + 3 * 4 + (patched[3] & 0xFFFC)); // to the epilogue at +0x20
    expect_eq(0x7C1DE850, patched[4]); // subf r0,r29,r29 = 0
    expect_eq(orig[5], patched[5]); // stw r0,-0x296c(r13)
    expect_eq(orig[6], patched[6]); // mtlr r12
    expect_eq(orig[7], patched[7]); // blrl
    expect_eq(0x2C1D0000, orig[0]); // cmpwi r29,0 (the check the patch folds into the mullw)
    expect_eq(0x4182001C, orig[1]); // beq +0x1C: the same epilogue
    expect_eq(SPIRE_BBA_TX_GUARD_ADDRESS + 0x20, SPIRE_BBA_TX_GUARD_ADDRESS + 1 * 4 + (orig[1] & 0xFFFC));
    expect(orig != patched);

    // The compiled client function exists for 3OP0 only and carries exactly these two word tables
    ClientFunctionIndex index("system/client-functions", true);
    auto fn = index.get(SPIRE_BBA_TX_GUARD_FUNCTION_NAME, SPIRE_BBA_TX_GUARD_SPECIFIC_VERSION);
    expect(fn != nullptr);
    expect_eq(static_cast<int>(ClientFunctionIndex::Function::Architecture::POWERPC), static_cast<int>(fn->arch));
    expect_eq(SPIRE_BBA_TX_GUARD_SPECIFIC_VERSION, fn->specific_version);
    expect(!fn->appears_in_patches_menu());
    auto words_be = [](const std::array<uint32_t, 8>& words) -> std::string {
      std::string ret;
      for (uint32_t w : words) {
        ret.push_back((w >> 24) & 0xFF);
        ret.push_back((w >> 16) & 0xFF);
        ret.push_back((w >> 8) & 0xFF);
        ret.push_back(w & 0xFF);
      }
      return ret;
    };
    std::string orig_be = words_be(orig);
    std::string patched_be = words_be(patched);
    size_t orig_at = fn->code.find(orig_be);
    size_t patched_at = fn->code.find(patched_be);
    expect(orig_at != std::string::npos);
    expect(patched_at != std::string::npos);
    expect_eq(orig_at + 0x20, patched_at); // the .s puts the patched table right behind the original one
    expect_eq(std::string::npos, fn->code.find(orig_be, orig_at + 1));
    expect_eq(std::string::npos, fn->code.find(patched_be, patched_at + 1));
    for (uint32_t other : {0x334F4530u, 0x334F4531u, 0x334F4532u, 0x334F4A32u, 0x33535030u}) {
      bool threw = false;
      try {
        index.get(SPIRE_BBA_TX_GUARD_FUNCTION_NAME, other);
      } catch (const std::out_of_range&) {
        threw = true;
      }
      expect(threw);
    }
  }

  phosg::log_info_f("-- Spire follow: never a partner that is still loading (audit S-12)");
  {
    expect(spire_follow_should_warp(1, 2, false));
    expect(!spire_follow_should_warp(1, 2, true));                        // LOADING set
    expect(spire_follow_should_warp(0x0F, 0x0E));                         // Ep2: Gol Dragon is a real floor
    expect(spire_follow_should_warp(0x0E, 0x0F));
    expect(!spire_follow_should_warp(SPIRE_FOLLOW_CITY_FLOOR, 2));        // the city takes the Ragol warp itself
    expect(!spire_follow_should_warp(1, SPIRE_FOLLOW_CITY_FLOOR));        // never to the city
    expect(!spire_follow_should_warp(2, 2));                              // already there
    expect(spire_follow_partner_pending(1, 2, true));                     // remembered, follows after its load
    expect(!spire_follow_partner_pending(0x0F, 0x0E, false));              // Ep2 floor 0x0F, loaded: warped now
    expect(!spire_follow_partner_pending(1, 2, false));                   // warped now
    expect(!spire_follow_partner_pending(SPIRE_FOLLOW_CITY_FLOOR, 2, false)); // city: takes the Ragol warp itself
  }

  phosg::log_info_f("-- Spire bridge: no window in a run, every run text is one line (v35)");
  {
    expect_eq(std::string("New best score: 5237! Records saved."), spire_ticker_line("New best score: 5237!\nRecords saved."));
    expect_eq(std::string("a b"), spire_ticker_line("a \nb\n"));
    expect_eq(std::string("Waiting for your partner..."), spire_ticker_line("Waiting for your partner..."));
    for (uint32_t value : {0x57000102u, 0x4C000002u, 0x4C000101u, 0x5400000Fu, 0x50000000u, 0x50000001u, 0x580000A3u,
             0x43000102u, 0x43000202u, 0x43000602u, 0x43000702u, 0x43000802u, 0x47000000u, 0x47000001u, 0x55000000u, 0x55000001u, 0x48000000u, 0x41000002u, 0x41000105u, 0x41000207u}) {
      std::string text = spire_bridge_message_text(value);
      expect(!text.empty());
      expect(text.find('\n') == std::string::npos);
    }
    expect_eq(std::string("Waiting for your partner..."), spire_bridge_message_text(0x50000000));
    expect_eq(std::string("Run over - back to the lobby. Rooms cleared: 0, 163 s."), spire_bridge_message_text(0x580000A3));
    expect(spire_bridge_message_text(0x53504952).empty()); // 'SPIR' is no message
    // v36: the quest's codes from spike/stadstart b29f310 (kind << 24 | a << 8 | b)
    expect_eq(std::string("Reward taken. Quest Board: Shop (optional) or Continue = leave room."), spire_bridge_message_text(0x47000000));
    expect_eq(std::string("Reward taken. Quest Board: Shop (optional) or Ready = leave room."), spire_bridge_message_text(0x47000001));
    expect_eq(std::string("You are down - you get up when the room is clear."), spire_bridge_message_text(0x55000001));
    expect_eq(std::string("You are back on your feet."), spire_bridge_message_text(0x55000000));
    expect_eq(std::string("Close the board and take a step."), spire_bridge_message_text(0x48000000));
    expect_eq(std::string("Room 2"), spire_bridge_message_text(0x41000002));
    // v37: 'C' point << 8 | room (quest 802ebe9); 3-5 retired
    expect_eq(std::string("Take your reward: Quest Board, slot Reward."), spire_bridge_message_text(0x43000102));
    expect_eq(std::string("Take a step: your reward pages open."), spire_bridge_message_text(0x43000202));
    expect_eq(std::string("The way is open. Walk to a warp."), spire_bridge_message_text(0x43000602));
    expect_eq(std::string("Partner has not taken a reward. The way is open."), spire_bridge_message_text(0x43000702));
    expect_eq(std::string("Partner is in the shop. The warp waits for him."), spire_bridge_message_text(0x43000802));
    expect_eq(std::string("Waiting for your partner at Forest 1."), spire_bridge_message_text(0x43000901)); // leader
    expect_eq(std::string("Waiting for your partner at Forest 1."), spire_bridge_message_text(0x43000900)); // follower
    for (uint32_t value : {0x43000302u, 0x43000402u, 0x43000502u}) {
      expect(spire_bridge_message_text(value).empty());
    }
    expect_eq(std::string("Room clear! Take a step for your reward."), std::string(SPIRE_BRIDGE_ROOM_CLEAR_TEXT));
    expect_eq(std::string("Room 5 - Elite (harder, better reward)"), spire_bridge_message_text(0x41000105));
    expect_eq(std::string("Room 7 - Rest"), spire_bridge_message_text(0x41000207));
    expect_eq(std::string("Last life: fall again and you stay down until the clear."), spire_bridge_message_text(0x4C000100));
    expect_eq(std::string("$C1Life lost$C7 - 2 left. You get up on the spot."), spire_bridge_message_text(0x4C000002));
  }

  phosg::log_info_f("-- Spire lives: a counter, no Scape Doll, no respawn to the city (v41)");
  {
    expect_eq(std::string("Spire lives: death of serial 0012345678 revived on the spot, lives 3->2, no respawn to city"),
        spire_lives_revive_log_line(12345678, 0x4C000002));
    expect_eq(std::string("Spire lives: death of serial 0012345678 revived on the spot, lives 2->1, no respawn to city"),
        spire_lives_revive_log_line(12345678, 0x4C000101)); // the death that leaves the last life
    ItemData doll, mate, frame;
    doll.data1[0] = 0x03;
    doll.data1[1] = 0x09;
    mate.data1[0] = 0x03;
    mate.data1[1] = 0x00;
    frame.data1[0] = 0x01;
    frame.data1[1] = 0x09;
    expect(spire_drop_is_forbidden(doll));
    expect(!spire_drop_is_forbidden(mate));
    expect(!spire_drop_is_forbidden(frame));
  }

  phosg::log_info_f("-- Spire bridge: an enemy never hit holds no wave forever (v36)");
  {
    using E = SpireWaveEnemy;
    E dead{.defeated = true}, unhit{}, hit{.ever_hit = true}, boss{.is_boss = true};
    expect(spire_wave_held_only_by_unhit({dead, dead, unhit}));        // pc 09:26: E-02C in a warp, the rest defeated
    expect(spire_wave_held_only_by_unhit({dead, unhit, unhit}));
    expect(!spire_wave_held_only_by_unhit({dead, unhit, hit}));        // a hit enemy is reachable: wait for it
    expect(!spire_wave_held_only_by_unhit({dead, boss}));              // never a boss
    expect(!spire_wave_held_only_by_unhit({unhit, unhit}));            // nothing defeated yet: the wave has not run
    expect(!spire_wave_held_only_by_unhit({dead, dead}));              // already done
    expect(!spire_wave_held_only_by_unhit({}));
  }

  phosg::log_info_f("-- Spire bridge: a started event without a living enemy holds no room forever (v42)");
  {
    using E = SpireWaveEnemy;
    E dead{.defeated = true}, unhit{}, hit{.ever_hit = true}, boss{.is_boss = true}, dead_boss{true, true, true};
    expect(spire_event_without_living_enemy({unhit}));                  // pc 12:32: E-04D, the elite Hildebear, never showed
    expect(spire_event_without_living_enemy({dead, dead}));             // all defeated, the 6x76 never came
    expect(spire_event_without_living_enemy({dead, unhit}));
    expect(spire_event_without_living_enemy({}));                       // an event without enemies
    expect(spire_event_without_living_enemy({dead_boss}));
    expect(!spire_event_without_living_enemy({unhit, hit}));            // a hit enemy is alive and reachable: wait
    expect(!spire_event_without_living_enemy({boss}));                  // never a living boss
  }

  phosg::log_info_f("-- Spire bridge: run over sends the client back to the lobby, never twice (v35)");
  {
    using X = SpireRunOverExit;
    // arguments: same_game, quest_in_progress, can_run_functions, already_sent
    expect_eq(X::EXIT_QUEST, spire_run_over_exit_action(true, true, false, false));    // 6x73, as $exit in a quest
    expect_eq(X::EXIT_QUEST, spire_run_over_exit_action(true, true, true, false));
    expect_eq(X::NONE, spire_run_over_exit_action(true, true, true, true));            // its own 'X' after the fallback
    expect_eq(X::NONE, spire_run_over_exit_action(false, true, true, false));          // already left the game
    expect_eq(X::EXIT_ANYWHERE, spire_run_over_exit_action(true, false, true, false)); // no quest loaded any more
    expect_eq(X::TELL, spire_run_over_exit_action(true, false, false, false));
  }

  phosg::log_info_f("-- Spire protected characters: any quest, also one not listed (audit S-3)");
  {
    DataIndex d;
    d.spire_blocked_quest_numbers.emplace(929);
    d.spire_blocked_characters.emplace_back(123, "Wakiru");
    d.spire_blocked_characters.emplace_back(456, "");
    expect(d.is_spire_protected_character(123, "\tEwakiru"));     // language marker and case ignored
    expect(!d.is_spire_protected_character(123, "Other"));
    expect(d.is_spire_protected_character(456, "Anyone"));       // empty name: every character on the card
    expect(!d.is_spire_protected_character(789, "Wakiru"));
    expect(d.is_spire_blocked(123, "Wakiru", 929));
    expect(!d.is_spire_blocked(123, "Wakiru", 930));             // not a listed quest
    expect(!d.is_spire_blocked(123, "Other", 929));
    std::string wakiru = "Wakiru", other = "Other";
    expect(d.is_spire_blocked_quest(123, &wakiru, 930, true));   // Challenge: blocked whatever the number
    expect(!d.is_spire_blocked_quest(123, &other, 930, true));
    expect(!d.is_spire_blocked_quest(123, &wakiru, 930, false));
    expect(d.is_spire_blocked_quest(123, &wakiru, 929, false));
    expect(d.is_spire_blocked_quest(123, nullptr, 930, true));   // no character: fail closed
    expect(d.is_spire_blocked_quest(123, nullptr, 929, false));
    expect(!d.is_spire_blocked_quest(123, nullptr, 930, false));
    DataIndex empty;
    expect(!empty.is_spire_blocked_quest(123, nullptr, 929, true)); // no protected characters: nothing blocked
  }

  phosg::log_info_f("-- Spire test serials: room quests available server-side, no flags written (v28)");
  {
    DataIndex d;
    d.spire_test_serials.emplace(3117539659);
    auto available = [&](const char* expr_str, uint32_t serial, uint32_t checkpoint_rooms) -> bool {
      auto expr = ResourceDASM::Expression::Node::parse(expr_str);
      ExpressionEnvironment env{.num_players = 1, .section_id = 0, .event = 0, .v1_present = false,
          .difficulty = Difficulty::NORMAL, .quest_flags = nullptr, .challenge_records = nullptr, .team = nullptr,
          .spire_rooms = d.spire_availability_rooms(serial, checkpoint_rooms)};
      return env.evaluate(*expr) != 0;
    };
    expect(available("SPIRE_K3", 3117539659, 0));    // test serial, no rooms cleared: q945 available
    expect(available("SPIRE_K1", 3117539659, 0));
    expect(available("SPIRE_K10", 3117539659, 0));   // q955
    expect(!available("SPIRE_K11", 3117539659, 0));  // only rooms 1-10
    expect(!available("SPIRE_K3", 3117539666, 0));   // another serial without rooms: locked
    expect(!available("SPIRE_K1", 0, 0));            // no GC serial
    expect(available("SPIRE_K3", 3117539666, 0x4));  // another serial with room 3 cleared: available
    expect(!available("SPIRE_K4", 3117539666, 0x4));
    expect_eq(0x3FFu, d.spire_availability_rooms(3117539659, 0));
    expect_eq(0x800003FFu, d.spire_availability_rooms(3117539659, 0x80000004)); // checkpoint rooms kept
    expect_eq(0x4u, d.spire_availability_rooms(3117539666, 0x4));               // other serial unchanged
    DataIndex empty;
    expect_eq(0u, empty.spire_availability_rooms(3117539659, 0));             // key missing: off
  }

  phosg::log_info_f("-- Spire meta registers: one file per character (audit S-6)");
  {
    auto a = spire_bridge_registers_filename(42, "AAAA");
    auto b = spire_bridge_registers_filename(42, "BBBB");
    expect(a != b);
    expect(a.ends_with("/0000000042.AAAA.registers.json"));
    PlayerDispDataV4 da, db;
    da.visual.name.encode("Alpha", Language::ENGLISH);
    db.visual.name.encode("Beta", Language::ENGLISH);
    auto fa = spire_bridge_registers_filename_for(42, &da);
    expect(fa.ends_with(".registers.json"));
    expect(fa != spire_bridge_registers_filename_for(42, &db));   // a second character never reads the first's file
    expect(fa == spire_bridge_registers_filename(42, spire_guard_character_id(da)));
    expect(spire_bridge_registers_filename_for(42, nullptr).empty()); // no character data: not stored
  }

  phosg::log_info_f("-- Spire room clear: post-wave actions that hold the room (v19)");
  {
    auto none_undefeated = [](uint16_t) { return false; };
    auto wave3_undefeated = [](uint16_t wave) { return wave == 3; };
    auto none_unfinished = [](uint32_t) { return false; };
    auto event7_unfinished = [](uint32_t ev) { return ev == 7; };
    // 09 construct room 5 wave 3, then 01 stop
    const uint8_t construct[] = {0x09, 0x05, 0x00, 0x03, 0x00, 0x01};
    expect(spire_room_actions_hold(construct, sizeof(construct), 5, none_undefeated, none_unfinished).empty());
    expect(!spire_room_actions_hold(construct, sizeof(construct), 5, wave3_undefeated, none_unfinished).empty());
    expect(spire_room_actions_hold(construct, sizeof(construct), 6, wave3_undefeated, none_unfinished).empty()); // other room
    // 0A (2 bytes), 08 (4 bytes), 0C trigger event 7
    const uint8_t trigger[] = {0x0A, 0x11, 0x22, 0x08, 0, 0, 0, 0, 0x0C, 0x07, 0x00, 0x00, 0x00};
    expect(spire_room_actions_hold(trigger, sizeof(trigger), 5, none_undefeated, none_unfinished).empty());
    expect(!spire_room_actions_hold(trigger, sizeof(trigger), 5, none_undefeated, event7_unfinished).empty());
    // 0D ends the stream: the trigger after it is not read
    const uint8_t stop[] = {0x0D, 0x05, 0x00, 0x01, 0x00, 0x0C, 0x07, 0x00, 0x00, 0x00};
    expect(spire_room_actions_hold(stop, sizeof(stop), 5, none_undefeated, event7_unfinished).empty());
    // 01 stops too
    const uint8_t stop01[] = {0x01, 0x0C, 0x07, 0x00, 0x00, 0x00};
    expect(spire_room_actions_hold(stop01, sizeof(stop01), 5, none_undefeated, event7_unfinished).empty());
  }

  phosg::log_info_f("-- Spire draw: table limits and a fixed draw (v20, audit Q2-2)");
  {
    auto table_json = [](size_t num_entries, int zero_tag) {
      std::string entries, members;
      for (size_t z = 0; z < num_entries; z++) {
        entries += std::format("{}{{\"cap\":2,\"vanaf_k\":1,\"android_ok\":{},\"rare\":false,\"own_curse\":false,\"glass_ok\":false}}",
            z ? "," : "", (z == 0) ? "false" : "true");
        members += std::format("{}{}", z ? "," : "", z);
      }
      std::string row = std::format("[{}]", members);
      std::string zero = (zero_tag < 0) ? "" : std::format("{}", zero_tag);
      // num_tags 1: 3 * 1 + 1 rows per band; the same archetype for HU, RA and FO
      std::string archetype = std::format(
          "{{\"entries\":[{}],\"kbands\":[1],\"rows\":{{\"1\":[{},{},{},{}]}},\"slot_c_zero_tags\":[{}]}}",
          entries, row, row, row, row, zero);
      return std::format("{{\"num_tags\":1,\"rare_base\":5,\"rare_per_k\":3,\"uncommon_below\":40,\"curse_pct\":10,"
                         "\"max_attempts\":8,\"blood_pact_bonus\":15,\"relic_bit_r3\":4,\"relic_bit_r13\":8,\"relic_bit_r14\":16,"
                         "\"archetypes\":[{},{},{}]}}",
          archetype, archetype, archetype);
    };
    auto filename = (std::filesystem::temp_directory_path() / std::format("newserv-static-spire-draw-{}.json", getpid())).string();
    auto load = [&](const std::string& json) {
      phosg::save_file(filename, json);
      return SpireDrawTable::from_json_file(filename);
    };
    auto t = load(table_json(4, 0));
    expect_eq(3u, t.archetypes.size());
    expect_eq(SPIRE_DRAW_MAX_ENTRIES, load(table_json(SPIRE_DRAW_MAX_ENTRIES, -1)).archetypes.at(0).entries.size());
    expect_raises(std::runtime_error, [&]() { load(table_json(SPIRE_DRAW_MAX_ENTRIES + 1, -1)); }); // picks cut off
    expect_raises(std::runtime_error, [&]() { load(table_json(4, 1)); }); // zero tag >= num_tags
    std::filesystem::remove(filename);

    // rnd always 0: every slot picks the first member, then walks the table once the attempts run out
    auto rnd0 = [](uint32_t) -> uint32_t { return 0; };
    SpireDrawInput in;
    uint32_t drawn = spire_draw(t, in, rnd0);
    // board slot 2/3/4/5 at bits 0/6/12/18
    expect_eq(1u, ((drawn >> 6) & 0x3F));   // slot 3: entry 1
    expect_eq(2u, ((drawn >> 12) & 0x3F));  // slot 4: entry 1 is on the board, so entry 2
    expect_eq(3u, ((drawn >> 18) & 0x3F));  // slot 5: entry 3
    expect_eq(0u, (drawn & 0x3F));          // no fourth reward without R14
    expect_eq(0u, (drawn >> 24));           // R12 is never offered at fewer than 2 lives
    in.race = 2;                            // android: entry 1 is not android_ok
    expect_eq(2u, ((spire_draw(t, in, rnd0) >> 6) & 0x3F));
    in.race = 0;
    in.picks[0] = 2;                        // entry 1 taken twice: at its cap
    expect_eq(2u, ((spire_draw(t, in, rnd0) >> 6) & 0x3F));
    in.picks[0] = 0;
    in.relics = 16;                         // R14 More Eyes: slot 2 too
    expect_eq(4u, (spire_draw(t, in, rnd0) & 0x3F));
    in.archetype = 3;
    expect_raises(std::runtime_error, [&]() { spire_draw(t, in, rnd0); });
  }

  phosg::log_info_f("-- Spire meta: the flags a finished run sets (v24)");
  {
    auto has = [](const std::vector<uint16_t>& v, uint16_t f) { return std::find(v.begin(), v.end(), f) != v.end(); };
    auto lost = spire_meta_decode_finish(0);
    expect(!lost.won && (lost.exams == 0) && !lost.deathless);
    auto f = spire_meta_flags_for_finish(lost, 0, false);
    expect_eq(1u, f.size());
    expect_eq(SPIRE_META_FLAG_FIRST_RUN, f.at(0));

    auto dragon = spire_meta_decode_finish(1 << SPIRE_META_FINISH_EXAMS_SHIFT);
    expect_eq(1u, dragon.exams);
    f = spire_meta_flags_for_finish(dragon, 0, false);
    expect(has(f, SPIRE_META_FLAG_DRAGON) && has(f, SPIRE_META_FLAG_PACT_FIRST) && has(f, SPIRE_META_FLAG_PACT_FIRST + 1));
    expect(!has(f, SPIRE_META_FLAG_DEROLLE) && !has(f, SPIRE_META_FLAG_PAY_WIN));

    auto old_win = spire_meta_decode_finish(1 << 23); // a quest from before v24: the won bit alone means both exams
    expect(old_win.won && (old_win.exams == 2));
    expect_eq(2u, spire_meta_decode_finish(3 << SPIRE_META_FINISH_EXAMS_SHIFT).exams); // 3 is capped at 2

    auto deathless = spire_meta_decode_finish((1 << 23) | SPIRE_META_FINISH_DEATHLESS | 600);
    expect(deathless.deathless);
    f = spire_meta_flags_for_finish(deathless, 0, true); // HUmar
    expect(has(f, SPIRE_META_FLAG_DEATHLESS_WIN) && has(f, SPIRE_META_FLAG_PAY_DEATHLESS));
    expect(has(f, SPIRE_META_FLAG_KIT2_FIRST) && has(f, SPIRE_META_FLAG_PAY_RECORD));
    expect(has(spire_meta_flags_for_finish(deathless, 3, false), SPIRE_META_FLAG_KIT2_FIRST + 1)); // RAmar
    expect(has(spire_meta_flags_for_finish(deathless, 6, false), SPIRE_META_FLAG_KIT2_FIRST + 2)); // FOmarl
    expect(!has(spire_meta_flags_for_finish(deathless, 6, false), SPIRE_META_FLAG_PAY_RECORD));
    expect_eq(std::string("Iron man"), std::string(spire_meta_flag_name(SPIRE_META_FLAG_PACT_FIRST)));
    expect(spire_meta_flag_name(SPIRE_META_FLAG_PAY_WIN) == nullptr);
  }

  phosg::log_info_f("-- All static tests passed");
}
