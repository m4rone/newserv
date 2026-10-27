#include <phosg/Filesystem.hh>
#include <phosg/UnitTest.hh>

#include <algorithm>
#include <filesystem>
#include <format>
#include <unistd.h>

#include "DataIndex.hh"
#include "ExpressionEnvironment.hh"
#include "InventoryBackup.hh"
#include "Map.hh"
#include "PSOEncryption.hh"
#include "Text.hh"
#include "QuestScript.hh"
#include "ReceiveSubcommands.hh"
#include "RareItemSet.hh"
#include "ClientFunctionIndex.hh"
#include "CrashValueFilter.hh"
#include "SpireBBATxGuard.hh"
#include "SpireDraw.hh"
#include "SpireDrops.hh"
#include "SpireFlagBridge.hh"
#include "SpireMag.hh"
#include "SpireMeta.hh"
#include "SendCommands.hh"

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

  phosg::log_info_f("-- Spire base return: the push in a game only at the run end, on a field floor, never during a load (v48)");
  {
    using R = SpireBaseReturnRefusal;
    SpireBaseReturnState ok;
    ok.has_character = true;
    ok.spire_run = true;
    ok.run_over = true;
    ok.on_field_floor = true;
    ok.can_run_functions = true;
    ok.has_snapshot = true;
    ok.run_in_progress = true;
    expect_eq(R::NONE, spire_base_return_refusal(ok));
    auto with = [&](auto change) {
      SpireBaseReturnState st = ok;
      change(st);
      return spire_base_return_refusal(st);
    };
    expect_eq(R::NO_CHARACTER, with([](auto& st) { st.has_character = false; }));
    expect_eq(R::NOT_A_SPIRE_RUN, with([](auto& st) { st.spire_run = false; }));
    expect_eq(R::RUN_NOT_OVER, with([](auto& st) { st.run_over = false; }));          // mid-run: never
    expect_eq(R::NOT_ON_FIELD, with([](auto& st) { st.on_field_floor = false; }));    // already on Pioneer 2
    expect_eq(R::LOADING, with([](auto& st) { st.any_client_loading = true; }));      // the B2 + load hang (v33)
    expect_eq(R::NO_FUNCTIONS, with([](auto& st) { st.can_run_functions = false; }));
    expect_eq(R::PROTECTED, with([](auto& st) { st.protected_character = true; }));
    expect_eq(R::MARKER, with([](auto& st) { st.has_snapshot = false; }));
    expect_eq(R::MARKER, with([](auto& st) { st.run_in_progress = false; }));
    expect_eq(R::MARKER, with([](auto& st) { st.restore_pending = true; }));
    expect_eq(R::MARKER, with([](auto& st) { st.awaiting_save = true; }));
    expect_eq(R::RESTORE_RUNNING, with([](auto& st) { st.restore_running = true; }));
    expect_eq(R::ALREADY_SENT_BACK, with([](auto& st) { st.already_sent_back = true; }));
    // the answer: 'O' tag, refusal in bits 8-15, OK only without a refusal
    expect_eq(0x4F000001u, spire_base_return_answer(R::NONE));
    expect_eq(0x4F000500u, spire_base_return_answer(R::LOADING));
    expect_eq(0x4F000B00u, spire_base_return_answer(R::PUSH_NOT_VERIFIED));
    // v57: the base game is a FAIL to the quest (no warp in this game); the client leaves with 6x73
    expect_eq(0x4F000C00u, spire_base_return_answer(R::BASE_GAME));
    expect_eq(std::string("the client goes to the base game"), std::string(spire_base_return_refusal_name(R::BASE_GAME)));
    // v59: the base's new run is 'O' sub 2; the run end's own 'O' (0x4F000000) is sub 0
    expect_eq(0x4F020000u, SPIRE_BASE_NEW_RUN_VALUE);
    expect_ne(SPIRE_BASE_NEW_RUN_VALUE, SPIRE_BASE_RETURN_TAG);
    expect_eq(static_cast<uint32_t>(SPIRE_BRIDGE_BASE_RETURN), SPIRE_BASE_NEW_RUN_VALUE >> 24);
    // v59: the marker stays open in the base game: its 98 never follows the real character nor closes AwaitingSave
    expect_eq(false, spire_guard_98_follows(true, true, true));
    expect_eq(true, spire_guard_98_follows(true, true, false));
    expect_eq(false, spire_guard_98_follows(false, true, false));
    expect_eq(false, spire_guard_98_follows(true, false, false));
    for (uint8_t z = 1; z <= static_cast<uint8_t>(R::BASE_GAME); z++) {
      expect_eq(SPIRE_BASE_RETURN_FAIL, spire_base_return_answer(static_cast<R>(z)) & 0xFF);
    }
    // the code letter is free: no other r248 code or 4CC starts with 'O'
    for (uint8_t letter : {SPIRE_BRIDGE_MSG_WAVE, SPIRE_BRIDGE_MSG_LIFE, SPIRE_BRIDGE_MSG_TIME, SPIRE_BRIDGE_MSG_PARTNER,
             SPIRE_BRIDGE_MSG_RUN_OVER, SPIRE_BRIDGE_MSG_PAUSE_B, SPIRE_BRIDGE_MSG_DOWN, SPIRE_BRIDGE_MSG_HINT,
             SPIRE_BRIDGE_MSG_APPROACH, SPIRE_BRIDGE_MSG_PROMPT, SPIRE_BRIDGE_REC_BEGIN, SPIRE_BRIDGE_REC_SCORE,
             SPIRE_BRIDGE_REC_FINISH, SPIRE_BRIDGE_REC_BOARD, SPIRE_BRIDGE_TIMER_START, SPIRE_BRIDGE_DRAW_REQUEST,
             SPIRE_BRIDGE_SAVESTATE_WRITE, SPIRE_BRIDGE_RESUME, static_cast<uint8_t>('N'),
             static_cast<uint8_t>(SPIRE_BRIDGE_REQUEST_VALUE >> 24), static_cast<uint8_t>(SPIRE_BRIDGE_DONE_VALUE >> 24),
             static_cast<uint8_t>(SPIRE_BRIDGE_BATTLE_END_VALUE >> 24),
             static_cast<uint8_t>(SPIRE_BRIDGE_ROOM_CLEAR_VALUE >> 24)}) {
      expect(letter != SPIRE_BRIDGE_BASE_RETURN);
    }
    // the answer is tagged apart from the 'J' resume answer on the same register
    expect_eq(SPIRE_RESUME_ANSWER_REGISTER, SPIRE_BASE_RETURN_ANSWER_REGISTER);
    expect(SPIRE_BASE_RETURN_TAG != SPIRE_RESUME_ANSWER_TAG);
  }

  phosg::log_info_f("-- Spire run again: a second run in the same quest only after a verified return to base, on floor 0 or 1 (v49)");
  {
    using R = SpireRunAgainRefusal;
    SpireRunAgainState ok;
    ok.has_character = true;
    ok.spire_run = true;
    ok.base_returned = true;
    ok.at_start_floor = true;
    ok.can_run_functions = true;
    ok.has_snapshot = true;
    ok.run_in_progress = true;
    expect_eq(R::NONE, spire_run_again_refusal(ok));
    auto with = [&](auto change) {
      SpireRunAgainState st = ok;
      change(st);
      return spire_run_again_refusal(st);
    };
    expect_eq(R::NO_CHARACTER, with([](auto& st) { st.has_character = false; }));
    expect_eq(R::NOT_A_SPIRE_RUN, with([](auto& st) { st.spire_run = false; }));
    expect_eq(R::NOT_RETURNED, with([](auto& st) { st.base_returned = false; }));   // no 'O' OK, or used up by an 'I'
    expect_eq(R::NOT_AT_START, with([](auto& st) { st.at_start_floor = false; }));
    expect_eq(R::LOADING, with([](auto& st) { st.any_client_loading = true; }));     // never push during a load
    expect_eq(R::NO_FUNCTIONS, with([](auto& st) { st.can_run_functions = false; }));
    expect_eq(R::PROTECTED, with([](auto& st) { st.protected_character = true; }));
    expect_eq(R::MARKER, with([](auto& st) { st.has_snapshot = false; }));
    expect_eq(R::MARKER, with([](auto& st) { st.run_in_progress = false; }));
    expect_eq(R::MARKER, with([](auto& st) { st.restore_pending = true; }));
    expect_eq(R::MARKER, with([](auto& st) { st.awaiting_save = true; }));
    expect_eq(R::RESTORE_RUNNING, with([](auto& st) { st.restore_running = true; }));
    // the first refusal wins: without a return to base the floor does not matter
    expect_eq(R::NOT_RETURNED, with([](auto& st) { st.base_returned = false; st.at_start_floor = false; }));
    // Pioneer 2 and Forest 1 only
    expect(spire_run_again_floor_ok(0));
    expect(spire_run_again_floor_ok(1));
    expect(!spire_run_again_floor_ok(2));
    expect(!spire_run_again_floor_ok(0x0B));
    expect(!spire_run_again_floor_ok(0x0F)); // Client::floor's start value: no floor known yet
    // the answer: 'I' tag, refusal in bits 8-15, OK only without a refusal
    expect_eq(0x49000001u, spire_run_again_answer(R::NONE));
    expect_eq(0x49000300u, spire_run_again_answer(R::NOT_RETURNED));
    expect_eq(0x49000C00u, spire_run_again_answer(R::PUSH_NOT_VERIFIED));
    for (uint8_t z = 1; z <= static_cast<uint8_t>(R::PUSH_NOT_VERIFIED); z++) {
      expect_eq(SPIRE_RUN_AGAIN_FAIL, spire_run_again_answer(static_cast<R>(z)) & 0xFF);
      expect(std::string(spire_run_again_refusal_name(static_cast<R>(z))) != "unknown");
    }
    // the code letter is free: no other r248 code or 4CC starts with 'I'
    for (uint8_t letter : {SPIRE_BRIDGE_MSG_WAVE, SPIRE_BRIDGE_MSG_LIFE, SPIRE_BRIDGE_MSG_TIME, SPIRE_BRIDGE_MSG_PARTNER,
             SPIRE_BRIDGE_MSG_RUN_OVER, SPIRE_BRIDGE_MSG_PAUSE_B, SPIRE_BRIDGE_MSG_DOWN, SPIRE_BRIDGE_MSG_HINT,
             SPIRE_BRIDGE_MSG_APPROACH, SPIRE_BRIDGE_MSG_PROMPT, SPIRE_BRIDGE_REC_BEGIN, SPIRE_BRIDGE_REC_SCORE,
             SPIRE_BRIDGE_REC_FINISH, SPIRE_BRIDGE_REC_BOARD, SPIRE_BRIDGE_TIMER_START, SPIRE_BRIDGE_TIMER_END,
             SPIRE_BRIDGE_DRAW_REQUEST, SPIRE_BRIDGE_SAVESTATE_WRITE, SPIRE_BRIDGE_RESUME, SPIRE_BRIDGE_BASE_RETURN,
             static_cast<uint8_t>('N'), static_cast<uint8_t>('M'),
             static_cast<uint8_t>(SPIRE_BRIDGE_REQUEST_VALUE >> 24), static_cast<uint8_t>(SPIRE_BRIDGE_DONE_VALUE >> 24),
             static_cast<uint8_t>(SPIRE_BRIDGE_BATTLE_END_VALUE >> 24),
             static_cast<uint8_t>(SPIRE_BRIDGE_ROOM_CLEAR_VALUE >> 24)}) {
      expect(letter != SPIRE_BRIDGE_RUN_AGAIN);
    }
    // the same answer register as 'J' and 'O', each with its own tag
    expect_eq(SPIRE_RESUME_ANSWER_REGISTER, SPIRE_RUN_AGAIN_ANSWER_REGISTER);
    expect(SPIRE_RUN_AGAIN_TAG != SPIRE_RESUME_ANSWER_TAG);
    expect(SPIRE_RUN_AGAIN_TAG != SPIRE_BASE_RETURN_TAG);
  }

  phosg::log_info_f("-- Spire run again: the snapshot is taken again only after a verified return to base (v49)");
  {
    auto make = [](size_t n, uint8_t kind, uint32_t level, uint32_t meseta, const char* name, uint8_t section) {
      std::pair<PlayerInventory, PlayerDispDataV4> ret;
      ret.first.num_items = n;
      for (size_t z = 0; z < n; z++) {
        ret.first.items[z].data.data1[0] = kind;
        ret.first.items[z].data.data1[1] = 0x01 + z;
        ret.first.items[z].data.id = 0x00010000 + z;
      }
      ret.second.stats.level = level;
      ret.second.stats.meseta = meseta;
      ret.second.visual.name.encode(name, Language::ENGLISH);
      ret.second.visual.sh.section_id = section;
      return ret;
    };
    auto real = make(20, 0x00, 51, 1000, "BidX", 3);
    auto base_changed = make(19, 0x00, 51, 700, "BidX", 3); // dropped one item and spent meseta in the base
    auto other = make(20, 0x00, 51, 1000, "M4ia", 3);
    auto fresh = [&]() {
      SpireGuardSnapshot snap;
      snap.has_character = true;
      snap.run_in_progress = true;
      snap.quest_number = 921;
      snap.snapshot_time_usecs = 1;
      snap.inventory = real.first;
      snap.disp = real.second;
      auto printed = make(3, 0x00, 5, 0, "BidX", 3); // F1: the run copy's print, stored at 'O'
      snap.run_copy_print = spire_guard_run_copy_print(printed.first, printed.second);
      return snap;
    };
    // after 'O' OK: the 61 of the base is the real character, the snapshot takes it and RunInProgress stays
    auto spent = make(20, 0x00, 51, 700, "BidX", 3); // spent meseta in the base
    auto snap = fresh();
    expect(spire_guard_run_again_retake(snap, true, spent.first, spent.second, 921, 2));
    expect_eq(20u, static_cast<uint32_t>(snap.inventory.num_items));
    expect_eq(700u, snap.disp.stats.meseta.load());
    expect(snap.run_in_progress && !snap.restore_pending && !snap.awaiting_save && snap.has_character);
    expect_eq(2u, snap.snapshot_time_usecs);
    // v56 (live 2026-10-26 20:47:39): never fewer items, a lower level or less EXP than the stored snapshot; the
    // stored one stays. One item fewer, the run copy (Lv6, 3 items), the same level with less EXP:
    snap = fresh();
    expect(!spire_guard_run_again_retake(snap, true, base_changed.first, base_changed.second, 921, 2));
    expect_eq(20u, static_cast<uint32_t>(snap.inventory.num_items));
    expect_eq(1u, snap.snapshot_time_usecs);
    auto run_copy = make(3, 0x00, 5, 0, "BidX", 3);
    expect(!spire_guard_run_again_retake(snap, true, run_copy.first, run_copy.second, 921, 2));
    expect_eq(51u, snap.disp.stats.level.load());
    snap.disp.stats.exp = 90000;
    auto less_exp = make(20, 0x00, 51, 1000, "BidX", 3);
    less_exp.second.stats.exp = 89999;
    expect(!spire_guard_snapshot_not_worse(less_exp.first, less_exp.second, snap));
    expect(!spire_guard_run_again_retake(snap, true, less_exp.first, less_exp.second, 921, 2));
    expect_eq(90000u, snap.disp.stats.exp.load());
    // more items and a higher level are not worse
    auto gained = make(21, 0x00, 52, 0, "BidX", 3);
    gained.second.stats.exp = 95000;
    expect(spire_guard_snapshot_not_worse(gained.first, gained.second, snap));
    expect(!spire_guard_snapshot_not_worse(other.first, other.second, fresh()));
    // no verified return to base (or used up): nothing changes
    snap = fresh();
    expect(!spire_guard_run_again_retake(snap, false, spent.first, spent.second, 921, 2));
    expect_eq(20u, static_cast<uint32_t>(snap.inventory.num_items));
    expect_eq(1u, snap.snapshot_time_usecs);
    // another character on the card, or another marker: nothing changes
    snap = fresh();
    expect(!spire_guard_run_again_retake(snap, true, other.first, other.second, 921, 2));
    expect_eq(1000u, snap.disp.stats.meseta.load());
    snap = fresh();
    snap.restore_pending = true;
    expect(!spire_guard_run_again_retake(snap, true, spent.first, spent.second, 921, 2));
    snap = fresh();
    snap.awaiting_save = true;
    expect(!spire_guard_run_again_retake(snap, true, spent.first, spent.second, 921, 2));
    snap = fresh();
    snap.run_in_progress = false;
    expect(!spire_guard_run_again_retake(snap, true, spent.first, spent.second, 921, 2));
    snap = fresh();
    snap.has_character = false;
    expect(!spire_guard_run_again_retake(snap, true, spent.first, spent.second, 921, 2));
  }

  phosg::log_info_f("-- Spire guard: the game exit after 'O' and 'I' restores, never a stale snapshot (v51)");
  {
    auto make = [](size_t n, uint32_t level, uint32_t exp, uint32_t meseta, const char* name) {
      std::pair<PlayerInventory, PlayerDispDataV4> ret;
      ret.first.num_items = n;
      for (size_t z = 0; z < n; z++) {
        ret.first.items[z].data.data1[1] = 0x01 + z;
        ret.first.items[z].data.id = 0x00010000 + z;
      }
      ret.second.stats.level = level;
      ret.second.stats.exp = exp;
      ret.second.stats.meseta = meseta;
      ret.second.visual.name.encode(name, Language::ENGLISH);
      ret.second.visual.sh.section_id = 3;
      return ret;
    };
    auto real = make(20, 51, 90000, 1000, "BidX");
    auto base_changed = make(21, 51, 90000, 700, "BidX"); // took an item from the bank, bought one, in the base
    auto run_copy = make(4, 0, 0, 0, "BidX");              // the overlay of a run (the run copy's level and EXP)
    auto at_run_start = [&]() {
      SpireGuardSnapshot snap;
      snap.has_character = true;
      snap.run_in_progress = true;
      snap.inventory = real.first;
      snap.disp = real.second;
      snap.run_copy_print = spire_guard_run_copy_print(run_copy.first, run_copy.second); // F1: stored at 'O'
      return snap;
    };
    // the game exit: spire_guard_end_run turns RunInProgress into RestorePending (SpireFlagBridge.cc, the 98)
    auto end_run = [](SpireGuardSnapshot& snap) {
      expect(snap.run_in_progress && !snap.restore_pending);
      snap.run_in_progress = false;
      snap.restore_pending = true;
    };
    // the v47 restore at the 84 runs exactly when this gate is open
    auto exit_restore_runs = [](const SpireGuardSnapshot& snap) {
      return spire_guard_marker_open(false, false, snap.restore_pending, false);
    };

    // (a) 'O' OK, changes in the base, leave: the snapshot follows the 98, so the exit push is the real character
    // as it is now (the run start's 20 items would push the bank item away)
    auto snap = at_run_start();
    end_run(snap);
    expect(spire_guard_base_exit_follow(snap, true, base_changed.first, base_changed.second));
    expect_eq(21u, static_cast<uint32_t>(snap.inventory.num_items));
    expect_eq(700u, snap.disp.stats.meseta.load());
    expect(snap.restore_pending && !snap.run_in_progress && !snap.awaiting_save);
    expect(exit_restore_runs(snap)); // pushes what the client already holds: verified, AwaitingSave, no overlay
    // (a) 'O' OK, nothing changed in the base: the snapshot stays equal, the exit push changes nothing
    snap = at_run_start();
    end_run(snap);
    expect(spire_guard_base_exit_follow(snap, true, real.first, real.second));
    expect(spire_guard_same_state(snap.inventory, snap.disp, real.first, real.second));
    // (a) a 98 that is not the real character (the run copy's level or EXP, another character): the run start stays
    snap = at_run_start();
    end_run(snap);
    expect(!spire_guard_base_exit_follow(snap, true, run_copy.first, run_copy.second));
    expect_eq(20u, static_cast<uint32_t>(snap.inventory.num_items));
    auto higher = make(21, 52, 95000, 700, "BidX");
    expect(!spire_guard_base_exit_follow(snap, true, higher.first, higher.second));
    auto other = make(21, 51, 90000, 700, "M4ia");
    expect(!spire_guard_base_exit_follow(snap, true, other.first, other.second));
    expect_eq(1000u, snap.disp.stats.meseta.load());
    // v56: the real level and EXP but fewer items than the snapshot: kept
    auto fewer = make(19, 51, 90000, 700, "BidX");
    expect(!spire_guard_base_exit_follow(snap, true, fewer.first, fewer.second));
    expect_eq(20u, static_cast<uint32_t>(snap.inventory.num_items));
    // (a) only after end_run: never while RunInProgress, AwaitingSave or without a character
    snap = at_run_start();
    expect(!spire_guard_base_exit_follow(snap, true, base_changed.first, base_changed.second));
    snap.run_in_progress = false;
    snap.awaiting_save = true;
    expect(!spire_guard_base_exit_follow(snap, true, base_changed.first, base_changed.second));

    // (b) 'O' failed (or never asked): no base_returned, the 98 (the run copy) is ignored, the run start's snapshot
    // is pushed at the exit
    snap = at_run_start();
    end_run(snap);
    expect(!spire_guard_base_exit_follow(snap, false, run_copy.first, run_copy.second));
    expect(!spire_guard_base_exit_follow(snap, false, base_changed.first, base_changed.second));
    expect_eq(20u, static_cast<uint32_t>(snap.inventory.num_items));
    expect_eq(1000u, snap.disp.stats.meseta.load());
    expect(exit_restore_runs(snap));

    // (c) 'O' OK, 'I' (the snapshot taken again in the base, the run copy pushed, the 'O' OK used up), then leave: the
    // 98 holds the run copy and is ignored, the exit pushes the snapshot taken at the 'I'
    snap = at_run_start();
    expect(spire_guard_run_again_retake(snap, true, base_changed.first, base_changed.second, 921, 2));
    end_run(snap);
    expect(!spire_guard_base_exit_follow(snap, false, run_copy.first, run_copy.second));
    expect_eq(21u, static_cast<uint32_t>(snap.inventory.num_items));
    expect_eq(700u, snap.disp.stats.meseta.load());
    expect(exit_restore_runs(snap));
    // a protected character never gets the exit restore
    expect(!spire_guard_marker_open(true, false, snap.restore_pending, false));
  }

  phosg::log_info_f("-- Spire guard: a run copy never becomes the snapshot at either re-take (F1, route B)");
  {
    using Bytes = std::array<uint8_t, 12>;
    // Measured mag bytes (routeb-mag-vraag.md): Kama LV200 (BidX), Pitri LV200 (M4ia); the HU start-floor run mag (plan F6)
    const Bytes kama = {0x02, 0x06, 0xC8, 0x50, 0x0C, 0x07, 0x0E, 0x30, 0x0E, 0x0E, 0xC2, 0x09};
    const Bytes pitri = {0x02, 0x1D, 0xC8, 0xA3, 0xF4, 0x01, 0x1C, 0x25, 0x10, 0x27, 0x00, 0x00};
    const Bytes run_mag = {0x02, 0x01, 0x16, 0x00, 0x4C, 0x04, 0xE8, 0x03, 0x00, 0x00, 0x64, 0x00};
    // n plain items (data1[1] numbers them from first), then the given ones; item IDs as a 61 has them
    auto make = [](const char* name, uint32_t level, uint32_t exp, uint32_t meseta, size_t n, uint8_t first,
                    std::vector<Bytes> extra) {
      std::pair<PlayerInventory, PlayerDispDataV4> ret;
      for (size_t z = 0; z < n; z++) {
        ret.first.items[z].data.data1[0] = 0x01;
        ret.first.items[z].data.data1[1] = first + z;
        ret.first.items[z].data.id = 0x00010000 + z;
      }
      for (size_t z = 0; z < extra.size(); z++) {
        memcpy(ret.first.items[n + z].data.data1.data(), extra[z].data(), 12);
        ret.first.items[n + z].data.id = 0x00010100 + z;
      }
      ret.first.num_items = n + extra.size();
      ret.second.stats.level = level - 1; // stored 0-based
      ret.second.stats.exp = exp;
      ret.second.stats.meseta = meseta;
      ret.second.visual.name.encode(name, Language::ENGLISH);
      ret.second.visual.sh.section_id = 3;
      return ret;
    };
    auto snapshot_of = [](const std::pair<PlayerInventory, PlayerDispDataV4>& real, bool run_in_progress) {
      SpireGuardSnapshot snap;
      snap.has_character = true;
      snap.run_in_progress = run_in_progress;
      snap.restore_pending = !run_in_progress; // the 98 after 'O' is checked after spire_guard_end_run
      snap.quest_number = 921;
      snap.snapshot_time_usecs = 1;
      snap.inventory = real.first;
      snap.disp = real.second;
      return snap;
    };
    auto print_of = [](const std::pair<PlayerInventory, PlayerDispDataV4>& d) {
      return spire_guard_run_copy_print(d.first, d.second);
    };
    auto unchanged = [](const SpireGuardSnapshot& snap, const std::pair<PlayerInventory, PlayerDispDataV4>& real) {
      return spire_guard_same_state(snap.inventory, snap.disp, real.first, real.second) && (snap.snapshot_time_usecs == 1);
    };
    auto f1_case = [](const char* name, const std::function<void()>& fn) {
      phosg::log_info_f("   F1 ({})", name);
      fn();
    };

    f1_case("a: a stronger run copy with the stored print", [&]() {
      // The test character Lv5, EXP 300, 3 items; the live player after 'O' is still the run copy Lv19 with 12 items
      auto real = make("Testa", 5, 300, 50, 3, 0x01, {});
      auto run_copy = make("Testa", 19, 9000, 400, 12, 0x20, {});
      auto snap = snapshot_of(real, true);
      snap.run_copy_print = print_of(run_copy);
      expect(spire_guard_snapshot_not_worse(run_copy.first, run_copy.second, snap)); // the v56 rule alone lets it in
      expect(!spire_guard_retake_ok(run_copy.first, run_copy.second, snap));
      expect(!spire_guard_run_again_retake(snap, true, run_copy.first, run_copy.second, 921, 2));
      expect(unchanged(snap, real));
      // the in-game 61 is the save form (equipped items only): the print's level and EXP still match
      auto save_form = make("Testa", 19, 9000, 400, 4, 0x20, {});
      expect(!spire_guard_run_again_retake(snap, true, save_form.first, save_form.second, 921, 2));
      // the same items and meseta at another level (hash only) match too
      auto same_items = make("Testa", 20, 9100, 400, 12, 0x20, {});
      expect(spire_guard_matches_print(same_items.first, same_items.second, snap.run_copy_print));
      expect(!spire_guard_run_again_retake(snap, true, same_items.first, same_items.second, 921, 2));
      expect(unchanged(snap, real));
      // the 98 after 'O' at the snapshot's level and EXP: the hash finds the run copy's items
      auto equal_stats = make("Testa", 5, 300, 400, 12, 0x20, {});
      auto exit_snap = snapshot_of(real, false);
      exit_snap.run_copy_print = print_of(equal_stats);
      expect(!spire_guard_base_exit_follow(exit_snap, true, equal_stats.first, equal_stats.second));
      expect(unchanged(exit_snap, real));
    });

    f1_case("b: a run copy holding the run mag, with a different print: level and EXP decide (no run mag term, f6 C1)", [&]() {
      auto real = make("Testa", 5, 300, 50, 3, 0x01, {});
      auto run_copy = make("Testa", 19, 9000, 400, 11, 0x20, {run_mag});
      auto snap = snapshot_of(real, true);
      snap.run_copy_print = print_of(make("Testa", 18, 8000, 300, 10, 0x40, {}));
      expect(!spire_guard_matches_print(run_copy.first, run_copy.second, snap.run_copy_print));
      expect(!spire_guard_retake_ok(run_copy.first, run_copy.second, snap));
      expect(!spire_guard_run_again_retake(snap, true, run_copy.first, run_copy.second, 921, 2));
      expect(unchanged(snap, real));
      // the 98 after 'O' with the snapshot's level and EXP but the printed run copy's items: the print refuses it
      auto exit_copy = make("Testa", 5, 300, 400, 11, 0x20, {run_mag});
      auto exit_snap = snapshot_of(real, false);
      exit_snap.run_copy_print = print_of(exit_copy);
      expect(!spire_guard_base_exit_follow(exit_snap, true, exit_copy.first, exit_copy.second));
      expect(unchanged(exit_snap, real));
    });

    f1_case("c: the real character after a verified restore plus one bank item", [&]() {
      auto real = make("BidX", 52, 90000, 1000, 19, 0x01, {kama});
      auto with_bank_item = make("BidX", 52, 90000, 1000, 20, 0x01, {kama});
      auto snap = snapshot_of(real, true);
      snap.run_copy_print = print_of(make("BidX", 19, 9000, 400, 12, 0x20, {}));
      expect(spire_guard_retake_ok(with_bank_item.first, with_bank_item.second, snap));
      expect(spire_guard_run_again_retake(snap, true, with_bank_item.first, with_bank_item.second, 921, 2));
      expect_eq(21u, static_cast<uint32_t>(snap.inventory.num_items));
      expect(snap.run_in_progress && (snap.snapshot_time_usecs == 2));
      auto exit_snap = snapshot_of(real, false);
      exit_snap.run_copy_print = snap.run_copy_print;
      expect(spire_guard_base_exit_follow(exit_snap, true, with_bank_item.first, with_bank_item.second));
      expect_eq(21u, static_cast<uint32_t>(exit_snap.inventory.num_items));
      // when in doubt the snapshot stays: no print stored at 'O'
      snap = snapshot_of(real, true);
      expect(!spire_guard_retake_ok(with_bank_item.first, with_bank_item.second, snap));
      expect(!spire_guard_run_again_retake(snap, true, with_bank_item.first, with_bank_item.second, 921, 2));
      expect(unchanged(snap, real));
    });

    f1_case("d: the live case of 2026-10-26 20:47:39 (B-BASIS-60)", [&]() {
      // M4ia Lv162 with 27 items (Pitri among them); the 'I' 61 showed the run copy Lv17 with 3 items, no mag
      auto real = make("M4ia", 162, 4000000, 90000, 26, 0x01, {pitri});
      auto run_copy = make("M4ia", 17, 4200, 0, 3, 0x60, {});
      auto snap = snapshot_of(real, true);
      for (bool printed : {false, true}) {
        snap.run_copy_print = printed ? print_of(make("M4ia", 17, 4200, 0, 18, 0x60, {})) : SpireRunCopyPrint();
        expect(!spire_guard_retake_ok(run_copy.first, run_copy.second, snap));
        expect(!spire_guard_run_again_retake(snap, true, run_copy.first, run_copy.second, 921, 2));
        expect(unchanged(snap, real));
      }
    });

    f1_case("e: a stronger copy without the real mag; the mag signature is gone (B-REGIE-14-9)", [&]() {
      // the snapshot holds the real mag; the stronger candidate holds none: refused by the level and EXP equality
      auto real = make("BidX", 52, 90000, 1000, 19, 0x01, {kama});
      auto snap = snapshot_of(real, true);
      snap.run_copy_print = print_of(make("BidX", 19, 9000, 400, 12, 0x20, {}));
      auto stronger = make("BidX", 60, 120000, 2000, 24, 0x01, {});
      expect(!spire_guard_matches_print(stronger.first, stronger.second, snap.run_copy_print));
      expect(spire_guard_snapshot_not_worse(stronger.first, stronger.second, snap)); // the v56 rule alone lets it in
      expect(!spire_guard_retake_ok(stronger.first, stronger.second, snap));
      expect(!spire_guard_run_again_retake(snap, true, stronger.first, stronger.second, 921, 2));
      expect(unchanged(snap, real));
      // holding a run mag instead: still a run copy, refused by its level and EXP
      auto with_run_mag = make("BidX", 60, 120000, 2000, 23, 0x01, {run_mag});
      expect(!spire_guard_run_again_retake(snap, true, with_run_mag.first, with_run_mag.second, 921, 2));
      // B-REGIE-14-9 (C3): the real level and EXP without the mag (deposited in the base) is the real character; before
      // the fix the signature refused it, and the exit restore then pushed the deposited mag back next to the bank copy
      auto exit_copy = make("BidX", 52, 90000, 2000, 24, 0x01, {});
      auto exit_snap = snapshot_of(real, false);
      exit_snap.run_copy_print = snap.run_copy_print;
      expect(spire_guard_base_exit_follow(exit_snap, true, exit_copy.first, exit_copy.second));
      expect(spire_guard_same_state(exit_snap.inventory, exit_snap.disp, exit_copy.first, exit_copy.second));
      // the real mag back (fed in the base: other stat bytes), the re-take goes on
      Bytes fed = kama;
      fed[3] = 0x52;
      auto real_fed = make("BidX", 52, 90000, 1000, 19, 0x01, {fed});
      expect(spire_guard_run_again_retake(snap, true, real_fed.first, real_fed.second, 921, 2));
      // a snapshot without a mag
      auto no_mag = make("Testa", 5, 300, 50, 3, 0x01, {});
      auto grown = make("Testa", 5, 300, 50, 4, 0x01, {});
      auto plain = snapshot_of(no_mag, true);
      plain.run_copy_print = print_of(make("Testa", 19, 9000, 400, 12, 0x20, {}));
      expect(spire_guard_run_again_retake(plain, true, grown.first, grown.second, 921, 2));
    });

    f1_case("print: kept in the run, cleared by the next run start", [&]() {
      auto real = make("Testa", 5, 300, 50, 3, 0x01, {});
      auto run_copy = make("Testa", 19, 9000, 400, 12, 0x20, {});
      auto print = print_of(run_copy);
      expect(print.exists && (print.level == 18));
      // the item order and the item IDs do not change the hash; the meseta does
      auto reordered = run_copy;
      std::swap(reordered.first.items[0], reordered.first.items[11]);
      reordered.first.items[3].data.id = 0x00810003;
      expect_eq(print.hash, print_of(reordered).hash);
      auto more_meseta = run_copy;
      more_meseta.second.stats.meseta = 401;
      expect(print.hash != print_of(more_meseta).hash);
      SpireGuardSnapshot snap;
      snap.run_copy_print = print;
      expect_eq(SpireGuardRunStart::FRESH, spire_guard_run_start_apply(snap, real.first, real.second, 921, 1));
      expect(!snap.run_copy_print.exists);
      snap.run_copy_print = print; // an open run keeps its print
      expect_eq(SpireGuardRunStart::KEPT_OPEN_RUN, spire_guard_run_start_apply(snap, run_copy.first, run_copy.second, 921, 2));
      expect(snap.run_copy_print.exists);
    });

    f1_case("f: hard stop mid-run, the login 61 is the run copy's save form (amendment 7)", [&]() {
      // The card holds the run copy's save form after a hard stop: the equipped run items and the equipped run mag, never
      // the real mag (routeb-mag-vraag.md section 2 point 5). The run mag here has the real mag's kind on purpose.
      Bytes same_kind_run_mag = pitri;
      for (size_t z = 2; z < 12; z++) {
        same_kind_run_mag[z] = run_mag[z];
      }
      auto real = make("M4ia", 162, 4000000, 90000, 26, 0x01, {pitri});
      auto save_form = make("M4ia", 17, 4200, 0, 3, 0x60, {same_kind_run_mag});
      auto snap = snapshot_of(real, true);
      // the login of a RunInProgress snapshot turns it into RestorePending (spire_guard_restore_if_needed); under
      // AwaitingSave alone the same 61 is no confirmation either
      SpireGuardSnapshot awaiting = snap;
      awaiting.run_in_progress = false;
      awaiting.awaiting_save = true;
      expect(!spire_guard_awaiting_save_login_apply(awaiting, save_form.first, save_form.second));
      expect(awaiting.restore_pending && unchanged(awaiting, real));
      // the verification of the 61 after the push (spire_guard_verify_restore): level and EXP, items and meseta, materials
      auto verified = [&](const std::pair<PlayerInventory, PlayerDispDataV4>& after) {
        bool same_stats = (after.second.stats.level == snap.disp.stats.level) && (after.second.stats.exp == snap.disp.stats.exp);
        bool same_items = spire_guard_same_inventory(after.first, after.second.stats.meseta, snap.inventory, snap.disp.stats.meseta) &&
            spire_guard_same_materials(after.first, snap.inventory);
        return spire_guard_push_verified(same_stats, same_items);
      };
      expect(!verified(save_form)); // the push did not land: still pending
      // the real level, EXP and item count, but the run mag in the real mag's place (same data1[0-1]): still pending
      auto run_mag_in_place = make("M4ia", 162, 4000000, 90000, 26, 0x01, {same_kind_run_mag});
      expect(!verified(run_mag_in_place));
      // only the real mag's 12 bytes back verify the restore
      auto restored = make("M4ia", 162, 4000000, 90000, 26, 0x01, {pitri});
      std::swap(restored.first.items[0], restored.first.items[26]); // another order and other IDs, as a 61 has them
      restored.first.items[5].data.id = 0x00210005;
      expect(verified(restored));
      // and the save form never becomes the snapshot by a re-take either
      snap.run_copy_print = print_of(make("M4ia", 17, 4200, 0, 18, 0x60, {same_kind_run_mag}));
      expect(!spire_guard_run_again_retake(snap, true, save_form.first, save_form.second, 921, 2));
    });

    f1_case("g: the real character holds Sega's unfed Mag LV5, the bytes of a bonus-0 step-0 run mag (routeb-f6-critic C1)", [&]() {
      // Sega's starting mag (kind 0, LV5, DEF 5.00, no photon blast) is byte for byte the run mag of a run without bought
      // levels before its first evolve (spire_mag_step_bytes(line, 0, 0, std::nullopt), F6 test "HU start floor L22").
      // Real characters hold one (routeb-mag-vraag.md: M4ia, Nyra, Wakiru), so these bytes never identify a run copy.
      const Bytes mag_lv5 = {0x02, 0x00, 0x05, 0x00, 0xF4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
      // A short bonus-0 run that ended before the first evolve; the real character (spare Mag LV5 next to Pitri) is back
      // at the snapshot's exact level and EXP; the base changed its items (one bank withdrawal)
      auto real = make("Nyra", 30, 52000, 3000, 14, 0x01, {pitri, mag_lv5});
      auto after_base = make("Nyra", 30, 52000, 3000, 15, 0x01, {pitri, mag_lv5});
      auto run_copy = make("Nyra", 1, 0, 0, 3, 0x60, {mag_lv5}); // the run copy as 'O' printed it
      // the 'I' re-take (run again)
      auto snap = snapshot_of(real, true);
      snap.run_copy_print = print_of(run_copy);
      expect(!spire_guard_matches_print(after_base.first, after_base.second, snap.run_copy_print));
      expect(spire_guard_retake_ok(after_base.first, after_base.second, snap));
      expect(spire_guard_run_again_retake(snap, true, after_base.first, after_base.second, 921, 2));
      expect_eq(17u, static_cast<uint32_t>(snap.inventory.num_items));
      expect(spire_guard_same_state(snap.inventory, snap.disp, after_base.first, after_base.second));
      // the 98 after 'O': the base's changes follow into the snapshot, so the exit restore pushes them, not the run start
      auto exit_snap = snapshot_of(real, false);
      exit_snap.run_copy_print = print_of(run_copy);
      expect(spire_guard_base_exit_follow(exit_snap, true, after_base.first, after_base.second));
      expect(spire_guard_same_state(exit_snap.inventory, exit_snap.disp, after_base.first, after_base.second));
      // the run copy itself is still refused: its level and EXP differ from the snapshot's, and its 98 matches the print
      auto again = snapshot_of(real, true);
      again.run_copy_print = print_of(run_copy);
      expect(!spire_guard_run_again_retake(again, true, run_copy.first, run_copy.second, 921, 2));
      expect(unchanged(again, real));
    });

    // Fix round 1 (routeb-f1-critic.md C1-C5, B-REGIE-14-9). Every check is logged before any is asserted, so one run
    // shows each one that fails.
    std::vector<std::string> fix_failures;
    auto check = [&](const std::string& what, bool ok) {
      phosg::log_info_f("   F1 fix check {}: {}", what, ok ? "ok" : "FAILS");
      if (!ok) {
        fix_failures.emplace_back(what);
      }
    };
    const Bytes plain_mag = {0x02, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // a fresh Mag
    const Bytes sato = {0x02, 0x2A, 0xC8, 0x00, 0xF4, 0x01, 0xE8, 0x03, 0xD0, 0x07, 0x00, 0x00};

    f1_case("C1: the print's level with another EXP, no mag in the snapshot", [&]() {
      // GC: the overlay's EXP stays the start value (0 from Lv1), so the print holds {Lv19, EXP 0}. The 'I' 61 is the
      // save form: Lv19 with its real EXP 9000 and four equipped items, one of them a step-0 run mag of the plain kind.
      auto real = make("Testa", 5, 300, 50, 3, 0x01, {});
      auto snap = snapshot_of(real, true);
      snap.run_copy_print = print_of(make("Testa", 19, 0, 400, 12, 0x20, {}));
      auto save_form = make("Testa", 19, 9000, 400, 3, 0x20, {plain_mag});
      check("C1 retake_ok refuses the save form", !spire_guard_retake_ok(save_form.first, save_form.second, snap));
      check("C1 'I' refuses the save form",
          !spire_guard_run_again_retake(snap, true, save_form.first, save_form.second, 921, 2));
      check("C1 the snapshot stays", unchanged(snap, real));
      // the same with a snapshot that holds the same plain mag kind
      auto real_mag = make("Testa", 5, 300, 50, 3, 0x01, {plain_mag});
      auto snap_mag = snapshot_of(real_mag, true);
      snap_mag.run_copy_print = snap.run_copy_print;
      check("C1 'I' refuses the save form (snapshot with a plain mag)",
          !spire_guard_run_again_retake(snap_mag, true, save_form.first, save_form.second, 921, 2));
      // C2: the old rule (not worse) against the new one (exact): the snapshot's level, one EXP point more
      auto fresh = snapshot_of(real, true);
      fresh.run_copy_print = snap_mag.run_copy_print;
      auto one_more = make("Testa", 5, 301, 50, 4, 0x01, {});
      check("C2 'I' refuses the snapshot's level with one EXP point more",
          !spire_guard_run_again_retake(fresh, true, one_more.first, one_more.second, 921, 2));
      // and the real character (exact level and EXP, more items from the bank) still passes
      fresh = snapshot_of(real, true);
      fresh.run_copy_print = snap_mag.run_copy_print;
      auto real_plus = make("Testa", 5, 300, 50, 4, 0x01, {});
      check("C2 'I' takes the real character with a bank item",
          spire_guard_run_again_retake(fresh, true, real_plus.first, real_plus.second, 921, 2));
    });

    f1_case("C3: a bank mag swap in the base (deposit Kama, withdraw Sato)", [&]() {
      auto real = make("BidX", 52, 90000, 1000, 19, 0x01, {kama});
      auto swapped = make("BidX", 52, 90000, 1000, 19, 0x01, {sato});
      auto print = print_of(make("BidX", 19, 0, 400, 12, 0x20, {}));
      auto snap = snapshot_of(real, true);
      snap.run_copy_print = print;
      check("C3 'I' takes the swapped mag", spire_guard_run_again_retake(snap, true, swapped.first, swapped.second, 921, 2));
      check("C3 the snapshot holds Sato, not Kama", spire_guard_same_state(snap.inventory, snap.disp, swapped.first, swapped.second));
      auto exit_snap = snapshot_of(real, false);
      exit_snap.run_copy_print = print;
      check("C3 the 98 after 'O' takes the swapped mag",
          spire_guard_base_exit_follow(exit_snap, true, swapped.first, swapped.second));
      check("C3 the exit restore pushes Sato, not Kama",
          spire_guard_same_state(exit_snap.inventory, exit_snap.disp, swapped.first, swapped.second));
    });

    f1_case("C2/C4/C5: the guard file (round trip, print lifecycle, a bad field)", [&]() {
      auto dir = std::filesystem::temp_directory_path() / std::format("newserv-static-spire-guardfile-{}", getpid());
      std::filesystem::remove_all(dir);
      std::filesystem::create_directories(dir);
      auto name = (dir / "0000000042.json").string();
      auto real = make("Testa", 5, 300, 50, 3, 0x01, {plain_mag});
      auto run_copy = make("Testa", 19, 0, 400, 12, 0x20, {});
      auto snap = snapshot_of(real, true);
      snap.run_copy_print = print_of(run_copy);
      snap.run_copy_print.hash = 0xFEDCBA9876543210; // above 2^63: stored as hex text
      spire_guard_save(name, 42, snap);
      auto loaded = spire_guard_load(name);
      check("C2 round trip: markers", loaded.run_in_progress && !loaded.restore_pending && !loaded.awaiting_save);
      check("C2 round trip: character", loaded.has_character && unchanged(loaded, real) && (loaded.quest_number == 921));
      check("C2 round trip: print", loaded.run_copy_print.exists && (loaded.run_copy_print.level == 18) &&
          (loaded.run_copy_print.hash == 0xFEDCBA9876543210));
      // C4: an 'O' without a run copy to print leaves no print of an earlier 'O' in the file
      auto second = spire_guard_load(name);
      check("C4 storing no print succeeds", spire_guard_store_print(name, 42, second, nullptr, nullptr));
      check("C4 no stale print in the file", !spire_guard_load(name).run_copy_print.exists);
      // C4: an 'O' with a run copy stores exactly that run copy's print
      auto other_copy = make("Testa", 22, 0, 900, 14, 0x40, {});
      check("C4 storing the print succeeds", spire_guard_store_print(name, 42, second, &other_copy.first, &other_copy.second));
      auto stored = spire_guard_load(name).run_copy_print;
      check("C4 the new print is in the file", stored.exists && (stored.level == 21) && (stored.hash == print_of(other_copy).hash));
      // C4: a print that cannot be saved is reported, so the caller refuses the push
      auto unwritable = (dir / "missing" / "0000000042.json").string();
      check("C4 a failed save is reported",
          !spire_guard_store_print(unwritable, 42, second, &other_copy.first, &other_copy.second));
      // C5: a bad print or overlay block reads as none; the snapshot itself loads (no login lockout)
      for (const std::string bad_print : {"{\"Level\": 18, \"Hash\": \"XYZ\"}", "{\"Level\": 18, \"Hash\": 7}",
               "{\"Level\": \"abc\", \"Hash\": \"0000000000000001\"}"}) {
        auto json = phosg::JSON::parse(phosg::load_file(name));
        json.as_dict().erase("RunCopyPrint");
        json.as_dict().erase("Overlay");
        json.emplace("RunCopyPrint", phosg::JSON::parse(bad_print));
        json.emplace("Overlay", phosg::JSON::parse("{\"Level\": \"x\"}"));
        phosg::save_file(name, json.serialize());
        bool threw = false;
        SpireGuardSnapshot tolerant;
        try {
          tolerant = spire_guard_load(name);
        } catch (const std::exception&) {
          threw = true;
        }
        check("C5 no throw for " + bad_print, !threw);
        check("C5 reads as no print, the snapshot stays, for " + bad_print,
            !tolerant.run_copy_print.exists && tolerant.has_character && unchanged(tolerant, real) && tolerant.run_in_progress);
      }
      std::filesystem::remove_all(dir);
    });

    for (const auto& what : fix_failures) {
      phosg::log_error_f("   F1 fix check failed: {}", what);
    }
    expect(fix_failures.empty());
  }

  phosg::log_info_f("-- Spire ladder: the base can open a rung next to the ladder; BaseRung is no win (v49, B-BASIS-13)");
  {
    expect_eq(0, spire_meta_open_rung(0, 0));
    expect_eq(2, spire_meta_open_rung(0, 2)); // opened by the base, nothing won
    expect_eq(2, spire_meta_open_rung(2, 1)); // the win is higher
    expect_eq(3, spire_meta_open_rung(1, 3));
    expect_eq(3, spire_meta_open_rung(0, 7)); // never past Ultimate
    // the gate: a low character with BaseRung 2 for Run I enters Very Hard in a Battle game, not Ultimate
    expect(SpireLadderGate::RUNG_OPEN == spire_meta_ladder_gate(true, true, spire_meta_open_rung(0, 2), 2, 10, 79));
    expect(SpireLadderGate::REFUSED == spire_meta_ladder_gate(true, true, spire_meta_open_rung(0, 2), 3, 10, 89));
    expect(SpireLadderGate::REFUSED == spire_meta_ladder_gate(true, true, spire_meta_open_rung(0, 0), 1, 10, 19));
    // meta.json with BaseRung and no win: the open rung is BaseRung, the won rung stays 0
    auto dir = std::filesystem::temp_directory_path() / std::format("newserv-static-spire-baserung-{}", getpid());
    std::filesystem::create_directories(dir);
    auto name = (dir / "base.meta.json").string();
    phosg::save_file(name, "{\"SerialNumber\":42,\"Flags\":[],\"BaseRung\":{\"1\":2,\"2\":9}}");
    auto file = spire_meta_load_file(name);
    expect_eq(2, spire_meta_base_rung(file, 1));
    expect_eq(3, spire_meta_base_rung(file, 2)); // clamped
    expect_eq(0, spire_meta_ladder_rung(file.flags, 1)); // BaseRung is no win
    expect_eq(2, spire_meta_open_rung(file, 1));
    expect_eq(3, spire_meta_open_rung(file, 2));
    expect_eq(0, spire_meta_open_rung(file, 0)); // a quest of neither run
    expect(SpireLadderGate::RUNG_OPEN == spire_meta_ladder_gate(true, true, spire_meta_open_rung(file, 1), 2, 10, 79));
    // the AvailableIf of q982 (Run I Very Hard) opens with the combined rung
    auto expr = ResourceDASM::Expression::Node::parse("V_Difficulty == 2 && SPIRE_RUN1 >= 2");
    ExpressionEnvironment env{.num_players = 1, .section_id = 0, .event = 0, .v1_present = false,
        .difficulty = Difficulty::VERY_HARD, .quest_flags = nullptr, .challenge_records = nullptr, .team = nullptr,
        .spire_rooms = 0, .spire_run1 = spire_meta_open_rung(file, 1), .spire_run2 = 0};
    expect(env.evaluate(*expr) != 0);
    // a won rung above BaseRung still decides; a file without BaseRung has none
    file.flags[std::make_pair(uint8_t(2), SPIRE_META_FLAG_RUN1_WON)] = true; // Run I won on Very Hard
    expect_eq(3, spire_meta_open_rung(file, 1));
    phosg::save_file(name, "{\"SerialNumber\":42,\"Flags\":[]}");
    expect(spire_meta_load_file(name).base_rung.empty());
    expect_eq(0, spire_meta_open_rung(spire_meta_load_file(name), 1));
    std::filesystem::remove_all(dir);
  }

  phosg::log_info_f("-- Spire records route B F2: 3 per room and 10 per boss when the checkpoint is stored, once per run and k, pacts at 'F'");
  {
    auto dir = std::filesystem::temp_directory_path() / std::format("newserv-static-spire-splinters-{}", getpid());
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto name = (dir / "0000000042.char.records.json").string();
    const SpireShopPayout payout{.per_room = 3, .per_boss = 10, .pact_tenths = 1};
    auto balance = [&]() { return spire_records_load(name).splinters; };
    // a run's rooms k1-k10 with bosses at k4 (Dragon) and k8 (De Rol Le), each paid when its checkpoint is stored
    auto credit_run = [&](uint32_t run_id, uint8_t last_k) {
      uint32_t sum = 0;
      for (uint8_t k = 1; k <= last_k; k++) {
        sum += spire_records_credit_room_file(name, 42, payout, run_id, k, (k == 4) || (k == 8), false);
      }
      return sum;
    };
    // a freeze after k6 (boss k4), no 'F': 6 * 3 + 10 = 28, kept in records.json
    expect_eq(28u, credit_run(0x11111111, 6));
    expect_eq(28u, balance());
    expect_eq(28u, spire_records_load(name).splinters_total);
    // the resume keeps the run id: k1-k6 again pay nothing, k7-k10 pay the rest: 10 rooms and 2 bosses give 50
    expect_eq(22u, credit_run(0x11111111, 10));
    expect_eq(50u, balance());
    // the same k twice pays 3 once (a respawn reloads the floor); the file keeps the set, also over a restart
    expect_eq(0u, spire_records_credit_room_file(name, 42, payout, 0x11111111, 3, false, false));
    expect_eq(0u, spire_records_credit_room_file(name, 42, payout, 0x11111111, 4, true, false));
    auto rec = spire_records_load(name);
    expect_eq(1u, rec.credited.size());
    expect_eq(0x11111111u, rec.credited[0].run_id);
    expect_eq(10u, rec.credited[0].rooms.size());
    expect(rec.credited[0].bosses == (std::set<uint8_t>{4, 8}));
    expect_eq(50u, rec.credited[0].base);
    // 'F' with 4 pacts adds only the pact bonus floor(50 * 4 * 1 / 10) = 20: exactly 70, never 50 + 50 + 20 = 120. The
    // rooms of this game passed in are paid already (no catch-up), and the old V (10 rooms, 2 bosses) is not paid
    std::set<uint8_t> rooms, boss_rooms{4, 8};
    for (uint8_t k = 1; k <= 10; k++) {
      rooms.emplace(k);
    }
    auto fin = spire_records_finish_splinters(rec, &payout, 0x11111111, 4, rooms, boss_rooms, 10, 2);
    expect_eq(20u, fin.added);
    expect_eq(20u, fin.pact_bonus);
    expect_eq(0u, fin.caught_up);
    expect_eq(50u, fin.base);
    expect(!fin.legacy);
    expect(fin.warning.empty());
    spire_records_save(name, 42, rec);
    expect_eq(70u, balance());
    // a second 'F' of the run pays nothing; F2 fix (M1): a later room of the run (resumed after a partner-left 'F')
    // is still paid, and its bonus difference comes with the next 'F'
    rec = spire_records_load(name);
    expect_eq(0u, spire_records_finish_splinters(rec, &payout, 0x11111111, 4, rooms, boss_rooms, 10, 2).added);
    expect_eq(70u, rec.splinters);
    expect_eq(3u, spire_records_credit_room(rec, payout, 0x11111111, 11, false, false));
    expect_eq(73u, rec.splinters);
    // a new run id pays again; a practice run pays 0; run id 0 (no run) pays 0
    expect_eq(3u, spire_records_credit_room_file(name, 42, payout, 0x22222222, 3, false, false));
    expect_eq(0u, spire_records_credit_room_file(name, 42, payout, 0x33333333, 3, false, true));
    expect_eq(0u, spire_records_credit_room_file(name, 42, payout, 0, 3, false, false));
    expect_eq(73u, balance());
    expect_eq(73u, spire_records_load(name).splinters_total);
    // an arrival may store a boss room before its clear: the clear still pays the boss
    expect_eq(3u, spire_records_credit_room_file(name, 42, payout, 0x22222222, 4, false, false));
    expect_eq(10u, spire_records_credit_room_file(name, 42, payout, 0x22222222, 4, true, false));
    // pacts 0: 'F' adds nothing; 2 pacts on 50: 60 (the numbers table)
    {
      SpireRecords r;
      for (uint8_t k = 1; k <= 10; k++) {
        spire_records_credit_room(r, payout, 7, k, (k == 4) || (k == 8), false);
      }
      SpireRecords r0 = r;
      expect_eq(0u, spire_records_finish_splinters(r0, &payout, 7, 0, {}, {}, 10, 2).added);
      expect_eq(50u, r0.splinters);
      expect_eq(10u, spire_records_finish_splinters(r, &payout, 7, 2, {}, {}, 10, 2).added);
      expect_eq(60u, r.splinters);
    }
    // catch-up at 'F': rooms of this game the checkpoint could not pay (the catalogue was unreadable then)
    {
      SpireRecords r;
      spire_records_credit_room(r, payout, 8, 1, false, false);
      auto f = spire_records_finish_splinters(r, &payout, 8, 0, {1, 2, 3, 4}, {4}, 4, 1);
      expect_eq(19u, f.caught_up); // k2, k3, k4 + boss k4
      expect_eq(22u, f.base);
      expect_eq(22u, r.splinters);
    }
    // no Payout block: the old V at 'F', rooms + 5 per boss = 20 for 10 rooms and 2 bosses, plus a warning
    {
      SpireRecords r;
      auto f = spire_records_finish_splinters(r, nullptr, 9, 4, rooms, boss_rooms, 10, 2);
      expect_eq(20u, f.added);
      expect(f.legacy);
      expect(!f.warning.empty());
      expect_eq(20u, r.splinters);
      // no run id (a quest without 'B'): the old V too, never 0
      SpireRecords r2;
      auto f2 = spire_records_finish_splinters(r2, &payout, 0, 4, rooms, boss_rooms, 10, 2);
      expect_eq(20u, f2.added);
      expect(f2.legacy && !f2.warning.empty());
      // the Payout block gone after the rooms were paid: nothing more at 'F' (never the old V on top), a warning
      SpireRecords r3;
      spire_records_credit_room(r3, payout, 10, 1, false, false);
      auto f3 = spire_records_finish_splinters(r3, nullptr, 10, 4, {1}, {}, 1, 0);
      expect_eq(0u, f3.added);
      expect(!f3.warning.empty());
      expect_eq(3u, r3.splinters);
    }
    // the fallback formula itself, never wrapping
    expect_eq(20u, spire_splinters_for_run(10, 2, 0));
    expect_eq(0xFFFFFFFFu, spire_splinters_for_run(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF));
    // at most SPIRE_RECORDS_CREDITED_RUNS runs are kept, the oldest dropped first
    {
      SpireRecords r;
      for (uint32_t run = 1; run <= SPIRE_RECORDS_CREDITED_RUNS + 3; run++) {
        spire_records_credit_room(r, payout, run, 1, false, false);
      }
      expect_eq(SPIRE_RECORDS_CREDITED_RUNS, r.credited.size());
      expect_eq(4u, r.credited.front().run_id);
      expect_eq(static_cast<uint32_t>(SPIRE_RECORDS_CREDITED_RUNS + 3), r.credited.back().run_id);
    }
    std::filesystem::remove_all(dir);
  }

  phosg::log_info_f("-- Spire records route B F2 fix: rooms after a partner-left 'F', the bonus on a prior balance, a repeated 'F'");
  {
    // routeb-f2-critic.md M1, M2, L4. Every check is logged before any is asserted, so one run shows each that fails.
    std::vector<std::string> failures;
    auto check = [&](const std::string& what, bool ok) {
      phosg::log_info_f("   F2 fix check {}: {}", what, ok ? "ok" : "FAILS");
      if (!ok) {
        failures.emplace_back(what);
      }
    };
    auto dir = std::filesystem::temp_directory_path() / std::format("newserv-static-spire-f2fix-{}", getpid());
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const SpireShopPayout payout{.per_room = 3, .per_boss = 10, .pact_tenths = 1};
    std::set<uint8_t> no_rooms;

    // M1: duo run X, the partner freezes at k6, the timer's "player left" end keeps the savestates; the remaining
    // player's quest sends 'F' (2 pacts). Later both resume X (same run id) and finish k7-k10, then 'F' again.
    {
      auto name = (dir / "0000000042.m1.records.json").string();
      uint32_t first = 0;
      for (uint8_t k = 1; k <= 6; k++) {
        first += spire_records_credit_room_file(name, 42, payout, 0x51515151, k, k == 4, false);
      }
      check("M1 k1-k6 pay 28", first == 28);
      auto rec = spire_records_load(name);
      auto fin1 = spire_records_finish_splinters(rec, &payout, 0x51515151, 2, no_rooms, no_rooms, 6, 1);
      spire_records_save(name, 42, rec);
      check("M1 the first 'F' adds floor(28 * 2 / 10) = 5", (fin1.pact_bonus == 5) && (fin1.added == 5));
      uint32_t resumed = 0;
      for (uint8_t k = 7; k <= 10; k++) {
        resumed += spire_records_credit_room_file(name, 42, payout, 0x51515151, k, k == 8, false);
      }
      check("M1 the resumed k7-k10 pay 22 after the 'F'", resumed == 22);
      rec = spire_records_load(name);
      auto fin2 = spire_records_finish_splinters(rec, &payout, 0x51515151, 2, no_rooms, no_rooms, 4, 1);
      spire_records_save(name, 42, rec);
      check("M1 the second 'F' pays only the difference floor(50 * 2 / 10) - 5 = 5", (fin2.pact_bonus == 5) && (fin2.added == 5));
      rec = spire_records_load(name);
      check("M1 balance 28 + 5 + 22 + 5 = 60", rec.splinters == 60);
      check("M1 BonusPaid 10 is in the file", (rec.credited.size() == 1) && (rec.credited[0].bonus_paid == 10) &&
          (rec.credited[0].base == 50));
      // a resend of the last 'F' pays nothing
      auto fin3 = spire_records_finish_splinters(rec, &payout, 0x51515151, 2, no_rooms, no_rooms, 4, 1);
      check("M1 a resent 'F' pays 0", (fin3.added == 0) && (rec.splinters == 60));
      // an entry written before the fix ('Finished' without 'BonusPaid') never pays its bonus twice
      auto old_name = (dir / "0000000042.old.records.json").string();
      phosg::save_file(old_name, "{\"SerialNumber\": 42, \"BestScore\": 0, \"BestTime\": 0, \"Runs\": 1, \"Won\": 0, "
          "\"Splinters\": 70, \"SplintersTotal\": 70, \"Credited\": [{\"Run\": 7, \"Rooms\": [1], \"Bosses\": [], "
          "\"Base\": 50, \"Finished\": true}]}");
      auto old_rec = spire_records_load(old_name);
      check("M1 a pre-fix finished entry pays no bonus again",
          spire_records_finish_splinters(old_rec, &payout, 7, 4, no_rooms, no_rooms, 1, 0).added == 0);
    }

    // M2: a balance of 100 before the run; 'F' with 4 pacts adds floor(50 * 4 / 10) = 20 on the run's 50, not 60 on 150
    {
      auto name = (dir / "0000000042.m2.records.json").string();
      phosg::save_file(name, "{\"SerialNumber\": 42, \"BestScore\": 0, \"BestTime\": 0, \"Runs\": 3, \"Won\": 1, "
          "\"Splinters\": 100, \"SplintersTotal\": 100}");
      for (uint8_t k = 1; k <= 10; k++) {
        spire_records_credit_room_file(name, 42, payout, 0x52525252, k, (k == 4) || (k == 8), false);
      }
      auto rec = spire_records_load(name);
      check("M2 the run pays 50 on top of 100", rec.splinters == 150);
      auto fin = spire_records_finish_splinters(rec, &payout, 0x52525252, 4, no_rooms, no_rooms, 10, 2);
      check("M2 'F' with 4 pacts adds 20, not 60", (fin.pact_bonus == 20) && (rec.splinters == 170));
    }

    // L4: a repeated 'F' of one run counts Won (and the difficulty's Won) once; Runs is counted at 'B', never at 'F'
    {
      auto name = (dir / "0000000042.l4.records.json").string();
      SpireRecords rec;
      rec.runs = 1;
      auto s1 = spire_records_finish_stats(rec, 0x54545454, 1, 500, 900, true);
      check("L4 the first won 'F' counts", s1.new_score && s1.new_time && (rec.won == 1) &&
          (rec.per_difficulty[1] == std::make_pair<uint32_t, uint32_t>(1, 900)) && (rec.best_time == 900));
      spire_records_save(name, 42, rec);
      rec = spire_records_load(name);
      auto s2 = spire_records_finish_stats(rec, 0x54545454, 1, 500, 900, true);
      check("L4 the same 'F' again (after a reload) counts nothing", !s2.new_score && !s2.new_time && (rec.won == 1) &&
          (rec.per_difficulty[1].first == 1) && (rec.runs == 1) && (rec.best_time == 900) && (rec.best_score == 500));
      spire_records_finish_stats(rec, 0x55555555, 1, 400, 800, true);
      check("L4 another run's won 'F' counts", (rec.won == 2) && (rec.per_difficulty[1].first == 2) && (rec.best_time == 800));
      // partner left: a not-won 'F', then the resumed run is won: Won once
      spire_records_finish_stats(rec, 0x56565656, 1, 100, 300, false);
      check("L4 a not-won 'F' counts no win", rec.won == 2);
      spire_records_finish_stats(rec, 0x56565656, 1, 600, 1000, true);
      spire_records_finish_stats(rec, 0x56565656, 1, 600, 1000, true);
      check("L4 the resumed run's win counts once", (rec.won == 3) && (rec.per_difficulty[1].first == 3));
      // and the pact bonus of a run whose won 'F' came first is still paid (the legacy V too)
      SpireRecords legacy;
      spire_records_finish_splinters(legacy, nullptr, 0x57575757, 4, {1, 2}, {}, 2, 0);
      spire_records_finish_stats(legacy, 0x57575757, 0, 10, 100, true);
      check("L4 the stats after the legacy V keep its splinters", legacy.splinters == 2);
    }

    std::filesystem::remove_all(dir);
    for (const auto& what : failures) {
      phosg::log_error_f("   F2 fix check failed: {}", what);
    }
    expect(failures.empty());
  }

  phosg::log_info_f("-- Spire records route B fix round 2: a repeated 'F' of one run pays and unlocks nothing twice");
  {
    // Every check is logged before any is asserted, so one run shows each that fails.
    std::vector<std::string> failures;
    auto check = [&](const std::string& what, bool ok) {
      phosg::log_info_f("   R2 check {}: {}", what, ok ? "ok" : "FAILS");
      if (!ok) {
        failures.emplace_back(what);
      }
    };
    auto dir = std::filesystem::temp_directory_path() / std::format("newserv-static-spire-r2-{}", getpid());
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    // Item 3: no Payout block (the legacy V). A not-won 'F' of run X, then the same 'F' again (after a reload)
    {
      auto name = (dir / "0000000042.legacy.records.json").string();
      SpireRecords rec;
      auto fin1 = spire_records_finish_splinters(rec, nullptr, 0x61616161, 0, {1, 2, 3}, {}, 3, 0);
      spire_records_save(name, 42, rec);
      check("legacy: the first not-won 'F' pays the old V (3 rooms)", fin1.legacy && (fin1.added == 3) && (rec.splinters == 3));
      spire_records_finish_stats(rec, 0x61616161, 0, 10, 100, false);
      rec = spire_records_load(name);
      auto fin2 = spire_records_finish_splinters(rec, nullptr, 0x61616161, 0, {1, 2, 3}, {}, 3, 0);
      check("legacy: the same not-won 'F' again pays nothing", (fin2.added == 0) && (rec.splinters == 3));
      // a Payout block that shows up before a third 'F' of the run pays no room of it twice and no pact bonus
      const SpireShopPayout payout{.per_room = 3, .per_boss = 10, .pact_tenths = 1};
      auto fin3 = spire_records_finish_splinters(rec, &payout, 0x61616161, 2, {1, 2, 3}, {}, 3, 0);
      check("legacy: a later 'F' with a Payout block pays no room twice", (fin3.added == 0) && (rec.splinters == 3));
      // another run still pays its own V; run id 0 (no 'B') pays as before
      auto other = spire_records_finish_splinters(rec, nullptr, 0x62626262, 0, {1, 2}, {}, 2, 0);
      check("legacy: another run's 'F' pays its V", other.legacy && (other.added == 2));
      auto none = spire_records_finish_splinters(rec, nullptr, 0, 0, {1}, {}, 1, 0);
      check("legacy: an 'F' without a run id pays the V as before", none.legacy && (none.added == 1));
    }

    // Item 2: a won 'F' opens the payout flags once per run; q925 clears them; the same won 'F' again opens nothing
    {
      SpireRecords rec;
      SpireMetaFlags flags;
      SpireMetaFinish won;
      won.won = true;
      won.exams = 2;
      won.deathless = true;
      auto fl = spire_meta_flags_for_finish(won, 0, true);
      fl.emplace_back(spire_meta_ladder_flag(1));
      auto s1 = spire_records_finish_stats(rec, 0x63636363, 0, 500, 900, true);
      auto a1 = spire_meta_apply_finish(flags, fl, 0, 1, s1.won_counted);
      bool all_pay = flags[std::make_pair(0, SPIRE_META_FLAG_PAY_WIN)] &&
          flags[std::make_pair(0, SPIRE_META_FLAG_PAY_DEATHLESS)] && flags[std::make_pair(0, SPIRE_META_FLAG_PAY_RECORD)];
      check("meta: the first won 'F' opens the payout and names its unlocks", a1.payout && all_pay && !a1.unlocked.empty());
      for (uint16_t f = SPIRE_META_FLAG_PAY_WIN; f <= SPIRE_META_FLAG_PAY_RECORD; f++) {
        flags[std::make_pair(0, f)] = false; // q925 paid and cleared them
      }
      auto s2 = spire_records_finish_stats(rec, 0x63636363, 0, 500, 900, true);
      auto a2 = spire_meta_apply_finish(flags, fl, 0, 1, s2.won_counted);
      bool none_pay = !flags[std::make_pair(0, SPIRE_META_FLAG_PAY_WIN)] &&
          !flags[std::make_pair(0, SPIRE_META_FLAG_PAY_DEATHLESS)] && !flags[std::make_pair(0, SPIRE_META_FLAG_PAY_RECORD)];
      check("meta: the same won 'F' again reopens no payout flag", !a2.payout && none_pay);
      check("meta: the same won 'F' again names no unlock", a2.unlocked.empty());
      // partner left: a not-won 'F' of run Y opens nothing, its later won 'F' opens the payout once
      auto s3 = spire_records_finish_stats(rec, 0x64646464, 0, 100, 300, false);
      SpireMetaFinish lost;
      lost.exams = 1;
      auto a3 = spire_meta_apply_finish(flags, spire_meta_flags_for_finish(lost, 0, false), 0, 1, s3.won_counted);
      auto s4 = spire_records_finish_stats(rec, 0x64646464, 0, 600, 1000, true);
      auto a4 = spire_meta_apply_finish(flags, fl, 0, 1, s4.won_counted);
      check("meta: a partner-left run's later win opens the payout", !a3.payout && a4.payout &&
          flags[std::make_pair(0, SPIRE_META_FLAG_PAY_WIN)]);
      // a won 'F' of another run opens it again (each win pays once)
      flags[std::make_pair(0, SPIRE_META_FLAG_PAY_WIN)] = false;
      auto s5 = spire_records_finish_stats(rec, 0x65656565, 0, 600, 1000, true);
      auto a5 = spire_meta_apply_finish(flags, fl, 0, 1, s5.won_counted);
      check("meta: another run's win opens the payout", a5.payout && flags[std::make_pair(0, SPIRE_META_FLAG_PAY_WIN)]);
    }

    std::filesystem::remove_all(dir);
    for (const auto& what : failures) {
      phosg::log_error_f("   R2 check failed: {}", what);
    }
    expect(failures.empty());
  }

  phosg::log_info_f("-- Spire shop route B F2: the Payout block (PerRoom 3, PerBoss 10, PactTenths 1), parsed strictly");
  {
    std::string shipped = phosg::load_file(SPIRE_SHOP_CATALOGUE_FILE);
    auto repo = spire_shop_parse(shipped);
    expect(repo.payout.has_value());
    expect_eq(3u, repo.payout->per_room);
    expect_eq(10u, repo.payout->per_boss);
    expect_eq(1u, repo.payout->pact_tenths);
    auto with_payout = [&](const phosg::JSON* block) {
      auto j = phosg::JSON::parse(shipped);
      j.erase("Payout");
      if (block) {
        j.emplace("Payout", phosg::JSON(*block));
      }
      return j.serialize();
    };
    expect(!spire_shop_parse(with_payout(nullptr)).payout.has_value()); // no block: the fallback at 'F'
    auto refused = [&](const phosg::JSON& block) {
      try {
        spire_shop_parse(with_payout(&block));
        return false;
      } catch (const std::exception&) {
        return true;
      }
    };
    using J = phosg::JSON;
    expect(refused(J::list({3, 10, 1})));
    expect(refused(J::dict({{"PerRoom", 3}, {"PerBoss", 10}}))); // PactTenths missing
    expect(refused(J::dict({{"PerRoom", 3}, {"PerBoss", 10}, {"PactTenths", 1}, {"PerKill", 1}}))); // unknown key
    expect(refused(J::dict({{"PerRoom", -1}, {"PerBoss", 10}, {"PactTenths", 1}})));
    expect(refused(J::dict({{"PerRoom", 2.5}, {"PerBoss", 10}, {"PactTenths", 1}})));
    expect(refused(J::dict({{"PerRoom", "3"}, {"PerBoss", 10}, {"PactTenths", 1}})));
    expect(refused(J::dict({{"PerRoom", 3}, {"PerBoss", 1001}, {"PactTenths", 1}})));
    expect(refused(J::dict({{"PerRoom", 3}, {"PerBoss", 10}, {"PactTenths", 11}})));
    J good = J::dict({{"_what", "a note"}, {"PerRoom", 4}, {"PerBoss", 0}, {"PactTenths", 10}});
    auto ok = spire_shop_parse(with_payout(&good));
    expect(ok.payout.has_value() && (ok.payout->per_room == 4) && (ok.payout->per_boss == 0) && (ok.payout->pact_tenths == 10));
  }

  phosg::log_info_f("-- Spire shop: debit, balance, cap, sale, rung threshold and the heat table (v50, B-BASIS-2/-3/-13)");
  {
    using R = SpireShopRefusal;
    // a small catalogue: start_level not on sale, kit_tier cap 2 with 1 on sale, Hard at P 3
    auto cat = spire_shop_parse(R"({"Items": {
        "start_level": {"Max": 5, "OnSale": 0, "Price": [15, 30, 45, 60, 75], "P": [0, 1, 2, 3, 4, 5]},
        "kit_tier": {"Max": 2, "OnSale": 1, "Price": [30, 60], "P": [0, 2, 4]},
        "hp_mat": {"Max": 2, "Price": [8, 16], "P": [0, 0, 1]},
        "atp_grind": {"Max": 3, "Price": [10, 20, 30], "P": [0, 1, 2, 3]},
        "open_hard": {"Max": 1, "Price": [60], "PNeeded": 3},
        "open_vh": {"Max": 1, "Price": [120], "PNeeded": 5},
        "open_ult": {"Max": 1, "Price": [240], "PNeeded": 9}},
      "MetaHeat": {"HAuto": {"HUmar": [0, 0, 1, 2, null, 4], "RAmar": [0, null, null]}}})");
    expect_eq(SPIRE_SHOP_NUM_ITEMS, cat.items.size());
    expect_eq(0, cat.items[0].on_sale);
    expect_eq(2, cat.items[2].on_sale); // OnSale missing: the cap
    expect_eq(1, cat.items[4].rung);
    expect_eq(3, cat.items[6].rung);
    uint8_t closed[2] = {0, 0};
    std::map<std::string, uint32_t> up;
    // debit: atp_grind level 1 costs 10, the level goes up, the price of the next level follows
    expect(R::NONE == spire_shop_buy_refusal(cat, 3, up, 10, closed));
    SpireMetaFile file;
    expect_eq(10u, spire_shop_apply_buy(cat, 3, file));
    expect_eq(1u, spire_shop_level(file.upgrades, 3));
    expect_eq(20u, spire_shop_next_price(cat, 3, file.upgrades, closed));
    expect(file.base_rung.empty()); // a strength item opens no rung
    // insufficient balance: refused, and the price is still shown
    expect(R::BALANCE == spire_shop_buy_refusal(cat, 3, file.upgrades, 19, closed));
    expect(R::NONE == spire_shop_buy_refusal(cat, 3, file.upgrades, 20, closed));
    expect_eq(20u, spire_shop_next_price(cat, 3, file.upgrades, closed));
    // cap: atp_grind at 3 of 3
    up = {{"atp_grind", 3}};
    expect(R::MAX_LEVEL == spire_shop_buy_refusal(cat, 3, up, 9999, closed));
    expect_eq(0u, spire_shop_next_price(cat, 3, up, closed));
    // not on sale: start_level (OnSale 0) and kit_tier level 2 (OnSale 1); a balance never buys them
    expect(R::NOT_ON_SALE == spire_shop_buy_refusal(cat, 0, {}, 9999, closed));
    expect(R::NONE == spire_shop_buy_refusal(cat, 1, {}, 30, closed));
    expect(R::NOT_ON_SALE == spire_shop_buy_refusal(cat, 1, {{"kit_tier", 1}}, 9999, closed));
    expect(R::MAX_LEVEL == spire_shop_buy_refusal(cat, 1, {{"kit_tier", 2}}, 9999, closed));
    expect(R::UNKNOWN_ITEM == spire_shop_buy_refusal(cat, 7, {}, 9999, closed));
    // strength P: the catalogue's P at each owned level, rung items count 0
    expect_eq(0u, spire_shop_strength(cat, {}));
    expect_eq(2u + 1u + 3u, spire_shop_strength(cat, {{"kit_tier", 1}, {"hp_mat", 2}, {"atp_grind", 3}, {"open_hard", 1}}));
    expect_eq(5u, spire_shop_strength(cat, {{"start_level", 99}})); // a level past the table takes its last P
    // rung threshold: Hard needs P 3; at P 2 refused, at P 3 bought, and BaseRung of both runs becomes 1
    std::map<std::string, uint32_t> p2 = {{"atp_grind", 2}}, p3 = {{"atp_grind", 3}};
    expect(R::STRENGTH == spire_shop_buy_refusal(cat, 4, p2, 9999, closed));
    expect_eq(0u, spire_shop_next_price(cat, 4, p2, closed));
    expect(R::NONE == spire_shop_buy_refusal(cat, 4, p3, 60, closed));
    expect(R::BALANCE == spire_shop_buy_refusal(cat, 4, p3, 59, closed));
    SpireMetaFile rung_file;
    rung_file.upgrades = p3;
    expect_eq(60u, spire_shop_apply_buy(cat, 4, rung_file));
    expect_eq(1, spire_meta_base_rung(rung_file, 1));
    expect_eq(1, spire_meta_base_rung(rung_file, 2));
    expect_eq(1u, spire_shop_level(rung_file.upgrades, 4));
    expect_eq(3u, spire_shop_strength(cat, rung_file.upgrades));
    // Very Hard needs the rung below open for one run (won or BaseRung) and P 5
    std::map<std::string, uint32_t> p5 = {{"kit_tier", 1}, {"atp_grind", 3}};
    expect(R::RUNG_CLOSED == spire_shop_buy_refusal(cat, 5, p5, 9999, closed));
    uint8_t hard_run1[2] = {1, 0};
    expect(R::NONE == spire_shop_buy_refusal(cat, 5, p5, 120, hard_run1));
    expect(R::STRENGTH == spire_shop_buy_refusal(cat, 5, p3, 9999, hard_run1)); // the threshold before the rung
    // already open for both runs (ladder wins): no splinters taken for nothing; open for one run only: still sold
    uint8_t hard_both[2] = {1, 1};
    expect(R::ALREADY_OPEN == spire_shop_buy_refusal(cat, 4, p3, 9999, hard_both));
    expect(R::NONE == spire_shop_buy_refusal(cat, 4, p3, 9999, hard_run1));
    // BaseRung never goes down: a file with BaseRung 2 keeps 2 when open_hard is bought
    SpireMetaFile high;
    high.base_rung = {{1, 2}, {2, 0}};
    spire_shop_apply_buy(cat, 4, high);
    expect_eq(2, spire_meta_base_rung(high, 1));
    expect_eq(1, spire_meta_base_rung(high, 2));
    // the heat: H_auto(P, class); null takes the last heat at a lower P, P past the table its last column, an unknown
    // class or a class without a heat 0
    expect_eq(0u, spire_shop_heat(cat, 0, 0)); // HUmar
    expect_eq(2u, spire_shop_heat(cat, 0, 3));
    expect_eq(2u, spire_shop_heat(cat, 0, 4)); // null at P 4
    expect_eq(4u, spire_shop_heat(cat, 0, 5));
    expect_eq(4u, spire_shop_heat(cat, 0, 50));
    expect_eq(0u, spire_shop_heat(cat, 3, 2)); // RAmar: nulls after P 0
    expect_eq(0u, spire_shop_heat(cat, 2, 5)); // HUcast is not in this table
    // v54: one table per ladder rung, picked by the quest's rung; a rung without a table plays H 0, never Normal's
    expect_eq(0, spire_meta_ladder_quest_rung(921));
    expect_eq(0, spire_meta_ladder_quest_rung(922));
    expect_eq(0, spire_meta_ladder_quest_rung(980));
    expect_eq(1, spire_meta_ladder_quest_rung(981));
    expect_eq(2, spire_meta_ladder_quest_rung(982));
    expect_eq(3, spire_meta_ladder_quest_rung(983));
    expect_eq(1, spire_meta_ladder_quest_rung(984));
    expect_eq(2, spire_meta_ladder_quest_rung(985));
    expect_eq(3, spire_meta_ladder_quest_rung(986));
    expect_eq(0, spire_meta_ladder_quest_rung(987));
    auto rung_cat = spire_shop_parse(R"({"Items": {
        "start_level": {"Max": 1, "Price": [15], "P": [0, 1]},
        "kit_tier": {"Max": 1, "Price": [30], "P": [0, 2]},
        "hp_mat": {"Max": 1, "Price": [8], "P": [0, 0]},
        "atp_grind": {"Max": 1, "Price": [10], "P": [0, 1]},
        "open_hard": {"Max": 1, "Price": [60], "PNeeded": 8},
        "open_vh": {"Max": 1, "Price": [120], "PNeeded": 29},
        "open_ult": {"Max": 1, "Price": [240], "PNeeded": 38}},
      "MetaHeat": {"HAuto": {"HUmar": [0, 0, 1, 5]}, "HAutoHard": {"HUmar": [0, 0, 0, 3, null]},
                   "HAutoUltimate": {"RAmar": [0, 1]}}})");
    expect_eq(29u, rung_cat.items[5].p_needed);
    expect_eq(38u, rung_cat.items[6].p_needed);
    expect_eq(5u, spire_shop_heat(rung_cat, 0, 3)); // Normal: HUmar P 3
    expect_eq(5u, spire_shop_heat(rung_cat, 0, 3, 0));
    expect_eq(3u, spire_shop_heat(rung_cat, 0, 3, 1)); // Hard: its own table
    expect_eq(3u, spire_shop_heat(rung_cat, 0, 4, 1)); // null: the last heat at a lower P
    expect_eq(0u, spire_shop_heat(rung_cat, 0, 2, 1));
    expect_eq(0u, spire_shop_heat(rung_cat, 0, 3, 2)); // Very Hard: no table, H 0
    expect_eq(0u, spire_shop_heat(rung_cat, 0, 3, 3)); // Ultimate: HUmar not in its table
    expect_eq(1u, spire_shop_heat(rung_cat, 3, 9, 3)); // Ultimate RAmar, P past the table
    expect_eq(0u, spire_shop_heat(rung_cat, 0, 3, 4)); // no rung 4
    expect_eq(5u, spire_shop_heat(rung_cat, 0, 3, spire_meta_ladder_quest_rung(921)));
    expect_eq(3u, spire_shop_heat(rung_cat, 0, 3, spire_meta_ladder_quest_rung(981)));
    // the answer: 'M' tag, sub-request in bits 16-23, refusal in bits 8-15, OK only without a refusal
    expect_eq(0x4D030001u, spire_shop_answer(SPIRE_SHOP_START, R::NONE));
    expect_eq(0x4D020900u, spire_shop_answer(SPIRE_SHOP_BUY, R::BALANCE));
    expect_eq(0x4D010200u, spire_shop_answer(SPIRE_SHOP_LIST, R::NO_CATALOGUE));
    for (uint8_t z = 1; z <= static_cast<uint8_t>(R::FILE_ERROR); z++) {
      expect_eq(0u, spire_shop_answer(SPIRE_SHOP_BUY, static_cast<R>(z)) & 0xFF);
      expect(std::string(spire_shop_refusal_name(static_cast<R>(z))) != "unknown");
    }
    // the answer registers are outside every register the bridge stores or the quest's server channels use
    for (uint8_t reg = SPIRE_SHOP_LEVEL_FIRST_REGISTER; reg < SPIRE_SHOP_PRICE_FIRST_REGISTER + SPIRE_SHOP_NUM_ITEMS; reg++) {
      expect(reg != SPIRE_SHOP_ANSWER_REGISTER);
      expect(reg < SPIRE_RESUME_ITEM_FIRST_REGISTER);
    }
    for (uint8_t reg : {SPIRE_SHOP_BALANCE_REGISTER, SPIRE_SHOP_STRENGTH_REGISTER, SPIRE_SHOP_HEAT_REGISTER}) {
      expect((reg < SPIRE_BRIDGE_FIRST_REGISTER) && (reg != SPIRE_BRIDGE_DRAW_RESULT_REGISTER));
    }
    // the code letter is free: no other r248 code or 4CC starts with 'M'
    for (uint8_t letter : {SPIRE_BRIDGE_MSG_WAVE, SPIRE_BRIDGE_MSG_LIFE, SPIRE_BRIDGE_MSG_TIME, SPIRE_BRIDGE_MSG_PARTNER,
             SPIRE_BRIDGE_MSG_RUN_OVER, SPIRE_BRIDGE_MSG_PAUSE_B, SPIRE_BRIDGE_MSG_DOWN, SPIRE_BRIDGE_MSG_HINT,
             SPIRE_BRIDGE_MSG_APPROACH, SPIRE_BRIDGE_MSG_PROMPT, SPIRE_BRIDGE_REC_BEGIN, SPIRE_BRIDGE_REC_SCORE,
             SPIRE_BRIDGE_REC_FINISH, SPIRE_BRIDGE_REC_BOARD, SPIRE_BRIDGE_TIMER_START, SPIRE_BRIDGE_TIMER_END,
             SPIRE_BRIDGE_DRAW_REQUEST, SPIRE_BRIDGE_SAVESTATE_WRITE, SPIRE_BRIDGE_RESUME, SPIRE_BRIDGE_BASE_RETURN,
             SPIRE_BRIDGE_RUN_AGAIN}) {
      expect(letter != SPIRE_BRIDGE_SHOP);
    }
    for (uint32_t fourcc : {SPIRE_BRIDGE_REQUEST_VALUE, SPIRE_BRIDGE_DONE_VALUE, SPIRE_BRIDGE_BATTLE_END_VALUE,
             SPIRE_BRIDGE_ROOM_CLEAR_VALUE}) {
      expect((fourcc >> 24) != SPIRE_BRIDGE_SHOP);
    }
    // malformed catalogues are refused (the shop then answers NO_CATALOGUE)
    auto throws = [](const std::string& text) {
      try {
        spire_shop_parse(text);
      } catch (const std::exception&) {
        return true;
      }
      return false;
    };
    expect(throws("{\"Items\": {}, \"MetaHeat\": {\"HAuto\": {}}}"));
    expect(throws("{\"Items\": {\"start_level\": {\"Max\": 1, \"OnSale\": 2, \"Price\": [1, 2], \"P\": [0, 1]}}}"));
    // meta.json: Upgrades and BaseRung survive a save and a load (every flag write loads and saves the whole file)
    auto dir = std::filesystem::temp_directory_path() / std::format("newserv-static-spire-shop-{}", getpid());
    std::filesystem::create_directories(dir);
    auto name = (dir / "shop.meta.json").string();
    SpireMetaFile saved;
    saved.flags[std::make_pair(uint8_t(0), SPIRE_META_FLAG_FIRST_RUN)] = true;
    saved.upgrades = {{"hp_mat", 2}, {"open_hard", 1}};
    saved.base_rung = {{1, 1}, {2, 1}};
    spire_meta_save_file(name, 42, saved);
    auto loaded = spire_meta_load_file(name);
    expect_eq(2u, spire_shop_level(loaded.upgrades, 2));
    expect_eq(1u, spire_shop_level(loaded.upgrades, 4));
    expect_eq(1, spire_meta_base_rung(loaded, 2));
    expect(loaded.flags.at(std::make_pair(uint8_t(0), SPIRE_META_FLAG_FIRST_RUN)));
    phosg::save_file(name, "{\"SerialNumber\":42,\"Flags\":[]}");
    expect(spire_meta_load_file(name).upgrades.empty()); // an older file has none
    std::filesystem::remove_all(dir);
    // the catalogue in the repository parses, sells Hard within reach and has a heat row for every class on every
    // ladder rung (v54)
    auto repo = spire_shop_parse(phosg::load_file(SPIRE_SHOP_CATALOGUE_FILE));
    std::map<std::string, uint32_t> all_on_sale;
    for (size_t z = 0; z < 4; z++) {
      all_on_sale[SPIRE_SHOP_ITEM_NAMES[z]] = repo.items[z].on_sale;
    }
    expect(spire_shop_strength(repo, all_on_sale) >= repo.items[4].p_needed);
    for (const auto& table : repo.heat) {
      for (uint8_t cls = 0; cls < 12; cls++) {
        expect(table.count(name_for_char_class(cls)));
      }
    }
  }

  phosg::log_info_f("-- Spire start level: overlay level = quest level + owned start_level, rule block raised once (v52)");
  {
    // the bonus (route B F3): Levels[min(owned, Max)] of the catalogue (the shipped table without a catalogue)
    auto repo = spire_shop_parse(phosg::load_file(SPIRE_SHOP_CATALOGUE_FILE));
    expect_eq(0u, spire_start_level_bonus(&repo, {}));
    expect_eq(0u, spire_start_level_bonus(&repo, {{"hp_mat", 3}}));
    const uint32_t shipped_bonus[6] = {0, 5, 10, 14, 14, 14}; // owned 0-5
    for (uint32_t owned = 0; owned <= 5; owned++) {
      expect_eq(shipped_bonus[owned], spire_start_level_bonus(&repo, {{"start_level", owned}}));
      expect_eq(shipped_bonus[owned], spire_start_level_bonus(nullptr, {{"start_level", owned}}));
    }
    expect_eq(14u, spire_start_level_bonus(&repo, {{"start_level", 99}}));
    expect_eq(14u, spire_start_level_bonus(nullptr, {{"start_level", 99}}));
    // the catalogue sells start_level now (B-BASIS-32 closed by v52)
    expect(repo.items[0].on_sale >= 1);
    uint8_t none_open[2] = {0, 0};
    expect(SpireShopRefusal::NONE == spire_shop_buy_refusal(repo, 0, {}, 9999, none_open));
    // the level: quest level + bonus, at most the overlay's clamp
    expect_eq(3u, spire_start_level_char_level(0, 3)); // q921: Lv1 + 3 = Lv4
    expect_eq(32u, spire_start_level_char_level(27, 5)); // a room quest at Lv28 + 5
    expect_eq(SPIRE_START_LEVEL_LEVEL_CAP, spire_start_level_char_level(198, 5));
    expect_eq(SPIRE_START_LEVEL_LEVEL_CAP, spire_start_level_char_level(0xFFFFFFFF, 5)); // never wraps
    // the overlay at that level (create_battle_overlay's replace_char path: reset_to_base + advance_to_level on the
    // class's level table): level = base + tier, EXP and stats the class's own at that level
    auto level_table = std::make_shared<JSONLevelTable>(
        phosg::JSON::parse(phosg::load_file("system/tables/level-table-v3.json")));
    for (uint8_t cls : {uint8_t(0), uint8_t(4), uint8_t(8), uint8_t(11)}) {
      for (uint32_t tier = 0; tier <= 5; tier++) {
        BattleRules rules;
        rules.replace_char = true;
        rules.char_level = 0;
        BattleRules raised = rules;
        raised.char_level = spire_start_level_char_level(rules.char_level, tier);
        PlayerStats stats;
        level_table->reset_to_base(stats, cls);
        level_table->advance_to_level(stats, std::clamp<uint32_t>(raised.char_level, 0, 199), cls);
        expect_eq(rules.char_level + tier, static_cast<uint32_t>(stats.level));
        uint32_t hp = level_table->base_stats_for_class(cls).hp;
        for (uint32_t lv = 1; lv <= tier; lv++) {
          hp += level_table->stats_delta_for_level(cls, lv).hp;
        }
        expect_eq(hp, static_cast<uint32_t>(stats.char_stats.hp));
        uint32_t exp = tier ? static_cast<uint32_t>(level_table->stats_delta_for_level(cls, tier).exp) : 0u;
        expect_eq(exp, static_cast<uint32_t>(stats.exp));
      }
    }
    // the rule block in the .bin: "arg_pushl level; ba_set_char_level" (49 <u32 LE> F8 71), raised only if it is there once
    std::string rule_block = std::string("\x49\x00\x00\x00\x00\xF8\x71", 7);
    std::string other = std::string("\x49\x00\x00\x00\x00\xF8\x70", 7); // ba_set_max_tech_level 0: untouched
    std::string bin = "head" + other + rule_block + "tail";
    expect_eq(1u, spire_start_level_patch_bin(bin, 0, 3));
    expect_eq("head" + other + std::string("\x49\x03\x00\x00\x00\xF8\x71", 7) + "tail", bin);
    std::string twice = rule_block + rule_block;
    std::string twice_before = twice;
    expect_eq(2u, spire_start_level_patch_bin(twice, 0, 3));
    expect_eq(twice_before, twice);
    std::string none = "head" + other;
    expect_eq(0u, spire_start_level_patch_bin(none, 0, 3));
    std::string room = std::string("\x49\x1B\x00\x00\x00\xF8\x71", 7); // a room quest at char_level 27
    expect_eq(0u, spire_start_level_patch_bin(room, 0, 3));
    expect_eq(1u, spire_start_level_patch_bin(room, 27, 32));
    expect_eq(std::string("\x49\x20\x00\x00\x00\xF8\x71", 7), room);
    // route B F3: the largest bonus still patches the rule block exactly once (q921 Lv1 + 14 = Lv15)
    std::string q921_rules = "head" + other + rule_block + "tail";
    expect_eq(1u, spire_start_level_patch_bin(q921_rules, 0, spire_start_level_char_level(0, 14)));
    expect_eq("head" + other + std::string("\x49\x0E\x00\x00\x00\xF8\x71", 7) + "tail", q921_rules);
  }

  phosg::log_info_f("-- Spire shop route B F3: +5 start levels per purchase, bought levels add no heat, kit_tier once");
  {
    using R = SpireShopRefusal;
    // the shipped catalogue parses, with start_level Levels [0, 5, 10, 14], Max 3, OnSale 3, Price [50, 60, 75], P 0
    auto repo = spire_shop_parse(phosg::load_file(SPIRE_SHOP_CATALOGUE_FILE));
    const auto& start_level = repo.items[0];
    expect_eq(3u, start_level.max);
    expect_eq(3u, start_level.on_sale);
    expect_eq((std::vector<uint32_t>{0, 5, 10, 14}), start_level.levels);
    expect_eq((std::vector<uint32_t>{50, 60, 75}), start_level.price);
    expect_eq((std::vector<uint32_t>{0, 0, 0, 0}), start_level.strength);
    expect_eq(5u, spire_start_level_bonus(&repo, {{"start_level", 1}}));
    expect_eq(10u, spire_start_level_bonus(&repo, {{"start_level", 2}}));
    expect_eq(14u, spire_start_level_bonus(&repo, {{"start_level", 3}}));
    expect_eq(14u, spire_start_level_bonus(&repo, {{"start_level", 5}}));
    // bought start levels count 0 toward P (invariant 7, B-BASIS-67); hp_mat, atp_grind and kit_tier keep their P
    for (uint32_t owned = 0; owned <= 5; owned++) {
      expect_eq(0u, spire_shop_strength(repo, {{"start_level", owned}}));
    }
    expect_eq((std::vector<uint32_t>{0, 2, 4}), repo.items[1].strength);
    expect_eq((std::vector<uint32_t>{0, 0, 1, 1, 2, 2}), repo.items[2].strength);
    expect_eq((std::vector<uint32_t>{0, 1, 2, 3, 4, 5}), repo.items[3].strength);
    // HAuto on Normal is at most 2 for every class at every P the catalogue reaches (0-16); the row layout stays
    for (uint8_t cls = 0; cls < 12; cls++) {
      expect_eq(9u, repo.heat[0].at(name_for_char_class(cls)).size());
      for (uint32_t p = 0; p <= 16; p++) {
        expect(spire_shop_heat(repo, cls, p) <= 2);
        expect(spire_shop_heat(repo, cls, p, 0) <= 2);
      }
    }
    // a catalogue without Levels gives one start level per owned level (an older catalogue keeps its own meaning)
    auto old_cat = spire_shop_parse(R"({"Items": {
        "start_level": {"Max": 5, "OnSale": 1, "Price": [15, 30, 45, 60, 75], "P": [0, 1, 2, 3, 4, 5]},
        "kit_tier": {"Max": 2, "OnSale": 1, "Price": [30, 60], "P": [0, 2, 4]},
        "hp_mat": {"Max": 2, "Price": [8, 16], "P": [0, 0, 1]},
        "atp_grind": {"Max": 3, "Price": [10, 20, 30], "P": [0, 1, 2, 3]},
        "open_hard": {"Max": 1, "Price": [60], "PNeeded": 3},
        "open_vh": {"Max": 1, "Price": [120], "PNeeded": 5},
        "open_ult": {"Max": 1, "Price": [240], "PNeeded": 9}},
      "MetaHeat": {"HAuto": {}}})");
    expect_eq(2u, spire_start_level_bonus(&old_cat, {{"start_level", 2}}));
    expect_eq(5u, spire_start_level_bonus(&old_cat, {{"start_level", 9}}));
    // a Levels list that does not cover levels 0-Max is refused
    bool short_levels_throws = false;
    try {
      spire_shop_parse(R"({"Items": {"start_level": {"Max": 3, "Price": [50, 60, 75], "P": [0, 0, 0, 0],
          "Levels": [0, 5, 10]}}})");
    } catch (const std::exception&) {
      short_levels_throws = true;
    }
    expect(short_levels_throws);
    // kit_tier: refused with 8 (already_open) while the class's second-kit flag is set, and no price on the list
    uint8_t none_open[2] = {0, 0};
    expect_eq(8, static_cast<int>(R::ALREADY_OPEN));
    expect(R::NONE == spire_shop_buy_refusal(repo, 1, {}, 9999, none_open, false));
    expect(R::ALREADY_OPEN == spire_shop_buy_refusal(repo, 1, {}, 9999, none_open, true));
    expect(R::ALREADY_OPEN == spire_shop_buy_refusal(repo, 1, {}, 0, none_open, true)); // before the balance
    expect_eq(0u, spire_shop_next_price(repo, 1, {}, none_open, true));
    expect_eq(repo.items[1].price[0], spire_shop_next_price(repo, 1, {}, none_open, false));
    // the flag concerns kit_tier only
    expect(R::NONE == spire_shop_buy_refusal(repo, 0, {}, 9999, none_open, true));
    expect(R::NONE == spire_shop_buy_refusal(repo, 2, {}, 9999, none_open, true));
    // the flag of the character's own archetype on Normal (0x388 HU, 0x389 RA, 0x38A FO)
    for (uint8_t cls = 0; cls < 12; cls++) {
      uint16_t own = SPIRE_META_FLAG_KIT2_FIRST +
          (char_class_is_hunter(cls) ? 0 : char_class_is_ranger(cls) ? 1 : 2);
      SpireMetaFile file;
      expect(!spire_meta_kit2_owned(file, cls));
      for (uint16_t flag = SPIRE_META_FLAG_KIT2_FIRST; flag < SPIRE_META_FLAG_KIT2_FIRST + 3; flag++) {
        file.flags.clear();
        file.flags[std::make_pair(uint8_t(0), flag)] = true;
        expect_eq(flag == own, spire_meta_kit2_owned(file, cls));
      }
      file.flags.clear();
      file.flags[std::make_pair(uint8_t(0), own)] = false; // a cleared entry is not set
      expect(!spire_meta_kit2_owned(file, cls));
      file.flags.clear();
      file.flags[std::make_pair(uint8_t(1), own)] = true; // earned on Hard only: a Normal run still lacks it
      expect(!spire_meta_kit2_owned(file, cls));
    }
  }

  phosg::log_info_f("-- Spire heat: the sidecar writes one step's bytes into the H 0 .bin, or nothing on any doubt (v53)");
  {
    auto throws = [](const std::function<void()>& fn) {
      try {
        fn();
      } catch (const std::exception&) {
        return true;
      }
      return false;
    };
    // a 16-byte .bin with a 4-byte field at 2 (a .data row) and a 2-byte field at 10 (a threshold), steps H 0..3
    std::string h0_bin = std::string("\x00\x01\xAA\xAA\xAA\xAA\x06\x07\x08\x09\x10\x11\x0C\x0D\x0E\x0F", 16);
    std::string side = R"({"Quest": 921, "BinSize": 16, "HeatMax": 3, "Fields": [
        {"Offset": 10, "Width": 2, "Values": ["1011", "2021", "3031", "4041"]},
        {"Offset": 2, "Width": 4, "Values": ["AAAAAAAA", "bbbbbbbb", "CCCCCCCC", "DDDDDDDD"]}]})";
    auto patch = spire_heat_patch_parse(side);
    expect_eq(921u, patch.quest_number);
    expect_eq(16u, patch.bin_size);
    expect_eq(3u, patch.heat_max);
    expect_eq(2u, patch.fields.size());
    expect_eq(2u, patch.fields[0].offset); // sorted by offset
    for (uint32_t h = 0; h <= 5; h++) {
      std::string bin = h0_bin;
      uint32_t step = std::min<uint32_t>(h, 3); // an H past HeatMax plays the top step
      expect_eq(step, spire_heat_patch_bin(bin, patch, h));
      std::string want = h0_bin;
      want.replace(2, 4, std::string(4, static_cast<char>(0xAA + 0x11 * step)));
      want.replace(10, 2, std::string{static_cast<char>(0x10 * (step + 1)), static_cast<char>(0x10 * (step + 1) + 1)});
      expect_eq(want, bin);
      expect_eq(h0_bin.size(), bin.size()); // the layout never moves
    }
    // the sidecar of another build: a .bin of another size, or without the H 0 bytes at a field, stays unchanged
    std::string longer = h0_bin + "x";
    std::string longer_before = longer;
    expect(throws([&]() { spire_heat_patch_bin(longer, patch, 2); }));
    expect_eq(longer_before, longer);
    std::string other = h0_bin;
    other[11] = 0x12;
    std::string other_before = other;
    expect(throws([&]() { spire_heat_patch_bin(other, patch, 2); }));
    expect_eq(other_before, other);
    // malformed sidecars are refused at parse
    auto bad = [&](const std::string& fields, size_t bin_size = 16) {
      return throws([&]() {
        spire_heat_patch_parse(std::format(R"({{"Quest": 921, "BinSize": {}, "HeatMax": 1, "Fields": [{}]}})", bin_size, fields));
      });
    };
    expect(!bad(R"({"Offset": 0, "Width": 1, "Values": ["00", "01"]})"));
    expect(bad(R"({"Offset": 0, "Width": 1, "Values": ["00"]})")); // fewer values than H 0..HeatMax
    expect(bad(R"({"Offset": 0, "Width": 1, "Values": ["00", "0102"]})")); // values of different widths
    expect(bad(R"({"Offset": 0, "Width": 2, "Values": ["00", "01"]})")); // Width is not the values' width
    expect(bad(R"({"Offset": 0, "Values": ["0", "1"]})")); // half a byte
    expect(bad(R"({"Offset": 0, "Values": ["", ""]})")); // width 0
    expect(bad(R"({"Offset": 0, "Values": ["zz", "01"]})")); // no hex
    expect(bad(R"({"Offset": 15, "Values": ["0000", "0101"]})")); // past the end of the .bin
    expect(bad(R"({"Offset": -1, "Values": ["00", "01"]})"));
    expect(bad(R"({"Offset": 0, "Values": ["0000", "0101"]}, {"Offset": 1, "Values": ["00", "01"]})")); // overlap
    expect(!bad(R"({"Offset": 0, "Values": ["0000", "0101"]}, {"Offset": 2, "Values": ["00", "01"]})")); // adjacent
    expect(throws([]() { spire_heat_patch_parse(R"({"Quest": 921, "BinSize": 16, "Fields": []})"); })); // no HeatMax
  }

  phosg::log_info_f("-- Spire heat: 'M' START answers every client with the step its .bin was patched at (v55)");
  {
    // the game's H: the leader's H_auto (H_keuze is chosen in the quest after the .bin is sent), clamped to HeatMax
    auto cat = spire_shop_parse(R"({"Items": {
        "start_level": {"Max": 5, "Price": [1, 1, 1, 1, 1], "P": [0, 1, 2, 3, 4, 5]},
        "kit_tier": {"Max": 1, "Price": [1], "P": [0, 2]},
        "hp_mat": {"Max": 1, "Price": [1], "P": [0, 0]},
        "atp_grind": {"Max": 1, "Price": [1], "P": [0, 1]},
        "open_hard": {"Max": 1, "Price": [1], "PNeeded": 8},
        "open_vh": {"Max": 1, "Price": [1], "PNeeded": 9},
        "open_ult": {"Max": 1, "Price": [1], "PNeeded": 10}},
      "MetaHeat": {"HAuto": {"HUmar": [0, 2, 4, 6, 9], "RAmar": [0, 1, 1, 2, 3]}}})");
    auto patch = spire_heat_patch_parse(R"({"Quest": 921, "BinSize": 2, "HeatMax": 7, "Fields": [
        {"Offset": 0, "Width": 1, "Values": ["00", "01", "02", "03", "04", "05", "06", "07"]}]})");
    uint32_t leader_h = spire_shop_heat(cat, 0, 4); // HUmar leader, P 4: H_auto 9, past HeatMax
    uint32_t follower_h = spire_shop_heat(cat, 3, 1); // RAmar follower, P 1: its own H_auto 1
    expect_eq(9u, leader_h);
    expect_eq(1u, follower_h);
    std::string bin("\x00\x00", 2);
    uint32_t step = spire_heat_patch_bin(bin, patch, leader_h);
    expect_eq(7u, step); // the rows of H 7 for every client of the game
    // every GC client of the game got the same step; START sends it, not the client's own H_auto
    SpireHeatSent sent{12, 921, step};
    expect_eq(7u, spire_heat_start_answer(&sent, 12, 921));
    expect(spire_heat_start_answer(&sent, 12, 921) != follower_h);
    // a record of another game or quest, or none (no heat patch, a non-GC client, an error): the quest's H 0 rows
    expect_eq(0u, spire_heat_start_answer(&sent, 13, 921));
    expect_eq(0u, spire_heat_start_answer(&sent, 12, 981));
    expect_eq(0u, spire_heat_start_answer(nullptr, 12, 921));
    SpireHeatSent h0{12, 921, 0};
    expect_eq(0u, spire_heat_start_answer(&h0, 12, 921));
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
      // v56: a 98 below the snapshot's level or EXP under AwaitingSave alone is a run copy: the snapshot stays
      {
        auto low = stored(false, false, true);
        auto before = low;
        auto low_copy = make(3, 0x00, 5, 100, 0, 0);
        expect(!spire_guard_follow_real_progress(low, low_copy.first, low_copy.second));
        expect(low.awaiting_save && spire_guard_same_state(low.inventory, low.disp, before.inventory, before.disp));
        auto less_exp = make(21, 0x00, 51, 370757, 1500, 125);
        expect(!spire_guard_follow_real_progress(low, less_exp.first, less_exp.second));
        expect(low.awaiting_save);
      }
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
    expect_eq(std::string("Run over. Rooms cleared: 0, 163 s."), spire_bridge_message_text(0x580000A3)); // v49: both outcomes
    expect(spire_bridge_message_text(0x53504952).empty()); // 'SPIR' is no message
    // v36: the quest's codes from spike/stadstart b29f310 (kind << 24 | a << 8 | b)
    expect_eq(std::string("Reward taken. Quest Board: Shop (optional) or Continue = leave room."), spire_bridge_message_text(0x47000000));
    expect_eq(std::string("Reward taken. Quest Board: Shop (optional) or Ready = leave room."), spire_bridge_message_text(0x47000001));
    expect_eq(std::string("You are down - you get up when the room is clear."), spire_bridge_message_text(0x55000001));
    expect_eq(std::string("You are back on your feet."), spire_bridge_message_text(0x55000000));
    // v62: 'H' has the bundle 8 text (section "Spire bundle 8")
    expect_eq(std::string("Room 2"), spire_bridge_message_text(0x41000002));
    // v37: 'C' point << 8 | room (quest 802ebe9); 3-5 retired
    expect_eq(std::string("Take your reward: Quest Board, slot Reward."), spire_bridge_message_text(0x43000102));
    // v62: 'C' 2 shows the overview line or the bundle 8 fallback (section "Spire bundle 8")
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
    // v62: 'A' rest names its effect (section "Spire bundle 8")
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

  phosg::log_info_f("-- Spire protected characters: character cheats refused everywhere (regie pso-regie-9)");
  {
    for (const char* cmd : {"$edit", "$item", "$matcount", "$loadchar", "$qset", "$qclear", "$qgwrite", "$bank",
             "$switchchar", "$patch", "$writemem", "$nativecall", "$kit", "$lv"}) {
      expect(DataIndex::spire_cheat_refused(cmd, true));    // the protected character: refused
      expect(!DataIndex::spire_cheat_refused(cmd, false));  // any other character: allowed
    }
    expect(!DataIndex::spire_cheat_refused("$i", true));    // the dispatcher passes the first name ($item), not the alias
    for (const char* cmd : {"$fastkill", "$infhp", "$inftp", "$secid", "$savechar", "$bbchar", "$spire", "$li"}) {
      expect(!DataIndex::spire_cheat_refused(cmd, true));   // does not change the character: allowed
    }
    // Per character, never by serial alone: serial 3117539659 is the Wii that carries Wakiru and the test character BidX
    DataIndex d;
    d.spire_blocked_quest_numbers.emplace(929);
    d.spire_blocked_characters.emplace_back(3117539659, "Wakiru");
    for (const char* cmd : {"$edit", "$item", "$matcount", "$kit", "$lv"}) {
      expect(DataIndex::spire_cheat_refused(cmd, d.is_spire_protected_character(3117539659, "Wakiru")));
      expect(DataIndex::spire_cheat_refused(cmd, d.is_spire_protected_character(3117539659, "\tEWakiru")));
      expect(!DataIndex::spire_cheat_refused(cmd, d.is_spire_protected_character(3117539659, "BidX")));
      expect(!DataIndex::spire_cheat_refused(cmd, d.is_spire_protected_character(3117539659, ""))); // no character data
      expect(!DataIndex::spire_cheat_refused(cmd, d.is_spire_protected_character(3117539666, "Wakiru")));
    }
    expect(!DataIndex::spire_cheat_refused("$fastkill", d.is_spire_protected_character(3117539659, "Wakiru")));
  }

  phosg::log_info_f("-- Spire $kit: the start set; only in a normal game, never for Wakiru (regie pso-regie-10)");
  {
    const auto& limits = ItemData::StackLimits::DEFAULT_STACK_LIMITS_V3_V4;
    auto pmt = ItemParameterTable::from_json(
        phosg::JSON::parse(phosg::load_file("system/tables/item-parameter-table-gc-v3.json")));
    JSONLevelTable kit_levels(phosg::JSON::parse(phosg::load_file("system/tables/level-table-v3.json")));
    // Per class (all 12 GC classes): every weapon, frame and shield has the class's bits in UsabilityFlags (units have
    // no usability field); the mag is level 200 and fits the class; the material plan keeps every cap and no stat
    // goes over the class's MaxStats at level 200; "$kit mat" drops exactly that plan in batches of at most 30
    for (uint8_t cls = 0; cls < 12; cls++) {
      bool android = char_class_is_android(cls);
      uint8_t bits = spire_weapon_usability_bits(cls);
      auto kit = DataIndex::spire_start_kit(cls);
      expect_eq(12, kit.size());
      size_t weapons = 0, frames = 0, shields = 0, units = 0;
      for (size_t z = 0; z + 1 < kit.size(); z++) {
        const auto& item = kit[z];
        if (item.data1[0] == 0x00) {
          weapons++;
          expect_eq(bits, pmt->get_weapon(item.data1[1], item.data1[2]).usability_flags & bits);
        } else {
          expect_eq(0x01, item.data1[0]);
          if (item.data1[1] == 0x03) {
            units++;
            pmt->get_unit(item.data1[2]);
          } else {
            (item.data1[1] == 0x01) ? frames++ : shields++;
            expect_eq(bits, pmt->get_armor_or_shield(item.data1[1], item.data1[2]).usability_flags & bits);
          }
        }
      }
      expect_eq(5, weapons);
      expect_eq(1, frames);
      expect_eq(1, shields);
      expect_eq(4, units);
      const auto& mag = kit.back();
      expect_eq(0x02, mag.data1[0]);
      expect_eq(200, mag.compute_mag_level());
      expect_eq(200, mag.data1[2]);
      expect_eq(0x78C80700, static_cast<uint32_t>(mag.data2db));
      expect_eq(0xA3, mag.data1[3]); // the code Marck was given: 021DC8A3 ... 78C80700
      expect_eq(3, mag.mag_photon_blast_for_slot(0)); // Pilla
      expect_eq(4, mag.mag_photon_blast_for_slot(1)); // Leilla
      expect_eq(2, mag.mag_photon_blast_for_slot(2)); // Golla
      if (android) {
        expect_eq(0, mag.data1w[5] / 100);
      }
      if (char_class_is_force(cls)) {
        expect(mag.data1w[5] > mag.data1w[3]); // MIND over POW
      } else {
        expect_eq(0, mag.data1w[5] / 100);
      }

      auto m = DataIndex::spire_kit_material_targets(cls, kit_levels);
      expect(m.shared() <= (android ? DataIndex::SPIRE_KIT_MATERIAL_CAP_ANDROID : DataIndex::SPIRE_KIT_MATERIAL_CAP));
      expect_eq(125, m.hp);
      expect_eq(android ? 0 : 125, m.tp);
      if (android) {
        expect_eq(0, m.mind);
      }
      if (char_class_is_force(cls)) {
        expect(m.mind > 0);
      }
      PSOBBCharacterFile ch;
      ch.disp.visual.sh.char_class = cls;
      spire_kit_lobby_apply(ch, std::make_shared<JSONLevelTable>(kit_levels), 200);
      const auto& max = kit_levels.max_stats_for_class(cls).char_stats;
      const auto& st = ch.disp.stats.char_stats;
      expect(st.atp <= max.atp);
      expect(st.mst <= max.mst);
      expect(st.evp <= max.evp);
      expect(st.dfp <= max.dfp);
      expect(st.lck <= max.lck);

      std::map<uint8_t, size_t> totals;
      size_t dropped = 0;
      for (;;) {
        auto batch = DataIndex::spire_kit_material_batch(limits, m, dropped);
        if (batch.empty()) {
          break;
        }
        expect(batch.size() <= 30);
        for (const auto& item : batch) {
          expect_eq(0x030B, (item.data1[0] << 8) | item.data1[1]);
          expect(item.stack_size(limits) <= item.max_stack_size(limits));
          totals[item.data1[2]] += item.stack_size(limits);
        }
        dropped += batch.size();
      }
      expect_eq(m.power, totals[0x00]);
      expect_eq(m.mind, totals[0x01]);
      expect_eq(m.evade, totals[0x02]);
      expect_eq(m.hp, totals[0x03]);
      expect_eq(m.tp, totals[0x04]);
      expect_eq(m.def, totals[0x05]);
      expect_eq(m.luck, totals[0x06]);
      expect_eq(DataIndex::spire_kit_materials(limits, m).size(), dropped);
    }
    for (uint8_t cls = 0; cls < 12; cls++) {
      auto m = DataIndex::spire_kit_material_targets(cls, kit_levels);
      phosg::log_info_f("   class {}: Pow {} Mind {} Evade {} Def {} Luck {} HP {} TP {}", cls, m.power, m.mind, m.evade,
          m.def, m.luck, m.hp, m.tp);
    }
    // HUcaseal: Luck to its cap 100, the rest of the android cap 150 in Power; the old hunter weapons
    auto caseal = DataIndex::spire_kit_material_targets(9, kit_levels);
    expect_eq(45, caseal.luck);
    expect_eq(105, caseal.power);
    expect_eq(150, caseal.shared());
    expect_eq(0x8A, DataIndex::spire_start_kit(9)[0].data1[1]);
    // $kit tech: forces get disks at their max level, capped at 30; androids none
    for (uint8_t cls = 0; cls < 12; cls++) {
      auto disks = DataIndex::spire_kit_tech_disks(cls, *pmt);
      if (char_class_is_android(cls)) {
        expect_eq(0, disks.size());
      }
      for (const auto& d : disks) {
        expect_eq(0x03, d.data1[0]);
        expect_eq(0x02, d.data1[1]);
        expect(d.data1[2] <= 29);
        expect_eq(pmt->get_max_tech_level(cls, d.data1[4]) >= 29 ? 29 : pmt->get_max_tech_level(cls, d.data1[4]), d.data1[2]);
      }
    }
    expect_eq(29, DataIndex::spire_kit_tech_disks(6, *pmt).at(0).data1[2]); // FOmarl Foie Lv30
    // $kit tools: full stacks (10 on GC), one Scape Doll
    auto tools = DataIndex::spire_kit_tools(limits);
    expect_eq(8, tools.size());
    expect_eq(10, tools.at(3).data1[5]); // Trifluid x10
    expect_eq(1, tools.at(7).data1[5]); // Scape Doll x1
    // $kit dolls: 10 separate Scape Dolls
    auto dolls = DataIndex::spire_kit_scape_dolls();
    expect_eq(10, dolls.size());
    for (const auto& doll : dolls) {
      expect_eq(0x03, doll.data1[0]);
      expect_eq(0x09, doll.data1[1]);
      expect_eq(1, doll.data1[5]);
    }
    expect(DataIndex::spire_kit_material_message(0, 0).find("niets") != std::string::npos);
    expect(DataIndex::spire_kit_material_message(30, 245).find("30 neergelegd") != std::string::npos);
    expect(DataIndex::spire_kit_material_message(30, 245).find("nog 245 over") != std::string::npos);
    expect(DataIndex::spire_kit_material_message(5, 0).find("niets meer") != std::string::npos);
    // A server with stackable materials (limit 99): fewer items, same totals; HP 125 is 99 + 26
    auto stacked_limits_vector = ItemData::StackLimits::DEFAULT_TOOL_LIMITS_V3_V4;
    stacked_limits_vector.at(0x0B) = 99;
    ItemData::StackLimits stacked_limits(Version::GC_V3, stacked_limits_vector, 999999);
    auto stacked = DataIndex::spire_kit_material_batch(stacked_limits, caseal, 0);
    expect_eq(5, stacked.size());
    expect_eq(26, stacked.back().stack_size(stacked_limits));
    expect(DataIndex::spire_kit_material_batch(stacked_limits, caseal, 5).empty());
    // Only in a game, never in a Spire run
    expect(DataIndex::spire_kit_refused(false, false) != nullptr); // lobby
    expect(DataIndex::spire_kit_refused(false, true) != nullptr);
    expect(DataIndex::spire_kit_refused(true, true) != nullptr);   // Spire or SpireMeta quest
    expect(DataIndex::spire_kit_refused(true, false) == nullptr);  // a normal game
    // Wakiru refused, BidX on the same Wii allowed
    DataIndex d;
    d.spire_blocked_characters.emplace_back(3117539659, "Wakiru");
    expect(DataIndex::spire_cheat_refused("$kit", d.is_spire_protected_character(3117539659, "Wakiru")));
    expect(!DataIndex::spire_cheat_refused("$kit", d.is_spire_protected_character(3117539659, "BidX")));
    expect(DataIndex::spire_cheat_refused("$lv", d.is_spire_protected_character(3117539659, "Wakiru")));
    expect(!DataIndex::spire_cheat_refused("$lv", d.is_spire_protected_character(3117539659, "BidX")));
  }

  phosg::log_info_f("-- $kitlobby: level table stats and EXP, material counters and caps, gates (regie 2026-10-25)");
  {
    using MatType = PSOBBCharacterFile::MaterialType;
    auto level_table = std::make_shared<JSONLevelTable>(
        phosg::JSON::parse(phosg::load_file("system/tables/level-table-v3.json")));
    // A Lv1 FOnewearl (class 8) as Nyra: level 200, EXP and stats summed from the table, plus 2 per material
    PSOBBCharacterFile ch;
    ch.disp.visual.sh.char_class = 8;
    level_table->reset_to_base(ch.disp.stats, 8);
    spire_kit_lobby_apply(ch, level_table, 200);
    auto t8 = DataIndex::spire_kit_material_targets(8, *level_table);
    const auto& base = level_table->base_stats_for_class(8);
    uint32_t atp = base.atp, dfp = base.dfp, mst = base.mst, hp = base.hp, evp = base.evp;
    for (uint32_t lv = 1; lv < 200; lv++) {
      const auto& delta = level_table->stats_delta_for_level(8, lv);
      atp += delta.atp;
      dfp += delta.dfp;
      mst += delta.mst;
      hp += delta.hp;
      evp += delta.evp;
    }
    expect_eq(199, ch.disp.stats.level);
    expect_eq(level_table->stats_delta_for_level(8, 199).exp, ch.disp.stats.exp);
    expect(ch.disp.stats.exp > 0);
    expect_eq(atp + t8.power * 2, ch.disp.stats.char_stats.atp);
    expect_eq(dfp + t8.def * 2, ch.disp.stats.char_stats.dfp);
    expect_eq(mst + t8.mind * 2, ch.disp.stats.char_stats.mst);
    expect_eq(evp + t8.evade * 2, ch.disp.stats.char_stats.evp);
    expect(t8.mind > 0);
    expect_eq(hp, ch.disp.stats.char_stats.hp); // HP from materials is in hp_from_materials, not here
    expect_eq(base.lck + t8.luck * 2, ch.disp.stats.char_stats.lck);
    expect_eq(t8.power, ch.get_material_usage(MatType::POWER));
    expect_eq(t8.def, ch.get_material_usage(MatType::DEF));
    expect_eq(t8.luck, ch.get_material_usage(MatType::LUCK));
    expect_eq(125, ch.get_material_usage(MatType::TP));
    expect_eq(125, ch.get_material_usage(MatType::HP));
    expect_eq(250, ch.inventory.hp_from_materials);
    expect_eq(t8.power, ch.inventory.items[8 + static_cast<uint8_t>(MatType::POWER)].extension_data2);
    expect_eq(t8.mind, ch.get_material_usage(MatType::MIND));
    expect_eq(t8.evade, ch.get_material_usage(MatType::EVADE));
    // Twice gives the same: counters are never added twice
    spire_kit_lobby_apply(ch, level_table, 200);
    expect_eq(t8.power, ch.get_material_usage(MatType::POWER));
    expect_eq(atp + t8.power * 2, ch.disp.stats.char_stats.atp);
    // Level 50 on request; out-of-range levels clamp
    PSOBBCharacterFile low;
    low.disp.visual.sh.char_class = 8;
    spire_kit_lobby_apply(low, level_table, 50);
    expect_eq(49, low.disp.stats.level);
    expect_eq(level_table->stats_delta_for_level(8, 49).exp, low.disp.stats.exp);
    spire_kit_lobby_apply(low, level_table, 999);
    expect_eq(199, low.disp.stats.level);
    // An android (HUcast, class 2) with 100 Evade already: the shared cap 150 leaves 50 (Power 50, Def 0, Luck 0)
    PSOBBCharacterFile cast;
    cast.disp.visual.sh.char_class = 2;
    cast.set_material_usage(MatType::EVADE, 100);
    spire_kit_lobby_apply(cast, level_table, 200);
    expect_eq(50, cast.get_material_usage(MatType::POWER));
    expect_eq(0, cast.get_material_usage(MatType::DEF));
    expect_eq(0, cast.get_material_usage(MatType::LUCK));
    expect_eq(100, cast.get_material_usage(MatType::EVADE));
    expect_eq(125, cast.get_material_usage(MatType::HP));
    expect_eq(0, cast.get_material_usage(MatType::TP));
    expect_eq(0, cast.get_material_usage(MatType::MIND));
    // A non-android with 200 Mind: the cap 250 leaves 50
    PSOBBCharacterFile mind;
    mind.disp.visual.sh.char_class = 8;
    mind.set_material_usage(MatType::MIND, 200);
    mind.set_material_usage(MatType::POWER, 80); // higher than the target: kept
    spire_kit_lobby_apply(mind, level_table, 200);
    expect_eq(std::max<uint8_t>(80, t8.power), mind.get_material_usage(MatType::POWER));
    expect_eq(0, mind.get_material_usage(MatType::DEF)); // 200 + 80 is over the cap already
    // Gates: Wakiru refused, BidX allowed, never in a Spire run or a game, never while a restore is pending
    DataIndex d;
    d.spire_blocked_characters.emplace_back(3117539659, "Wakiru");
    expect_eq(std::string("$C6Niet op dit personage."), std::string(spire_kit_lobby_refused(
        d.is_spire_protected_character(3117539659, "Wakiru"), false, false, false)));
    expect(spire_kit_lobby_refused(d.is_spire_protected_character(3117539659, "BidX"), false, false, false) == nullptr);
    expect(DataIndex::spire_cheat_refused("$kitlobby", d.is_spire_protected_character(3117539659, "Wakiru")));
    expect(!DataIndex::spire_cheat_refused("$kitlobby", d.is_spire_protected_character(3117539659, "BidX")));
    expect_eq(std::string("$C6Niet in een Spire-run."), std::string(spire_kit_lobby_refused(false, true, true, false)));
    expect_eq(std::string("$C6Alleen in de lobby."), std::string(spire_kit_lobby_refused(false, true, false, false)));
    expect_eq(std::string("$C6Wacht: herstel loopt"), std::string(spire_kit_lobby_refused(false, false, false, true)));
    // Lobby without cheat mode: allowed for BidX (cheat mode is never on in a lobby), Wakiru still refused
    expect(!SPIRE_KIT_LOBBY_NEEDS_CHEAT_MODE);
    expect(spire_kit_lobby_refused(d.is_spire_protected_character(3117539659, "BidX"), false, false, false) == nullptr);
    expect(spire_kit_lobby_refused(d.is_spire_protected_character(3117539659, "Wakiru"), false, false, false) != nullptr);
  }

  phosg::log_info_f("-- Spire guard: a marker only for a guarded quest, never for a protected character (regie pso-regie-9)");
  {
    using G = SpireGuardStart;
    // arguments: battle_game, has_quest, replace_char, guarded_quest, protected_character
    expect_eq(G::MARKER, spire_guard_start_action(true, true, true, true, false));    // Spire quest game: marker
    expect_eq(G::NONE, spire_guard_start_action(true, false, false, false, false));   // q0 Battle game (BidX 13:05): none
    expect_eq(G::NONE, spire_guard_start_action(true, false, false, false, true));    // q0 Battle game, Wakiru 13:07: none
    expect_eq(G::NONE, spire_guard_start_action(false, true, true, true, false));     // not a Battle game
    expect_eq(G::NONE, spire_guard_start_action(true, true, false, true, false));     // Battle quest without replace_char
    expect_eq(G::NONE, spire_guard_start_action(true, true, true, false, false));     // replace_char, not guarded
    expect_eq(G::REFUSE, spire_guard_start_action(true, true, true, true, true));     // protected: no marker, no overlay
    // arguments: protected_character, run_in_progress, restore_pending, awaiting_save
    expect(spire_guard_marker_open(false, true, false, false));
    expect(spire_guard_marker_open(false, false, true, false));
    expect(spire_guard_marker_open(false, false, false, true));
    expect(!spire_guard_marker_open(false, false, false, false));
    expect(!spire_guard_marker_open(true, true, false, false));   // Wakiru's old RunInProgress file: ignored
    expect(!spire_guard_marker_open(true, false, true, true));    // never a restore onto a protected character
    DataIndex d;
    d.spire_blocked_quest_numbers.emplace(929);
    d.spire_blocked_characters.emplace_back(3117539659, "Wakiru");
    expect(!spire_guard_marker_open(d.is_spire_protected_character(3117539659, "Wakiru"), true, false, false));
    expect(spire_guard_marker_open(d.is_spire_protected_character(3117539659, "BidX"), true, false, false));
  }

  phosg::log_info_f("-- Spire ladder: rung per character and run, win flags per difficulty, refuse with a message");
  {
    // q921/q922 on Normal, q981-q983 Run I Hard/VH/Ult, q984-q986 Run II
    expect_eq(1, spire_meta_ladder_run(921));
    expect_eq(1, spire_meta_ladder_run(983));
    expect_eq(2, spire_meta_ladder_run(922));
    expect_eq(2, spire_meta_ladder_run(984));
    expect_eq(2, spire_meta_ladder_run(986));
    expect_eq(0, spire_meta_ladder_run(940)); // a room quest: no ladder (plan §2, the room quests stay on Normal)
    expect_eq(0, spire_meta_ladder_run(925));
    expect_eq(0, spire_meta_ladder_run(987));

    // two characters on one card: each its own meta file, so its own rungs
    SpireMetaFlags bidx, other;
    expect_eq(0, spire_meta_ladder_rung(bidx, 1)); // nothing won: Normal only
    bidx[std::make_pair(uint8_t(0), SPIRE_META_FLAG_RUN1_WON)] = true; // Run I won on Normal
    expect_eq(1, spire_meta_ladder_rung(bidx, 1)); // Hard open
    expect_eq(0, spire_meta_ladder_rung(bidx, 2)); // Run II has its own ladder
    expect_eq(0, spire_meta_ladder_rung(other, 1));
    bidx[std::make_pair(uint8_t(1), SPIRE_META_FLAG_RUN1_WON)] = true;
    expect_eq(2, spire_meta_ladder_rung(bidx, 1)); // Very Hard
    bidx[std::make_pair(uint8_t(2), SPIRE_META_FLAG_RUN1_WON)] = true;
    bidx[std::make_pair(uint8_t(3), SPIRE_META_FLAG_RUN1_WON)] = true; // a win on Ultimate opens nothing past it
    expect_eq(3, spire_meta_ladder_rung(bidx, 1));
    other[std::make_pair(uint8_t(0), SPIRE_META_FLAG_RUN2_WON)] = false; // a cleared entry is not a win
    expect_eq(0, spire_meta_ladder_rung(other, 2));

    // AvailableIf of the run quests: q921 Normal only, q981 Hard with Run I open to Hard
    auto available = [&](const char* expr_str, Difficulty difficulty, uint8_t run1, uint8_t run2) -> bool {
      auto expr = ResourceDASM::Expression::Node::parse(expr_str);
      ExpressionEnvironment env{.num_players = 1, .section_id = 0, .event = 0, .v1_present = false,
          .difficulty = difficulty, .quest_flags = nullptr, .challenge_records = nullptr, .team = nullptr,
          .spire_rooms = 0, .spire_run1 = run1, .spire_run2 = run2};
      return env.evaluate(*expr) != 0;
    };
    expect(available("V_Difficulty == 0", Difficulty::NORMAL, 0, 0)); // q921
    expect(!available("V_Difficulty == 0", Difficulty::HARD, 3, 3));
    expect(!available("V_Difficulty == 1 && SPIRE_RUN1 >= 1", Difficulty::HARD, 0, 3)); // q981 closed
    expect(available("V_Difficulty == 1 && SPIRE_RUN1 >= 1", Difficulty::HARD, 1, 0)); // q981 open
    expect(!available("V_Difficulty == 3 && SPIRE_RUN1 >= 3", Difficulty::ULTIMATE, 2, 0)); // q983 closed
    expect(available("V_Difficulty == 3 && SPIRE_RUN1 >= 3", Difficulty::ULTIMATE, 3, 0));
    expect(available("V_Difficulty == 2 && SPIRE_RUN2 >= 2", Difficulty::VERY_HARD, 0, 2)); // q985
    expect(!available("V_Difficulty == 2 && SPIRE_RUN2 >= 2", Difficulty::VERY_HARD, 2, 1));

    // win flags on the game's difficulty; the payout always on Normal (plan §5), so q925 on Normal pays every rung
    expect_eq(2, spire_meta_flag_difficulty(SPIRE_META_FLAG_RUN1_WON, 2));
    expect_eq(1, spire_meta_flag_difficulty(SPIRE_META_FLAG_DEROLLE, 1));
    expect_eq(0, spire_meta_flag_difficulty(SPIRE_META_FLAG_PAY_WIN, 3));
    expect_eq(0, spire_meta_flag_difficulty(SPIRE_META_FLAG_PAY_DEATHLESS, 1));
    expect_eq(0, spire_meta_flag_difficulty(SPIRE_META_FLAG_PAY_RECORD, 2));
    expect_eq(0, spire_meta_flag_difficulty(SPIRE_META_FLAG_PAY_WIN, 0));
    expect(SPIRE_META_FLAG_RUN2_WON <= SPIRE_META_SERVER_LAST_FLAG); // server-owned: a client write never sets it
    expect(spire_meta_flag_name(SPIRE_META_FLAG_RUN1_WON) == nullptr); // named by on_finish with the next rung

    // game creation: Sega's level check passes, or the rung skips it in a Battle game, or a refusal with a message
    // (Hard min level 19 = Lv20, BidX Lv52 = level 51 against Ultimate Lv80 = 79)
    expect(spire_meta_ladder_gate(true, true, 0, 1, 51, 19) == SpireLadderGate::LEVEL_OK);
    expect(spire_meta_ladder_gate(true, true, 3, 3, 51, 79) == SpireLadderGate::RUNG_OPEN);
    expect(spire_meta_ladder_gate(true, true, 2, 3, 51, 79) == SpireLadderGate::REFUSED); // rung below: refused
    expect(spire_meta_ladder_gate(false, true, 3, 3, 51, 79) == SpireLadderGate::REFUSED); // Normal mode: Sega's rule
    expect(spire_meta_ladder_gate(true, false, 3, 3, 51, 79) == SpireLadderGate::REFUSED); // not a Spire character
    expect(spire_meta_ladder_gate(true, true, 1, 1, 0, 19) == SpireLadderGate::RUNG_OPEN); // a Lv1 test character
    std::string refuse = spire_meta_ladder_refuse_text(3, 79);
    expect_eq(std::string("$C7Ultimate needs level 80. Spire: win Spire Run on Very Hard to open it."), refuse);
    expect(refuse.find('\n') == std::string::npos); // one line
    expect_eq(std::string("$C7Hard needs level 20. Spire: win Spire Run on Normal to open it."),
        spire_meta_ladder_refuse_text(1, 19));

    // quest menu on a difficulty whose run quest is hidden: one info line, nothing when both are open or on Normal
    expect_eq(std::string(""), spire_meta_ladder_menu_note(0, 0, 0));
    expect_eq(std::string(""), spire_meta_ladder_menu_note(1, 1, 1));
    expect_eq(std::string("Spire Run on Hard opens after a win on Normal."), spire_meta_ladder_menu_note(0, 0, 1));
    expect_eq(std::string("Spire Run II on Very Hard opens after a win on Hard."), spire_meta_ladder_menu_note(3, 1, 2));
    expect_eq(std::string("Spire Run I on Ultimate opens after a win on Very Hard."), spire_meta_ladder_menu_note(2, 3, 3));
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

  phosg::log_info_f("-- Spire rooms k9-k11: Mines 1, Mines 2 and Vol Opt checkpoints and unlocks (plan-mines-volopt section 5)");
  {
    // SpireCheckpointRooms as pso-roguelike quests/spire/build.sh writes it (build-pc/newserv-config.json)
    std::map<std::pair<uint8_t, int32_t>, uint8_t> rooms = {{{1, 7}, 2}, {{1, 10}, 2}, {{1, 11}, 1}, {{2, 2}, 3},
        {{3, 50}, 5}, {{4, 14}, 6}, {{5, 50}, 7}, {{6, 50}, 9}, {{7, 60}, 10}, {{11, -1}, 4}, {{12, -1}, 8},
        {{13, -1}, 11}};
    expect_eq(9, spire_checkpoint_room_k(rooms, 6, 50));  // Mines 1
    expect_eq(10, spire_checkpoint_room_k(rooms, 7, 60)); // Mines 2
    expect_eq(11, spire_checkpoint_room_k(rooms, 13, 0)); // Vol Opt: any room of floor 13
    expect_eq(11, spire_checkpoint_room_k(rooms, 13, 3));
    expect_eq(8, spire_checkpoint_room_k(rooms, 12, 0));  // De Rol Le unchanged
    expect_eq(4, spire_checkpoint_room_k(rooms, 11, 0));  // Dragon unchanged
    expect_eq(0, spire_checkpoint_room_k(rooms, 6, 49));  // another Mines 1 room: no checkpoint
    expect_eq(0, spire_checkpoint_room_k(rooms, 7, 50));
    expect_eq(0, spire_checkpoint_room_k(rooms, 8, 0));   // without the Ruins rooms: no checkpoint
    DataIndex d;
    auto available = [&](const char* expr_str, uint32_t checkpoint_rooms) -> bool {
      auto expr = ResourceDASM::Expression::Node::parse(expr_str);
      ExpressionEnvironment env{.num_players = 1, .section_id = 0, .event = 0, .v1_present = false,
          .difficulty = Difficulty::NORMAL, .quest_flags = nullptr, .challenge_records = nullptr, .team = nullptr,
          .spire_rooms = d.spire_availability_rooms(3117539666, checkpoint_rooms)};
      return env.evaluate(*expr) != 0;
    };
    uint32_t to_k8 = 0xFF, to_k9 = 0x1FF, to_k10 = 0x3FF, to_k11 = 0x7FF;
    expect(available("SPIRE_K8", to_k8) && !available("SPIRE_K9", to_k8)); // q954 (Mines 2) still locked after De Rol Le
    expect(available("SPIRE_K9", to_k9) && !available("SPIRE_K10", to_k9)); // q954 open, q955 (Vol Opt) still locked
    expect(available("SPIRE_K10", to_k10) && !available("SPIRE_K11", to_k10)); // q955 open
    expect(available("SPIRE_K11", to_k11));
    expect(available("SPIRE_MAX == 11", to_k11) && available("SPIRE_MAX == 10", to_k10) && available("SPIRE_MAX == 9", to_k9));
    expect(available("SPIRE_K10", 1u << 9) && !available("SPIRE_K9", 1u << 9)); // only room 10 stored: bit 9
  }

  phosg::log_info_f("-- Spire rooms k12-k15: Ruins 1-3 and Dark Falz checkpoints, unlocks, clear and drop (plan-ruins-darkfalz section 7)");
  {
    // SpireCheckpointRooms with the Ruins rooms of the plan's room table: (8, 60), (9, 31), (10, 40), floor 14 any room
    std::map<std::pair<uint8_t, int32_t>, uint8_t> rooms = {{{1, 7}, 2}, {{1, 10}, 2}, {{1, 11}, 1}, {{2, 2}, 3},
        {{3, 50}, 5}, {{4, 14}, 6}, {{5, 50}, 7}, {{6, 50}, 9}, {{7, 60}, 10}, {{8, 60}, 12}, {{9, 31}, 13},
        {{10, 40}, 14}, {{11, -1}, 4}, {{12, -1}, 8}, {{13, -1}, 11}, {{14, -1}, 15}};
    expect_eq(12, spire_checkpoint_room_k(rooms, 8, 60));  // Ruins 1
    expect_eq(13, spire_checkpoint_room_k(rooms, 9, 31));  // Ruins 2
    expect_eq(14, spire_checkpoint_room_k(rooms, 10, 40)); // Ruins 3
    expect_eq(15, spire_checkpoint_room_k(rooms, 14, 0));  // Dark Falz: any room of floor 14
    expect_eq(15, spire_checkpoint_room_k(rooms, 14, 7));
    expect_eq(0, spire_checkpoint_room_k(rooms, 8, 0));    // another Ruins room: no checkpoint
    expect_eq(0, spire_checkpoint_room_k(rooms, 9, 40));
    expect_eq(0, spire_checkpoint_room_k(rooms, 10, 60));
    expect_eq(11, spire_checkpoint_room_k(rooms, 13, 0));  // Vol Opt, Mines, De Rol Le, Dragon unchanged
    expect_eq(10, spire_checkpoint_room_k(rooms, 7, 60));
    expect_eq(8, spire_checkpoint_room_k(rooms, 12, 0));
    expect_eq(4, spire_checkpoint_room_k(rooms, 11, 0));
    DataIndex d;
    auto available = [&](const char* expr_str, uint32_t checkpoint_rooms) -> bool {
      auto expr = ResourceDASM::Expression::Node::parse(expr_str);
      ExpressionEnvironment env{.num_players = 1, .section_id = 0, .event = 0, .v1_present = false,
          .difficulty = Difficulty::NORMAL, .quest_flags = nullptr, .challenge_records = nullptr, .team = nullptr,
          .spire_rooms = d.spire_availability_rooms(3117539666, checkpoint_rooms)};
      return env.evaluate(*expr) != 0;
    };
    // q956-q959 (k12-k15) open in order: q956 after Vol Opt (SPIRE_K11), q957 after K12, q958 after K13, q959 after K14
    uint32_t to_k11 = 0x7FF, to_k12 = 0xFFF, to_k13 = 0x1FFF, to_k14 = 0x3FFF, to_k15 = 0x7FFF;
    expect(available("SPIRE_K11", to_k11) && !available("SPIRE_K12", to_k11)); // q956 open, q957 locked
    expect(available("SPIRE_K12", to_k12) && !available("SPIRE_K13", to_k12)); // q957 open, q958 locked
    expect(available("SPIRE_K13", to_k13) && !available("SPIRE_K14", to_k13)); // q958 open, q959 locked
    expect(available("SPIRE_K14", to_k14) && !available("SPIRE_K15", to_k14)); // q959 open
    expect(available("SPIRE_K15", to_k15) && available("SPIRE_MAX == 15", to_k15) && available("SPIRE_MAX == 14", to_k14));
    expect(available("SPIRE_K14", 1u << 13) && !available("SPIRE_K13", 1u << 13)); // only room 14 stored: bit 13
    expect(!available("SPIRE_K12", 0x3FFu)); // the test serial's rooms 1-10 do not open the Ruins

    // Room clear: no Falz part or Darvant clears through 6x76 (a Darvant's 6x76 is redirected to the Falz root);
    // only the final-phase flag does, 0x35 on Normal and 0x37 above. Vol Opt keeps its 6x76 clear.
    expect(spire_bridge_is_server_clear_boss(EnemyType::VOL_OPT_2));
    for (auto part : {EnemyType::DARK_FALZ_1, EnemyType::DARK_FALZ_2, EnemyType::DARK_FALZ_3, EnemyType::DARVANT}) {
      expect(!spire_bridge_is_server_clear_boss(part));
    }
    expect(spire_bridge_dark_falz_final_phase(Difficulty::NORMAL, 0x0035) == EnemyType::DARK_FALZ_2);
    expect(spire_bridge_dark_falz_final_phase(Difficulty::NORMAL, 0x0037) == EnemyType::NONE);
    expect(spire_bridge_dark_falz_final_phase(Difficulty::NORMAL, 0x0036) == EnemyType::NONE);
    for (auto diff : {Difficulty::HARD, Difficulty::VERY_HARD, Difficulty::ULTIMATE}) {
      expect(spire_bridge_dark_falz_final_phase(diff, 0x0037) == EnemyType::DARK_FALZ_3);
      expect(spire_bridge_dark_falz_final_phase(diff, 0x0035) == EnemyType::NONE);
    }
    expect(spire_bridge_dark_falz_final_phase(Difficulty::NORMAL, 0x0057) == EnemyType::NONE); // Olga Flow's flag

    // Boss drop: only the final phase; DF1 and the Darvants give no boss weapon; Dragon, De Rol Le, Vol Opt unchanged
    expect(spire_drops_is_boss_drop(EnemyType::DARK_FALZ_2));
    expect(spire_drops_is_boss_drop(EnemyType::DARK_FALZ_3));
    expect(type_definition_for_enemy(EnemyType::DARK_FALZ_1).is_boss()); // Sega's flag: a boss part
    expect(!spire_drops_is_boss_drop(EnemyType::DARK_FALZ_1));
    expect(!spire_drops_is_boss_drop(EnemyType::DARVANT));
    expect(spire_drops_is_boss_drop(EnemyType::DRAGON));
    expect(spire_drops_is_boss_drop(EnemyType::DE_ROL_LE));
    expect(spire_drops_is_boss_drop(EnemyType::VOL_OPT_2));
    expect(!spire_drops_is_boss_drop(EnemyType::VOL_OPT_1));
  }

  phosg::log_info_f("-- Spire rooms k16-k18: Episode 2 VR Temple and Barba Ray checkpoints per episode and drop (plan-ep2-temple section 7)");
  {
    // SpireCheckpointRooms with an Episode field: entries without it are Episode 1, so the Ep1 rooms stay as they were
    std::map<uint8_t, std::map<std::pair<uint8_t, int32_t>, uint8_t>> rooms = {
        {1, {{{1, 7}, 2}, {{1, 10}, 2}, {{1, 11}, 1}, {{2, 2}, 3}, {{3, 50}, 5}, {{4, 14}, 6}, {{5, 50}, 7},
                {{6, 50}, 9}, {{7, 60}, 10}, {{8, 60}, 12}, {{9, 31}, 13}, {{10, 40}, 14}, {{11, -1}, 4},
                {{12, -1}, 8}, {{13, -1}, 11}, {{14, -1}, 15}}},
        {2, {{{1, 70}, 16}, {{2, 50}, 17}, {{14, -1}, 18}}}};
    expect_eq(15, spire_checkpoint_room_k(rooms, Episode::EP1, 14, 0)); // Dark Falz stays k15
    expect_eq(15, spire_checkpoint_room_k(rooms, Episode::EP1, 14, 7));
    expect_eq(18, spire_checkpoint_room_k(rooms, Episode::EP2, 14, 0)); // Barba Ray: any room of Ep2 floor 14
    expect_eq(18, spire_checkpoint_room_k(rooms, Episode::EP2, 14, 1));
    expect_eq(16, spire_checkpoint_room_k(rooms, Episode::EP2, 1, 70)); // VR Temple Alpha
    expect_eq(17, spire_checkpoint_room_k(rooms, Episode::EP2, 2, 50)); // VR Temple Beta
    expect_eq(0, spire_checkpoint_room_k(rooms, Episode::EP2, 1, 7));   // Ep1 Forest 1 room 7 is not an Ep2 room
    expect_eq(0, spire_checkpoint_room_k(rooms, Episode::EP2, 2, 2));
    expect_eq(0, spire_checkpoint_room_k(rooms, Episode::EP2, 13, 0));  // Vol Opt's floor number in Ep2: nothing
    expect_eq(0, spire_checkpoint_room_k(rooms, Episode::EP1, 1, 70));  // the Temple room numbers in Ep1: nothing
    expect_eq(2, spire_checkpoint_room_k(rooms, Episode::EP1, 1, 7));   // Ep1 unchanged
    expect_eq(12, spire_checkpoint_room_k(rooms, Episode::EP1, 8, 60));
    expect_eq(0, spire_checkpoint_room_k(rooms, Episode::EP4, 14, 0));  // no Ep4 rooms
    expect_eq(0, spire_checkpoint_room_k(rooms, Episode::NONE, 14, 0));
    // SpireCheckpointArrivals: Ep1 floor 14 is k14 (the rest room before Dark Falz), Ep2 floor 14 is k17
    std::map<uint8_t, std::map<uint8_t, uint8_t>> arrivals = {{1, {{14, 14}}}, {2, {{14, 17}}}};
    expect_eq(14, spire_checkpoint_arrival_k(arrivals, Episode::EP1, 14));
    expect_eq(17, spire_checkpoint_arrival_k(arrivals, Episode::EP2, 14));
    expect_eq(0, spire_checkpoint_arrival_k(arrivals, Episode::EP2, 13));
    expect_eq(0, spire_checkpoint_arrival_k(arrivals, Episode::EP4, 14));

    // The config as build.sh writes it: entries without Episode are Episode 1, with "Episode": 2 a separate table
    {
      DataIndex d;
      d.config_json = std::make_shared<phosg::JSON>(phosg::JSON::parse(R"({
        "SpireCheckpointRooms": [{"Floor": 14, "Room": -1, "K": 15}, {"Floor": 1, "Room": 7, "K": 2},
            {"Episode": 2, "Floor": 1, "Room": 70, "K": 16}, {"Episode": 2, "Floor": 2, "Room": 50, "K": 17},
            {"Episode": 2, "Floor": 14, "K": 18}],
        "SpireCheckpointArrivals": [{"Floor": 14, "K": 14}, {"Episode": 2, "Floor": 14, "K": 17}]})"));
      d.load_spire_checkpoint_config();
      expect_eq(15, spire_checkpoint_room_k(d.spire_checkpoint_rooms, Episode::EP1, 14, 3));
      expect_eq(18, spire_checkpoint_room_k(d.spire_checkpoint_rooms, Episode::EP2, 14, 3));
      expect_eq(2, spire_checkpoint_room_k(d.spire_checkpoint_rooms, Episode::EP1, 1, 7));
      expect_eq(0, spire_checkpoint_room_k(d.spire_checkpoint_rooms, Episode::EP2, 1, 7));
      expect_eq(16, spire_checkpoint_room_k(d.spire_checkpoint_rooms, Episode::EP2, 1, 70));
      expect_eq(17, spire_checkpoint_room_k(d.spire_checkpoint_rooms, Episode::EP2, 2, 50));
      expect_eq(14, spire_checkpoint_arrival_k(d.spire_checkpoint_arrivals, Episode::EP1, 14));
      expect_eq(17, spire_checkpoint_arrival_k(d.spire_checkpoint_arrivals, Episode::EP2, 14));
    }

    // Boss drop: Barba Ray yes; its joints (Sega's boss flag) and the Pig Rays no
    expect(spire_drops_is_boss_drop(EnemyType::BARBA_RAY));
    expect(type_definition_for_enemy(EnemyType::BARBA_RAY_JOINT).is_boss()); // Sega's flag: a boss part
    expect(!spire_drops_is_boss_drop(EnemyType::BARBA_RAY_JOINT));
    expect(!spire_drops_is_boss_drop(EnemyType::PIG_RAY));
    expect(!spire_drops_is_boss_drop(EnemyType::BARBA_RAY, 0x2F)); // the trailing BARBA_RAY child of the set

    // Boss drop: only the head of a boss set. Gol Dragon's set is the head plus 5 GOL_DRAGON children (Map.cc
    // TBoss8Dragon, default_num_children = 5), each with its own drop flag; only the head gives the boss drop
    expect(spire_drops_is_boss_drop(EnemyType::GOL_DRAGON));
    expect(spire_drops_is_boss_drop(EnemyType::GOL_DRAGON, 0));
    for (uint16_t child = 1; child <= 5; child++) {
      expect(!spire_drops_is_boss_drop(EnemyType::GOL_DRAGON, child));
    }
    expect(spire_drops_is_boss_drop(EnemyType::GAL_GRYPHON)); // no children in its set
    expect(spire_drops_is_boss_drop(EnemyType::OLGA_FLOW_2)); // its 0x200 children alias the head
    expect(spire_drops_is_boss_drop(EnemyType::DARK_FALZ_3)); // Dark Falz's phases alias the head
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

  phosg::log_info_f("-- Spire meta flags: id distinct per character on one card, overlay is the real character (audit S-6)");
  {
    PlayerDispDataV4 a, b, a_class, a_section;
    a.visual.name.encode("Alpha", Language::ENGLISH);
    a.visual.sh.char_class = 0;
    a.visual.sh.section_id = 3;
    b = a;
    b.visual.name.encode("Beta", Language::ENGLISH);
    a_class = a;
    a_class.visual.sh.char_class = 1;
    a_section = a;
    a_section.visual.sh.section_id = 4;
    expect(spire_guard_character_id(a) != spire_guard_character_id(b));         // another name
    expect(spire_guard_character_id(a) != spire_guard_character_id(a_class));   // same name, another class
    expect(spire_guard_character_id(a) != spire_guard_character_id(a_section)); // same name, another section ID
    // What create_battle_overlay changes (level, EXP, stats, ESP, meseta) never moves the id: a run's overlay records
    // to the real character's file
    PlayerDispDataV4 overlay = a;
    overlay.stats.level = 98;
    overlay.stats.exp = 123456;
    overlay.stats.esp = 40;
    overlay.stats.meseta = 300;
    overlay.stats.char_stats.atp = 1;
    expect_eq(spire_guard_character_id(a), spire_guard_character_id(overlay));
    expect_eq(spire_meta_filename_for(42, &a), spire_meta_filename_for(42, &overlay));
    auto fa = spire_meta_filename_for(42, &a);
    auto fb = spire_meta_filename_for(42, &b);
    expect(fa != fb);
    expect(fa.ends_with(std::format("/0000000042.{}.meta.json", spire_guard_character_id(a))));
    expect(spire_meta_filename_for(42, nullptr).empty()); // no character data: nothing read or stored
  }

  phosg::log_info_f("-- Spire meta flags: A's unlocks and q925 payout never reach B on the same card (audit S-6)");
  {
    PlayerDispDataV4 a, b;
    a.visual.name.encode("Alpha", Language::ENGLISH);
    b.visual.name.encode("Beta", Language::ENGLISH);
    auto dir = std::filesystem::temp_directory_path() / std::format("newserv-static-spire-meta-{}", getpid());
    std::filesystem::create_directories(dir);
    auto local = [&](const std::string& filename) {
      return (dir / std::filesystem::path(filename).filename()).string();
    };
    // A won a run: Haste unlocked and the payout open, on Normal
    std::string flags_json = "{\"SerialNumber\":42,\"Flags\":[{\"Difficulty\":0,\"Flag\":900,\"Set\":true},"
                             "{\"Difficulty\":0,\"Flag\":928,\"Set\":true},{\"Difficulty\":0,\"Flag\":929,\"Set\":true}]}";
    phosg::save_file(local(spire_meta_filename_for(42, &a)), flags_json);
    // The v5-v21 per-card file holds the same flags; it is nobody's meta file and is never read
    std::string legacy = std::format("{}/{:010}.json", SPIRE_BRIDGE_DIRECTORY, 42);
    phosg::save_file(local(legacy), flags_json);
    expect(spire_meta_filename_for(42, &a) != legacy);
    expect(spire_meta_filename_for(42, &b) != legacy);
    expect(spire_bridge_registers_filename_for(42, &b) != legacy);

    auto stored_a = spire_meta_load(local(spire_meta_filename_for(42, &a)));
    auto stored_b = spire_meta_load(local(spire_meta_filename_for(42, &b)));
    expect_eq(3u, stored_a.size());
    expect(stored_b.empty()); // B has no file of its own: the legacy file next to it is not taken
    auto value_of = [](const std::vector<SpireMetaDelivery>& v, uint16_t flag) -> int {
      int ret = -1;
      for (const auto& e : v) {
        if ((e.difficulty == 0) && (e.flag == flag)) {
          expect_eq(-1, ret); // one entry per flag
          ret = e.set ? 1 : 0;
        }
      }
      return ret;
    };
    expect(spire_meta_load_file(local(spire_meta_filename_for(42, &a))).exists);
    expect(spire_meta_load_file(local(spire_meta_filename_for(42, &a))).adopted == SpireMetaAdopted::NO_MARKER);
    expect(!spire_meta_load_file(local(spire_meta_filename_for(42, &b))).exists);
    // A's file is from before B-REGIE-10-2 (no marker); B here has an empty file of its own from then: both cleared
    // as before
    SpireMetaFile file_a{.exists = true, .adopted = SpireMetaAdopted::NO_MARKER, .flags = stored_a, .base_rung = {}, .upgrades = {}};
    SpireMetaFile file_b{.exists = true, .adopted = SpireMetaAdopted::NO_MARKER, .flags = stored_b, .base_rung = {}, .upgrades = {}};
    auto to_a = spire_meta_delivery_entries(file_a, 0, false);
    auto to_b = spire_meta_delivery_entries(file_b, 0, false);
    expect_eq(1, value_of(to_a, 0x0384));                    // A's own unlock
    expect_eq(1, value_of(to_a, SPIRE_META_FLAG_PAY_WIN));   // A's payout
    expect_eq(1, value_of(to_a, SPIRE_META_FLAG_PAY_DEATHLESS));
    expect_eq(0, value_of(to_a, SPIRE_META_FLAG_PAY_RECORD));
    expect_eq(0, value_of(to_b, 0x0384));                    // not B's unlock: cleared, even after A's join sync
    expect_eq(0, value_of(to_b, SPIRE_META_FLAG_PAY_WIN));   // A's payout is not visible to B in q925
    expect_eq(0, value_of(to_b, SPIRE_META_FLAG_PAY_DEATHLESS));
    expect_eq(-1, value_of(to_b, 0x03A8));                   // q925's S-rank received stays the client's own
    expect_eq(-1, value_of(to_b, 0x03A9));
    expect_eq(static_cast<size_t>(SPIRE_META_SERVER_LAST_FLAG - SPIRE_BRIDGE_FIRST_FLAG + 1), to_b.size());
    expect_eq(to_b.size(), to_a.size());
    // A live write in a Spire or q925 game stays with its writer: not forwarded to B, not in the game's flag state
    expect(!spire_meta_flag_is_shared(true, SPIRE_META_FLAG_FIRST_RUN));
    expect(!spire_meta_flag_is_shared(true, SPIRE_META_FLAG_PAY_WIN));
    expect(!spire_meta_flag_is_shared(true, 0x03A8));
    expect(!spire_meta_flag_is_shared(true, SPIRE_BRIDGE_LAST_FLAG));
    expect(spire_meta_flag_is_shared(true, SPIRE_BRIDGE_FIRST_FLAG - 1)); // a quest's own flags: shared as before
    expect(spire_meta_flag_is_shared(true, SPIRE_BRIDGE_LAST_FLAG + 1));
    expect(spire_meta_flag_is_shared(false, SPIRE_META_FLAG_PAY_WIN));    // a normal game: unchanged
    std::filesystem::remove_all(dir);
  }

  phosg::log_info_f("-- Spire meta flags: a character without a meta file loses nothing at its first delivery (B-REGIE-10-2)");
  {
    auto value_of = [](const std::vector<SpireMetaDelivery>& v, uint8_t difficulty, uint16_t flag) -> int {
      int ret = -1;
      for (const auto& e : v) {
        if ((e.difficulty == difficulty) && (e.flag == flag)) {
          expect_eq(-1, ret); // one entry per flag
          ret = e.set ? 1 : 0;
        }
      }
      return ret;
    };
    auto clears = [](const std::vector<SpireMetaDelivery>& v) {
      size_t ret = 0;
      for (const auto& e : v) {
        ret += e.set ? 0 : 1;
      }
      return ret;
    };
    // Known source: A has 0x380 and 0x384 on Normal, 0x3A0 on Hard, and 0x3A8 (q925's, not server-owned)
    QuestFlags current;
    current.set(Difficulty::NORMAL, 0x0380);
    current.set(Difficulty::NORMAL, 0x0384);
    current.set(Difficulty::HARD, SPIRE_META_FLAG_PAY_WIN);
    current.set(Difficulty::NORMAL, 0x03A8);
    current.set(Difficulty::NORMAL, 0x0200);
    auto adopted = spire_meta_adopt(&current);
    expect(adopted.exists);
    expect(adopted.adopted == SpireMetaAdopted::KNOWN);
    SpireMetaFlags expected_flags{{{0, 0x0380}, true}, {{0, 0x0384}, true}, {{1, SPIRE_META_FLAG_PAY_WIN}, true}};
    expect(adopted.flags == expected_flags); // exactly the server-owned flags it has, per difficulty
    auto first = spire_meta_delivery_entries(adopted, 0, true);
    expect_eq(0u, clears(first)); // the first delivery clears nothing
    expect_eq(1, value_of(first, 0, 0x0380));
    expect_eq(1, value_of(first, 0, 0x0384));
    expect_eq(1, value_of(first, 1, SPIRE_META_FLAG_PAY_WIN));
    expect_eq(-1, value_of(first, 0, 0x0381));
    expect_eq(3u, first.size());
    // The next delivery (file exists, KNOWN) clears only the server-owned flags without an entry
    auto second = spire_meta_delivery_entries(adopted, 0, false);
    expect_eq(1, value_of(second, 0, 0x0380));
    expect_eq(1, value_of(second, 0, 0x0384));
    expect_eq(0, value_of(second, 0, 0x0381));
    expect_eq(0, value_of(second, 0, SPIRE_META_FLAG_PAY_RECORD));
    expect_eq(-1, value_of(second, 0, 0x03A8)); // not server-owned: never cleared
    expect_eq(static_cast<size_t>(SPIRE_META_SERVER_LAST_FLAG - SPIRE_BRIDGE_FIRST_FLAG + 1 - 2), clears(second));

    // Unknown source (GC v3): an empty file marked unknown; no delivery ever clears a flag it never saw
    auto unknown = spire_meta_adopt(nullptr);
    expect(unknown.exists);
    expect(unknown.adopted == SpireMetaAdopted::UNKNOWN);
    expect(unknown.flags.empty());
    expect(spire_meta_delivery_entries(unknown, 0, true).empty());
    expect(spire_meta_delivery_entries(unknown, 0, false).empty());
    // After a run the server set 0x380 for it and the character cleared 0x3A0 in q925: those entries are sent, nothing else
    unknown.flags[std::make_pair(0, SPIRE_META_FLAG_FIRST_RUN)] = true;
    unknown.flags[std::make_pair(0, SPIRE_META_FLAG_PAY_WIN)] = false;
    auto later = spire_meta_delivery_entries(unknown, 0, false);
    expect_eq(2u, later.size());
    expect_eq(1, value_of(later, 0, SPIRE_META_FLAG_FIRST_RUN));
    expect_eq(0, value_of(later, 0, SPIRE_META_FLAG_PAY_WIN));
    expect_eq(-1, value_of(later, 0, 0x0384));

    // The marker survives on disk; a file without it keeps the S-6 clears
    auto dir = std::filesystem::temp_directory_path() / std::format("newserv-static-spire-adopt-{}", getpid());
    std::filesystem::create_directories(dir);
    auto unknown_name = (dir / "unknown.meta.json").string();
    auto known_name = (dir / "known.meta.json").string();
    auto old_name = (dir / "old.meta.json").string();
    phosg::save_file(unknown_name, "{\"SerialNumber\":42,\"Adopted\":\"unknown\",\"Flags\":[]}");
    phosg::save_file(known_name, "{\"SerialNumber\":42,\"Adopted\":\"known\",\"Flags\":[]}");
    phosg::save_file(old_name, "{\"SerialNumber\":42,\"Flags\":[]}");
    expect(spire_meta_load_file(unknown_name).adopted == SpireMetaAdopted::UNKNOWN);
    expect(spire_meta_load_file(known_name).adopted == SpireMetaAdopted::KNOWN);
    expect(spire_meta_load_file(old_name).adopted == SpireMetaAdopted::NO_MARKER);
    expect(spire_meta_delivery_entries(spire_meta_load_file(unknown_name), 0, false).empty());
    expect_eq(static_cast<size_t>(SPIRE_META_SERVER_LAST_FLAG - SPIRE_BRIDGE_FIRST_FLAG + 1),
        clears(spire_meta_delivery_entries(spire_meta_load_file(old_name), 0, false)));
    std::filesystem::remove_all(dir);
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

    // tot_k (act 3, Ruins): entry 1 ends at room 1, so from room 2 on its replacement is drawn, also by the walk
    std::string ruins_json = table_json(4, 0);
    for (size_t pos; (pos = ruins_json.find("\"vanaf_k\":1,\"android_ok\":false")) != std::string::npos;) {
      ruins_json.replace(pos, 11, "\"vanaf_k\":1,\"tot_k\":1");
    }
    auto ruins = load(ruins_json);
    std::filesystem::remove(filename);
    expect_eq(1u, static_cast<uint32_t>(ruins.archetypes.at(0).entries.at(0).tot_k));
    expect_eq(0xFFu, static_cast<uint32_t>(ruins.archetypes.at(0).entries.at(1).tot_k));  // no tot_k: every room
    SpireDrawInput rin;
    rin.k = 1;
    expect_eq(1u, ((spire_draw(ruins, rin, rnd0) >> 6) & 0x3F));
    rin.k = 2;
    expect_eq(2u, ((spire_draw(ruins, rin, rnd0) >> 6) & 0x3F));
  }

  phosg::log_info_f("-- Spire meta: flags only in a game with a Spire quest (regie pso-regie-9)");
  {
    // arguments: is_game, has_quest, spire_quest (spire_game_is_spire), meta_quest (SpireMetaQuests)
    expect(spire_meta_delivery_applies(true, true, true, false));     // q920/q921/q940-q955 loaded: the run's flags
    expect(spire_meta_delivery_applies(true, true, false, true));     // q925 (SpireMetaQuests): the payout flags
    expect(!spire_meta_delivery_applies(true, false, false, false));  // Battle game before its quest (town start), q0
    expect(!spire_meta_delivery_applies(true, true, false, false));   // a normal game with another quest
    expect(!spire_meta_delivery_applies(false, false, false, false)); // lobby
    // arguments: bridged_quest, meta_quest, flag_num, is_set
    expect(spire_meta_record_applies(true, false, SPIRE_META_FLAG_FIRST_RUN, true)); // run quest: every write
    expect(spire_meta_record_applies(true, false, SPIRE_META_FLAG_PAY_WIN, false));
    for (uint16_t f = SPIRE_META_FLAG_PAY_WIN; f <= SPIRE_META_FLAG_PAY_RECORD; f++) {
      expect(spire_meta_record_applies(false, true, f, false));  // q925 paid: the clear is stored, not set again
      expect(!spire_meta_record_applies(false, true, f, true));  // q925 never sets a payout flag on the server
    }
    expect(!spire_meta_record_applies(false, true, 0x03A8, true));  // S-rank received: the client's own
    expect(!spire_meta_record_applies(false, true, 0x03A9, true));  // ever paid: the client's own
    expect(!spire_meta_record_applies(false, true, SPIRE_META_FLAG_FIRST_RUN, false)); // no unlock clear from q925
    expect(!spire_meta_record_applies(false, false, SPIRE_META_FLAG_PAY_WIN, false));  // a normal quest: nothing
    DataIndex d;
    expect(d.spire_meta_quest_numbers.empty()); // no key: no quest
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

    // Route B F2: 'F' round trip with the pacts in bits 19-21 (clamped to 4). Bits 16-18 and 23 keep their meaning,
    // bit 22 stays 0, the seconds stay in the low 16 bits, and the flags do not depend on the pacts
    for (uint32_t seconds : {0u, 0xFFFFu}) {
      for (uint32_t exams = 0; exams <= 2; exams++) {
        for (uint32_t deathless = 0; deathless <= 1; deathless++) {
          for (uint32_t won = 0; won <= 1; won++) {
            for (uint32_t pacts = 0; pacts <= 7; pacts++) {
              uint32_t value = (static_cast<uint32_t>(SPIRE_BRIDGE_REC_FINISH) << 24) | (won << 23) |
                  (pacts << SPIRE_META_FINISH_PACTS_SHIFT) | (deathless ? SPIRE_META_FINISH_DEATHLESS : 0) |
                  (exams << SPIRE_META_FINISH_EXAMS_SHIFT) | seconds;
              expect_eq(0u, (value >> 22) & 1);
              auto d = spire_meta_decode_finish(value);
              expect_eq(won == 1, d.won);
              expect_eq(won ? 2u : exams, static_cast<uint32_t>(d.exams));
              expect_eq(deathless == 1, d.deathless);
              expect_eq(std::min<uint32_t>(pacts, 4), static_cast<uint32_t>(d.pacts));
              expect_eq(seconds, value & 0xFFFF);
              auto no_pacts = spire_meta_decode_finish(value & ~(7u << SPIRE_META_FINISH_PACTS_SHIFT));
              expect_eq(0u, static_cast<uint32_t>(no_pacts.pacts));
              expect((no_pacts.won == d.won) && (no_pacts.exams == d.exams) && (no_pacts.deathless == d.deathless));
              expect(spire_meta_flags_for_finish(no_pacts, 0, false) == spire_meta_flags_for_finish(d, 0, false));
            }
          }
        }
      }
    }
    auto pacts7 = spire_meta_decode_finish(7u << SPIRE_META_FINISH_PACTS_SHIFT); // all pact bits, won bit clear
    expect_eq(4u, static_cast<uint32_t>(pacts7.pacts));
    expect(!pacts7.won && (pacts7.exams == 0) && !pacts7.deathless);
  }

  phosg::log_info_f("-- Spire drops: class filter, one area ahead, pity, one boss weapon per player (B-E01-108)");
  {
    auto pmt = ItemParameterTable::from_json(phosg::JSON::parse(phosg::load_file("system/tables/item-parameter-table-gc-v3.json")));
    JSONLevelTable level_table(phosg::JSON::parse(phosg::load_file("system/tables/level-table-v3.json")));
    auto item = [](uint8_t d0, uint8_t d1, uint8_t d2) {
      ItemData ret;
      ret.data1[0] = d0;
      ret.data1[1] = d1;
      ret.data1[2] = d2;
      return ret;
    };
    auto same = [](const ItemData& a, const ItemData& b) {
      return (a.data1[0] == b.data1[0]) && (a.data1[1] == b.data1[1]) && (a.data1[2] == b.data1[2]);
    };
    auto player = [&](uint8_t cls, uint16_t atp_now, uint16_t atp_ahead) {
      SpireDropPlayer p;
      p.char_class = cls;
      p.now = level_table.base_stats_for_class(cls);
      p.ahead = p.now;
      p.now.atp = atp_now;
      p.ahead.atp = atp_ahead;
      return p;
    };
    auto lv1 = [&](uint8_t cls, uint32_t ahead) {
      SpireDropPlayer p;
      p.char_class = cls;
      p.now = level_table.base_stats_for_class(cls);
      p.ahead = spire_drops_stats_ahead(level_table, cls, 0, p.now, ahead);
      return p;
    };
    using F = SpireWeaponFit;
    const ItemData saber = item(0, 1, 0), brand = item(0, 1, 1), sword = item(0, 2, 0), cane = item(0, 0x0A, 0);
    const ItemData monomate = item(3, 0, 0), telepipe = item(3, 7, 0), scape_doll = item(3, 9, 0);
    // Weapon usability bits: HUmar = hunter human male, FOnewearl = force newman female
    expect_eq(0x49, spire_weapon_usability_bits(0));
    expect_eq(0xA4, spire_weapon_usability_bits(8));
    // Item 25: Sega's draagtabel. A Sword is for hunters, a Cane for forces
    expect_eq(F::NOW, spire_drops_weapon_fit(*pmt, sword, player(0, 999, 999)));
    expect_eq(F::NO, spire_drops_weapon_fit(*pmt, sword, player(8, 999, 999)));
    expect_eq(F::NO, spire_drops_weapon_fit(*pmt, cane, player(0, 999, 999)));
    expect_eq(F::NOT_A_WEAPON, spire_drops_weapon_fit(*pmt, monomate, player(0, 999, 999)));
    // Item 26: the Brand needs ATP 90: now, one area ahead, or beyond that
    expect_eq(90, pmt->get_weapon(1, 1).atp_required);
    expect_eq(F::NOW, spire_drops_weapon_fit(*pmt, brand, player(0, 90, 90)));
    expect_eq(F::AHEAD, spire_drops_weapon_fit(*pmt, brand, player(0, 50, 95)));
    expect_eq(F::NO, spire_drops_weapon_fit(*pmt, brand, player(0, 50, 80)));
    // One area ahead = LevelsAhead levels of Sega's level table; 0 = only now
    auto humar5 = lv1(0, 5);
    expect(humar5.ahead.atp > humar5.now.atp);
    expect_eq(humar5.now.atp, lv1(0, 0).ahead.atp);

    SpireDropsConfig cfg = SpireDropsConfig::from_json(phosg::JSON::parse("{}"));
    expect(cfg.enabled && cfg.only_run_items && cfg.boss_weapon_per_player);
    expect_eq(ServerDropMode::SERVER_PRIVATE, cfg.drop_mode);
    expect_eq(5u, cfg.levels_ahead);
    expect_eq(8u, cfg.pity_drops);
    bool threw = false;
    try {
      SpireDropsConfig::from_json(phosg::JSON::parse("{\"DropMode\": \"CLIENT\"}"));
    } catch (const std::runtime_error&) {
      threw = true;
    }
    expect(threw);

    // A level-1 hunter or force gets a common weapon he can equip now. A level-1 ranger (ATA 40) meets no ranged
    // weapon's ATA (Handgun 68) in Sega's table: he gets the common weapon of his class he lacks the least for, never
    // one of a group his class cannot use; with ATA 99 one he can equip now
    for (uint8_t cls = 0; cls < 12; cls++) {
      bool ranger = char_class_is_ranger(cls);
      for (uint32_t r = 0; r < 12; r++) {
        auto w = spire_drops_pick_weapon(*pmt, lv1(cls, 5), r);
        expect(!w.empty());
        expect(!pmt->is_item_rare(w));
        expect_eq(ranger ? F::NO : F::NOW, spire_drops_weapon_fit(*pmt, w, lv1(cls, 0)));
        uint8_t bits = spire_weapon_usability_bits(cls);
        expect_eq(bits, pmt->get_weapon(w.data1[1], w.data1[2]).usability_flags & bits);
      }
      auto strong = lv1(cls, 0);
      strong.now.ata = 99;
      expect_eq(F::NOW, spire_drops_weapon_fit(*pmt, spire_drops_pick_weapon(*pmt, strong, 3), strong));
    }
    expect_eq(68, pmt->get_weapon(6, 0).ata_required);
    expect_eq(40, level_table.base_stats_for_class(3).ata); // RAmar
    // Item 25 in the filter: a Force gets no Sword but a weapon a Force can equip now
    {
      uint32_t pity = 3;
      ItemData it = sword;
      expect(spire_drops_filter_item(cfg, *pmt, it, lv1(8, 5), false, pity, 7) != nullptr);
      expect_eq(F::NOW, spire_drops_weapon_fit(*pmt, it, lv1(8, 0)));
      expect_eq(0u, pity);
    }
    // Item 26 in the filter: a Brand one area ahead stays (it counts as a drop without a usable weapon); two areas
    // ahead it becomes a weapon for now; with LevelsAhead 0 the one-area-ahead Brand goes too
    {
      uint32_t pity = 0;
      ItemData it = brand;
      expect(spire_drops_filter_item(cfg, *pmt, it, player(0, 50, 95), false, pity, 0) == nullptr);
      expect(same(it, brand) && (pity == 1));
      it = brand;
      expect(spire_drops_filter_item(cfg, *pmt, it, player(0, 50, 80), false, pity, 0) != nullptr);
      expect(!same(it, brand) && (spire_drops_weapon_fit(*pmt, it, player(0, 50, 50)) == F::NOW) && (pity == 0));
      auto now_only = cfg;
      now_only.levels_ahead = 0;
      auto p = player(0, 50, 50); // what spire_drops_stats_ahead gives with LevelsAhead 0: ahead = now
      it = brand;
      expect(spire_drops_filter_item(now_only, *pmt, it, p, false, pity, 0) != nullptr);
      expect(!same(it, brand));
    }
    // Item 30: only run items drop
    for (auto [d0, d1, keep] : std::vector<std::tuple<uint8_t, uint8_t, bool>>{{0, 1, true}, {1, 1, true}, {1, 2, true},
             {1, 3, false}, {2, 0, false}, {3, 0, true}, {3, 1, true}, {3, 2, false}, {3, 3, true}, {3, 4, true},
             {3, 5, true}, {3, 6, false}, {3, 7, false}, {3, 9, false}, {3, 0x0A, false}, {3, 0x0B, false}, {4, 0, true}}) {
      expect_eq(keep, spire_drops_is_run_item(item(d0, d1, 0)));
    }
    {
      uint32_t pity = 0;
      ItemData it = telepipe;
      expect(spire_drops_filter_item(cfg, *pmt, it, lv1(0, 5), false, pity, 0) != nullptr);
      expect(it.empty() && (pity == 0));
      it = scape_doll;
      spire_drops_filter_item(cfg, *pmt, it, lv1(0, 5), false, pity, 0);
      expect(it.empty());
      auto all_items = cfg;
      all_items.only_run_items = false;
      it = telepipe;
      expect(spire_drops_filter_item(all_items, *pmt, it, lv1(0, 5), false, pity, 0) == nullptr);
      expect(same(it, telepipe));
    }
    // Item 27: after PityDrops kept drops in a row without a usable weapon, the next one is a weapon; then it resets
    {
      auto pity3 = cfg;
      pity3.pity_drops = 3;
      uint32_t pity = 0;
      for (uint32_t n = 1; n <= 3; n++) {
        ItemData it = monomate;
        expect(spire_drops_filter_item(pity3, *pmt, it, lv1(0, 5), false, pity, n) == nullptr);
        expect(same(it, monomate) && (pity == n));
      }
      ItemData it = monomate;
      expect(spire_drops_filter_item(pity3, *pmt, it, lv1(0, 5), false, pity, 4) != nullptr);
      expect((spire_drops_weapon_fit(*pmt, it, lv1(0, 0)) == F::NOW) && (pity == 0));
      it = monomate;
      spire_drops_filter_item(pity3, *pmt, it, lv1(0, 5), false, pity, 5);
      expect(same(it, monomate) && (pity == 1));
      // nothing dropped is no drop; a usable weapon resets the counter too
      it = ItemData();
      spire_drops_filter_item(pity3, *pmt, it, lv1(0, 5), false, pity, 0);
      expect(it.empty() && (pity == 1));
      it = saber;
      spire_drops_filter_item(pity3, *pmt, it, player(0, 999, 999), false, pity, 0);
      expect(same(it, saber) && (pity == 0));
      // PityDrops 0: never
      pity3.pity_drops = 0;
      pity = 50;
      it = monomate;
      expect(spire_drops_filter_item(pity3, *pmt, it, lv1(0, 5), false, pity, 0) == nullptr);
      expect(same(it, monomate));
    }
    // Item 28: a boss gives each player exactly one weapon for his own class, also when Sega's table gave nothing
    {
      for (uint8_t cls : {0, 8}) { // HUmar and FOnewearl, the two players of one boss kill
        for (const auto& sega : {ItemData(), monomate, sword, telepipe}) {
          uint32_t pity = 2;
          ItemData it = sega;
          spire_drops_filter_item(cfg, *pmt, it, lv1(cls, 5), true, pity, cls);
          expect(!it.empty());
          expect_eq(F::NOW, spire_drops_weapon_fit(*pmt, it, lv1(cls, 0)));
          expect_eq(0u, pity);
        }
      }
      uint32_t pity = 2;
      ItemData it = saber; // Sega's own drop already fits: it stays
      expect(spire_drops_filter_item(cfg, *pmt, it, player(0, 999, 999), true, pity, 0) == nullptr);
      expect(same(it, saber) && (pity == 0));
      auto no_boss_rule = cfg;
      no_boss_rule.boss_weapon_per_player = false;
      it = ItemData();
      expect(spire_drops_filter_item(no_boss_rule, *pmt, it, lv1(0, 5), true, pity, 0) == nullptr);
      expect(it.empty());
    }
    // Item 28 on Vol Opt (k11): only phase 2 is the boss drop, not its monitors, pillars, amp, core or phase 1
    {
      expect(spire_drops_is_boss_drop(EnemyType::VOL_OPT_2));
      for (auto part : {EnemyType::VOL_OPT_1, EnemyType::VOL_OPT_AMP, EnemyType::VOL_OPT_CORE,
               EnemyType::VOL_OPT_MONITOR, EnemyType::VOL_OPT_PILLAR}) {
        expect(type_definition_for_enemy(part).is_boss()); // Sega's flag: a boss part
        expect(!spire_drops_is_boss_drop(part));
        uint32_t pity = 0;
        ItemData it = monomate; // a pillar's monomate stays a monomate, no weapon per player
        expect(spire_drops_filter_item(cfg, *pmt, it, lv1(0, 5), spire_drops_is_boss_drop(part), pity, 0) == nullptr);
        expect(same(it, monomate));
      }
      uint32_t pity = 0;
      ItemData it = monomate;
      spire_drops_filter_item(cfg, *pmt, it, lv1(0, 5), spire_drops_is_boss_drop(EnemyType::VOL_OPT_2), pity, 0);
      expect_eq(F::NOW, spire_drops_weapon_fit(*pmt, it, lv1(0, 0)));
      // Dragon and De Rol Le unchanged: a boss drop; a normal enemy is not
      expect(spire_drops_is_boss_drop(EnemyType::DRAGON));
      expect(spire_drops_is_boss_drop(EnemyType::DE_ROL_LE));
      expect(!spire_drops_is_boss_drop(EnemyType::BOOMA));
    }
  }

  phosg::log_info_f("-- Spire drops F8: boss and picked weapons carry grind and Hit (route B, plan section 6)");
  {
    auto pmt = ItemParameterTable::from_json(phosg::JSON::parse(phosg::load_file("system/tables/item-parameter-table-gc-v3.json")));
    JSONLevelTable level_table(phosg::JSON::parse(phosg::load_file("system/tables/level-table-v3.json")));
    auto lv1 = [&](uint8_t cls, uint32_t ahead) {
      SpireDropPlayer p;
      p.char_class = cls;
      p.now = level_table.base_stats_for_class(cls);
      p.ahead = spire_drops_stats_ahead(level_table, cls, 0, p.now, ahead);
      return p;
    };
    auto weapon = [](uint8_t group, uint8_t index) {
      ItemData ret;
      ret.data1[0] = 0x00;
      ret.data1[1] = group;
      ret.data1[2] = index;
      return ret;
    };
    // The Hit of a weapon (its attribute slot of type 5), or -1 if it has none; fails on a second slot of one type
    auto hit_of = [](const ItemData& w) -> int {
      int ret = -1;
      std::set<uint8_t> types;
      for (size_t z = 6; z <= 10; z += 2) {
        uint8_t type = w.data1[z];
        if (type == 0) {
          continue;
        }
        expect(types.emplace(type).second); // no duplicate attribute type
        if (type == SPIRE_WEAPON_ATTRIBUTE_HIT) {
          ret = static_cast<int8_t>(w.data1[z + 1]);
        }
      }
      return ret;
    };
    auto never_forbidden = [](const ItemData& it) {
      expect(it.data1[0] != 0x02); // never a mag
      expect(!spire_drop_is_forbidden(it)); // never a Scape Doll
    };

    // Config: missing keys keep the drops as before F8 (no grind, no Hit, LevelsAhead 5, PityDrops 8)
    auto old_cfg = SpireDropsConfig::from_json(phosg::JSON::parse("{}"));
    expect_eq(0u, old_cfg.boss_grind_base);
    expect_eq(0u, old_cfg.boss_grind_per_k);
    expect_eq(0u, old_cfg.boss_hit_base);
    expect_eq(0u, old_cfg.boss_hit_per_k);
    expect_eq(0u, old_cfg.pick_from_k);
    expect_eq(5u, old_cfg.levels_ahead);
    expect_eq(8u, old_cfg.pity_drops);
    for (uint32_t k = 0; k <= 15; k++) {
      expect_eq(0, spire_drops_boss_boost(old_cfg, k).grind);
      expect_eq(0, spire_drops_boss_boost(old_cfg, k).hit);
      expect_eq(0, spire_drops_pick_boost(old_cfg, k).grind);
      expect_eq(0, spire_drops_pick_boost(old_cfg, k).hit);
    }
    // The route B values (config.example.json SpireDrops)
    auto cfg = SpireDropsConfig::from_json(phosg::JSON::parse(
        "{\"DropMode\": \"SERVER_PRIVATE\", \"OnlyRunItems\": true, \"LevelsAhead\": 8, \"PityDrops\": 6, "
        "\"BossWeaponPerPlayer\": true, \"BossGrindBase\": 2, \"BossGrindPerK\": 2, \"BossHitBase\": 10, "
        "\"BossHitPerK\": 4, \"PickFromK\": 5}"));
    expect_eq(2u, cfg.boss_grind_base);
    expect_eq(2u, cfg.boss_grind_per_k);
    expect_eq(10u, cfg.boss_hit_base);
    expect_eq(4u, cfg.boss_hit_per_k);
    expect_eq(5u, cfg.pick_from_k);
    expect_eq(8u, cfg.levels_ahead);
    expect_eq(6u, cfg.pity_drops);
    for (const char* bad : {"{\"BossGrindBase\": -1}", "{\"BossHitPerK\": 1000}", "{\"PickFromK\": -2}"}) {
      bool threw = false;
      try {
        SpireDropsConfig::from_json(phosg::JSON::parse(bad));
      } catch (const std::runtime_error&) {
        threw = true;
      }
      expect(threw);
    }
    // The plan's boss table: Dragon k4 +10 26%, De Rol Le k8 +18 42%, Vol Opt k11 +24 50%, Dark Falz k15 +32 50%
    expect_eq(10, spire_drops_boss_boost(cfg, 4).grind);
    expect_eq(26, spire_drops_boss_boost(cfg, 4).hit);
    expect_eq(18, spire_drops_boss_boost(cfg, 8).grind);
    expect_eq(42, spire_drops_boss_boost(cfg, 8).hit);
    expect_eq(24, spire_drops_boss_boost(cfg, 11).grind);
    expect_eq(50, spire_drops_boss_boost(cfg, 11).hit);
    expect_eq(32, spire_drops_boss_boost(cfg, 15).grind);
    expect_eq(50, spire_drops_boss_boost(cfg, 15).hit);
    expect_eq(0, spire_drops_boss_boost(cfg, 0).grind); // room unknown: nothing
    expect_eq(0, spire_drops_boss_boost(cfg, 0).hit);
    // Picked weapons: nothing before k5, then grind 1 + k and Hit min(25, 5 + 2k)
    expect_eq(0, spire_drops_pick_boost(cfg, 4).grind);
    expect_eq(0, spire_drops_pick_boost(cfg, 4).hit);
    expect_eq(6, spire_drops_pick_boost(cfg, 5).grind);
    expect_eq(15, spire_drops_pick_boost(cfg, 5).hit);
    expect_eq(11, spire_drops_pick_boost(cfg, 10).grind);
    expect_eq(25, spire_drops_pick_boost(cfg, 10).hit);
    expect_eq(16, spire_drops_pick_boost(cfg, 15).grind);
    expect_eq(25, spire_drops_pick_boost(cfg, 15).hit);

    // Every common weapon of groups 01-0C, every index, k1-k15, boss and picked: grind = min(MaxGrind, formula), Hit =
    // the formula (at most 50, picked at most 25) in a slot of type 5, special byte 0, no duplicate attribute type
    size_t checked = 0;
    for (uint8_t group = 0x01; group <= 0x0C; group++) {
      size_t count = pmt->num_weapons_in_class(group);
      expect(count > 0);
      for (size_t index = 0; index < count; index++) {
        ItemData plain = weapon(group, index);
        if (pmt->is_item_rare(plain)) {
          continue;
        }
        uint8_t max_grind = pmt->get_weapon(group, index).max_grind;
        for (uint32_t k = 1; k <= 15; k++) {
          for (bool boss : {true, false}) {
            auto boost = boss ? spire_drops_boss_boost(cfg, k) : spire_drops_pick_boost(cfg, k);
            ItemData w = plain;
            bool changed = spire_drops_boost_weapon(*pmt, w, boost);
            expect_eq(0x00, w.data1[0]);
            expect_eq(group, w.data1[1]);
            expect_eq(static_cast<uint8_t>(index), w.data1[2]);
            expect_eq(std::min<uint32_t>(max_grind, boost.grind), w.data1[3]);
            expect(w.data1[3] <= max_grind);
            expect_eq(0x00, w.data1[4]); // special byte
            int hit = hit_of(w);
            expect(hit <= static_cast<int>(boss ? SPIRE_DROPS_BOSS_HIT_MAX : SPIRE_DROPS_PICK_HIT_MAX));
            expect_eq(boost.hit ? static_cast<int>(boost.hit) : -1, hit);
            expect_eq(changed, (w.data1[3] != 0) || (hit >= 0));
            if (boss) {
              expect(hit > 0); // every boss weapon from k1 on carries Hit
            }
            never_forbidden(w);
            checked++;
          }
        }
      }
    }
    expect(checked > 1000);

    // The boost never lowers what a weapon has, keeps its other attributes and fills its type-5 slot
    {
      ItemData w = weapon(0x01, 0x00); // Saber
      w.data1[3] = 30;
      w.data1[6] = 0x01; // Native 20%
      w.data1[7] = 20;
      w.data1[8] = SPIRE_WEAPON_ATTRIBUTE_HIT;
      w.data1[9] = 45;
      spire_drops_boost_weapon(*pmt, w, {10, 26});
      expect_eq(30, w.data1[3]);
      expect_eq(0x01, w.data1[6]);
      expect_eq(20, w.data1[7]);
      expect_eq(45, hit_of(w));
      w.data1[9] = 5;
      expect(spire_drops_boost_weapon(*pmt, w, {10, 26}));
      expect_eq(26, hit_of(w));
      expect_eq(SPIRE_WEAPON_ATTRIBUTE_HIT, w.data1[8]); // in its own slot, not a second one
      expect_eq(0x00, w.data1[10]);
      // Three attributes and none is Hit: no Hit, grind still
      ItemData full = weapon(0x01, 0x00);
      full.data1[6] = 1;
      full.data1[8] = 2;
      full.data1[10] = 3;
      full.data1[7] = full.data1[9] = full.data1[11] = 10;
      expect(spire_drops_boost_weapon(*pmt, full, {10, 26}));
      expect_eq(10, full.data1[3]);
      expect_eq(-1, hit_of(full));
      // An S-rank weapon's attribute bytes are its name: never a Hit there
      ItemData s_rank = weapon(0x70, 0x00);
      expect(s_rank.is_s_rank_weapon());
      s_rank.data1[6] = 0x00;
      s_rank.data1[8] = 0x12;
      spire_drops_boost_weapon(*pmt, s_rank, {10, 26});
      expect_eq(0x00, s_rank.data1[6]);
      expect_eq(0x00, s_rank.data1[7]);
      expect_eq(0x12, s_rank.data1[8]);
      // Not a weapon: unchanged
      ItemData mate;
      mate.data1[0] = 0x03;
      expect(!spire_drops_boost_weapon(*pmt, mate, {10, 26}));
      expect_eq(0x00, mate.data1[3]);
    }

    // Room k of a floor (the lab's SpireCheckpointRooms): the highest K of the floor
    {
      std::map<uint8_t, std::map<std::pair<uint8_t, int32_t>, uint8_t>> rooms;
      rooms[1] = {{{1, 7}, 2}, {{1, 10}, 2}, {{1, 11}, 1}, {{3, 50}, 5}, {{11, -1}, 4}, {{12, -1}, 8}, {{13, -1}, 11},
          {{14, -1}, 15}};
      rooms[2] = {{{14, -1}, 18}};
      expect_eq(2, spire_drops_floor_k(rooms, Episode::EP1, 1));
      expect_eq(5, spire_drops_floor_k(rooms, Episode::EP1, 3));
      expect_eq(4, spire_drops_floor_k(rooms, Episode::EP1, 11));
      expect_eq(8, spire_drops_floor_k(rooms, Episode::EP1, 12));
      expect_eq(11, spire_drops_floor_k(rooms, Episode::EP1, 13));
      expect_eq(15, spire_drops_floor_k(rooms, Episode::EP1, 14));
      expect_eq(18, spire_drops_floor_k(rooms, Episode::EP2, 14));
      expect_eq(0, spire_drops_floor_k(rooms, Episode::EP1, 0)); // Pioneer 2
      expect_eq(0, spire_drops_floor_k(rooms, Episode::EP1, 2)); // not listed
      expect_eq(0, spire_drops_floor_k(rooms, Episode::EP4, 1));
      // Critic M1: k comes from the floor the item drops on. A box on Caves 1 (floor 3) is room 5 for every receiver,
      // also for a partner whose own floor is still Pioneer 2 (0, which would give room 0)
      expect_eq(5, spire_drops_floor_k(rooms, Episode::EP1, 3));
      expect(spire_drops_boss_k_warning(true, spire_drops_floor_k(rooms, Episode::EP1, 14), Episode::EP1, 14).empty());
    }

    // Critic M2: a boss drop with room 0 (a live config without SpireCheckpointRooms) gives one warning line naming the
    // floor; a boss drop with a room and any other drop give none
    {
      std::map<uint8_t, std::map<std::pair<uint8_t, int32_t>, uint8_t>> none;
      uint8_t k = spire_drops_floor_k(none, Episode::EP1, 14);
      expect_eq(0, k);
      auto warning = spire_drops_boss_k_warning(true, k, Episode::EP1, 14);
      expect(warning.find("floor 0E") != std::string::npos);
      expect(warning.find("SpireCheckpointRooms") != std::string::npos);
      expect(warning.find('\n') == std::string::npos); // one line
      expect(spire_drops_boss_k_warning(false, 0, Episode::EP1, 14).empty()); // not a boss: k 0 is normal (Pioneer 2)
      expect(spire_drops_boss_k_warning(true, 15, Episode::EP1, 14).empty());
    }

    // In the filter: each player's boss weapon (Sega's drop or picked) carries the boss grind and Hit of room k, a
    // picked weapon the pick grind and Hit; never a mag, never a Scape Doll
    {
      ItemData monomate, mag, scape_doll, saber = weapon(0x01, 0x00), sword = weapon(0x02, 0x00);
      monomate.data1[0] = 0x03;
      mag.data1[0] = 0x02;
      scape_doll.data1[0] = 0x03;
      scape_doll.data1[1] = 0x09;
      for (uint8_t cls = 0; cls < 12; cls++) {
        for (uint32_t k = 1; k <= 15; k++) {
          for (const auto& sega : {ItemData(), monomate, mag, scape_doll, sword}) {
            uint32_t pity = 0;
            ItemData it = sega;
            expect(spire_drops_filter_item(cfg, *pmt, it, lv1(cls, 8), true, pity, k * 7 + cls, k) != nullptr);
            never_forbidden(it);
            expect(!it.empty());
            auto boost = spire_drops_boss_boost(cfg, k);
            expect_eq(std::min(pmt->get_weapon(it.data1[1], it.data1[2]).max_grind, boost.grind), it.data1[3]);
            expect_eq(static_cast<int>(boost.hit), hit_of(it));
            expect_eq(0x00, it.data1[4]);
          }
          // A replaced weapon (a class that cannot equip it) and a pity weapon: the pick grind and Hit from k5 on
          auto pick = spire_drops_pick_boost(cfg, k);
          uint32_t pity = 0;
          ItemData it = char_class_is_force(cls) ? sword : weapon(0x0A, 0x00); // Cane: for forces only
          expect(spire_drops_filter_item(cfg, *pmt, it, lv1(cls, 8), false, pity, k, k) != nullptr);
          never_forbidden(it);
          expect_eq(std::min(pmt->get_weapon(it.data1[1], it.data1[2]).max_grind, pick.grind), it.data1[3]);
          expect_eq(pick.hit ? static_cast<int>(pick.hit) : -1, hit_of(it));
          expect_eq(0x00, it.data1[4]);
          pity = cfg.pity_drops;
          it = monomate;
          expect(spire_drops_filter_item(cfg, *pmt, it, lv1(cls, 8), false, pity, k, k) != nullptr);
          expect_eq(pick.hit ? static_cast<int>(pick.hit) : -1, hit_of(it));
          // Not a boss and not picked: a mag or a Scape Doll is removed, never boosted into something
          for (const auto& bad : {mag, scape_doll}) {
            pity = 0;
            it = bad;
            spire_drops_filter_item(cfg, *pmt, it, lv1(cls, 8), false, pity, k, k);
            expect(it.empty());
          }
        }
      }
      // Sega's own boss drop that the player can equip: kept, now with the boss grind and Hit (Dragon, k4)
      uint32_t pity = 0;
      ItemData it = saber;
      SpireDropPlayer strong = lv1(0, 8);
      strong.now.atp = strong.ahead.atp = 999;
      expect(spire_drops_filter_item(cfg, *pmt, it, strong, true, pity, 0, 4) != nullptr);
      expect_eq(0x01, it.data1[1]);
      expect_eq(0x00, it.data1[2]);
      expect_eq(10, it.data1[3]);
      expect_eq(26, hit_of(it));
      // Missing keys: the boss weapon has no grind and no Hit, Sega's fitting drop stays as it is (before F8)
      it = saber;
      expect(spire_drops_filter_item(old_cfg, *pmt, it, strong, true, pity, 0, 8) == nullptr);
      expect_eq(0, it.data1[3]);
      expect_eq(-1, hit_of(it));
      it = ItemData();
      expect(spire_drops_filter_item(old_cfg, *pmt, it, lv1(0, 5), true, pity, 0, 8) != nullptr);
      expect_eq(0, it.data1[3]);
      expect_eq(-1, hit_of(it));
      // Room unknown (k 0): nothing added
      it = ItemData();
      spire_drops_filter_item(cfg, *pmt, it, lv1(0, 8), true, pity, 0, 0);
      expect_eq(0, it.data1[3]);
      expect_eq(-1, hit_of(it));
    }
  }

  phosg::log_info_f("-- Security: 6x69 with npc_entity_id >= 4 refused, not forwarded (regie pso-regie-10)");
  {
    const auto& rules = crash_value_rules();
    auto npc = [](uint16_t command, uint16_t param1, uint16_t param2) {
      G_NPCControl_6x69 cmd;
      cmd.header = {0x69, sizeof(G_NPCControl_6x69) / 4, 0};
      cmd.param1 = param1;
      cmd.param2 = param2;
      cmd.command = command;
      cmd.param3 = 13;
      return cmd;
    };
    auto check = [&](const G_NPCControl_6x69& cmd, uint32_t* value = nullptr) {
      return crash_value_rule_violated(rules, 0x69, &cmd, sizeof(cmd), value);
    };
    uint32_t value = 0;
    auto q903 = npc(0, 0, 13); // the q903 freeze: create follower NPC in "slot" 13
    expect_eq(0, check(q903, &value));
    expect_eq(13u, value);
    expect_eq("Security: 6x69 from C-1 refused: npc_entity_id 13 >= 4 (client crash value)",
        crash_value_refusal_message(rules[0], 1, value));
    expect_eq(0, check(npc(3, 0, 4))); // attacker NPC: the first bad slot
    expect_eq(0, check(npc(0, 0, 0xFFFF)));
    for (uint16_t slot = 0; slot < 4; slot++) {
      expect_eq(-1, check(npc(0, 0, slot))); // Sega's NPCs: slots 0-3 pass
      expect_eq(-1, check(npc(3, 0, slot)));
      expect_eq(-1, check(npc(1, slot, 0)));
    }
    expect_eq(1, check(npc(1, 13, 0), &value)); // stop acting: param1 is the slot
    expect_eq(13u, value);
    expect_eq(-1, check(npc(2, 13, 0))); // start acting: param1 is the player to follow, no rule
    expect_eq(-1, check(npc(0, 13, 1))); // create: param1 is the state, not the slot
    expect_eq(-1, crash_value_rule_violated(rules, 0x68, &q903, sizeof(q903), nullptr)); // another subcommand
    expect_eq(-1, crash_value_rule_violated(rules, 0x69, &q903, 8, nullptr)); // too short: the handler rejects it
    // Logged once per client per rule
    uint64_t logged = 0;
    expect(crash_value_should_log(logged, 0));
    expect(!crash_value_should_log(logged, 0));
    expect(crash_value_should_log(logged, 1));
    expect(!crash_value_should_log(logged, 1));
    uint64_t other_client = 0;
    expect(crash_value_should_log(other_client, 0));
    // Every rule in the table names its source and a reason
    for (const auto& rule : rules) {
      expect(rule.source && *rule.source && rule.reason && *rule.reason);
    }
  }

  phosg::log_info_f("-- Inventory backup: saved at game join, newest 20 kept, guard snapshot import (regie pso-regie-10)");
  {
    auto make_inventory = [](const std::vector<std::pair<uint64_t, uint64_t>>& codes, size_t equipped) {
      PlayerInventory inv;
      inv.language = Language::ENGLISH;
      inv.num_items = codes.size();
      for (size_t z = 0; z < codes.size(); z++) {
        inv.items[z].state = (z < equipped) ? 2 : 1;
        inv.items[z].flags = (z < equipped) ? 0x00000008 : 0;
        inv.items[z].data = ItemData(codes[z].first, codes[z].second);
        inv.items[z].data.id = 0x00010000 + z;
      }
      return inv;
    };
    auto make_disp = [](const char* name, uint32_t level, uint32_t meseta) {
      PlayerDispDataV4 disp;
      disp.visual.name.encode(name, Language::ENGLISH);
      disp.stats.level = level - 1;
      disp.stats.meseta = meseta;
      return disp;
    };
    // The test character BidX on the Wii serial that also carries Wakiru (never Wakiru himself in a test)
    InventoryBackup b;
    b.serial_number = 3117539659;
    b.disp = make_disp("BidX", 12, 300);
    b.character_id = spire_guard_character_id(b.disp);
    b.inventory = make_inventory({{0x0001000000000000, 0}, {0x0300000000030000, 0}}, 1); // Saber (equipped), Monomate x3
    std::string root = std::format("{}/newserv-inventory-backup-test-{}",
        std::filesystem::temp_directory_path().string(), getpid());
    std::filesystem::remove_all(root);
    std::string dir = inventory_backup_character_directory(root, b.serial_number, b.disp);
    expect_eq(std::format("{}/3117539659.{}", root, b.character_id), dir);
    expect(dir != inventory_backup_character_directory(root, b.serial_number, make_disp("Wakiru", 140, 0)));
    expect(inventory_backup_basename_for_time(1000000) < inventory_backup_basename_for_time(2000000));
    expect_eq("20261025-120741-000005.json", inventory_backup_basename_for_time(1792930061000005ULL));

    // Save 22 joins: the newest 20 stay, the two oldest go
    std::vector<std::string> written;
    for (size_t z = 0; z < 22; z++) {
      b.time_usecs = 1792930061000000ULL + z * 1000000;
      b.disp.stats.meseta = 300 + z;
      written.emplace_back(inventory_backup_save(dir, b, INVENTORY_BACKUP_KEEP));
    }
    auto names = inventory_backup_list(dir);
    expect_eq(20u, names.size());
    expect(!std::filesystem::exists(written[0]) && !std::filesystem::exists(written[1]));
    expect(std::filesystem::exists(written[2]) && std::filesystem::exists(written[21]));
    expect_eq(std::filesystem::path(written[21]).filename().string(), names.back());
    expect_eq(2u, inventory_backup_files_to_delete(std::vector<std::string>(22, "x"), 20).size());
    expect_eq(0u, inventory_backup_files_to_delete(std::vector<std::string>(20, "x"), 20).size());
    // Items, meseta, level and name come back; the file is only written, never restored by itself
    auto loaded = inventory_backup_from_json(phosg::JSON::parse(phosg::load_file(written[21])));
    expect_eq(3117539659u, loaded.serial_number);
    expect_eq(b.character_id, loaded.character_id);
    expect_eq(321u, loaded.disp.stats.meseta.load());
    expect_eq(11u, loaded.disp.stats.level.load());
    expect_eq("BidX", loaded.disp.visual.name.decode(Language::ENGLISH));
    expect_eq(2u, loaded.inventory.num_items);
    expect(!memcmp(&loaded.inventory, &b.inventory, sizeof(b.inventory)));
    expect(!loaded.restored && !loaded.left_normally && !loaded.has_exit && !loaded.spire);
    auto json = phosg::JSON::parse(phosg::load_file(written[21]));
    expect_eq("000100000000000000000000 00000000", json.at("Items").at(0).get_string("Code"));
    expect(json.at("Items").at(0).get_bool("Equipped"));
    // The end of the game: Exit (dropped out) and LeftNormally (98) survive a round trip
    loaded.has_exit = true;
    loaded.exit_inventory = make_inventory({{0x0001000000000000, 0}}, 1);
    loaded.exit_meseta = 50;
    inventory_backup_write_file(written[21], loaded);
    auto ended = inventory_backup_from_json(phosg::JSON::parse(phosg::load_file(written[21])));
    expect(ended.has_exit && (ended.exit_meseta == 50) && (ended.exit_inventory.num_items == 1) && !ended.left_normally);
    expect_eq(20u, inventory_backup_list(dir).size()); // rewriting the end does not add a file

    // spire-backup-import: a guard snapshot (SerialNumber, Inventory, Disp hex) becomes a restorable backup
    std::string snapshot_filename = root + "/marker.bak";
    phosg::save_file(snapshot_filename, phosg::JSON::dict({
        {"SerialNumber", 3117539659u},
        {"SnapshotTime", 1792929999000000ULL},
        {"Inventory", spire_guard_hex_encode(&b.inventory, sizeof(b.inventory))},
        {"Disp", spire_guard_hex_encode(&b.disp, sizeof(b.disp))},
    }).serialize());
    std::string imported_path = inventory_backup_import_snapshot(snapshot_filename, root, 1792940000000000ULL);
    expect_eq(dir, std::filesystem::path(imported_path).parent_path().string());
    auto imported = inventory_backup_from_json(phosg::JSON::parse(phosg::load_file(imported_path)));
    expect(!imported.imported_from.empty() && !imported.restored && !imported.has_exit);
    expect(!memcmp(&imported.inventory, &b.inventory, sizeof(b.inventory)));
    expect_eq(b.character_id, imported.character_id);
    expect_eq(std::filesystem::path(imported_path).filename().string(), inventory_backup_list(dir).back()); // newest
    std::filesystem::remove_all(root);
  }

  phosg::log_info_f("-- $restore: own character, no Spire run, once per backup, anti-dup, Wakiru allowed, check (regie pso-regie-10)");
  {
    using R = InventoryRestoreRefusal;
    auto code = [](const ItemData& i) {
      return std::format("{:08X}{:08X}{:08X} {:08X}", i.data1db[0], i.data1db[1], i.data1db[2], i.data2db);
    };
    auto parse = [](const std::string& c) {
      // "<24 hex> <8 hex>", the $item form of item-herstel-2026-10-25.md
      uint64_t first = std::stoull(c.substr(0, 16), nullptr, 16);
      uint64_t second = (std::stoull(c.substr(16, 8), nullptr, 16) << 32) | std::stoull(c.substr(25, 8), nullptr, 16);
      return ItemData(first, second);
    };
    auto inventory_of = [&](const std::vector<std::string>& codes, size_t equipped) {
      PlayerInventory inv;
      inv.language = Language::ENGLISH;
      inv.num_items = codes.size();
      for (size_t z = 0; z < codes.size(); z++) {
        inv.items[z].state = (z < equipped) ? 2 : 1;
        inv.items[z].flags = (z < equipped) ? 0x00000008 : 0;
        inv.items[z].data = parse(codes[z]);
        inv.items[z].data.id = 0x00210000 + z;
      }
      return inv;
    };
    // The 13:35 case (pso-roguelike new-features/roguelike/output/item-herstel-2026-10-25.md): the 13:34:52 dump had 21
    // items and 1350 Meseta; after the freeze only the 7 equipped ones were left and 0 Meseta. The 7 equipped codes are
    // synthetic (the doc names them, not their bytes); the 14 lost ones are the doc's codes.
    const std::vector<std::string> equipped = {
        "000A05000000000000000000 00000000", // Ila (synthetic)
        "010128000000000000000000 00000000", // Smoking Plate (synthetic)
        "010228000000000000000000 00000000", // Tripolic Shield (synthetic)
        "01030F000000000000000000 00000000", // God/Mind (synthetic)
        "010305000000000000000000 00000000", // God/HP (synthetic)
        "010305000000000000000000 00000000", // God/HP (synthetic)
        "010336000000000000000000 00000000", // TP/Revival (synthetic)
    };
    const std::vector<std::string> lost = {
        "002200000000021900000000 00000000", // Caduceus 0/25/0/0
        "0101290000042C0005000000 00000000", // Guard Wave (4 slots) +44DEF +5EVP
        "01020C000000020004000000 00000000", // General Shield +2DEF +4EVP
        "010335000000000000000000 00000000", // HP/Revival
        "02000500F401000000000000 2800000A", // Mag LV5
        "030000000001000000000000 00000000", // Monomate x1
        "030001000001000000000000 00000000", // Dimate x1
        "030100000003000000000000 00000000", // Monofluid x3
        "03010200000A000000000000 00000000", // Trifluid x10
        "030300000002000000000000 00000000", // Sol Atomizer x2
        "030400000001000000000000 00000000", // Moon Atomizer x1
        "030700000001000000000000 00000000", // Telepipe x1
        "030E04000000000000000000 00000000", // Syncesta
        "031000000004000000000000 00000000", // Photon Drop x4
    };
    std::vector<std::string> all = equipped;
    all.insert(all.end(), lost.begin(), lost.end());
    InventoryBackup snap; // what spire-backup-import makes of the 13:07 marker
    snap.serial_number = 3117539659;
    snap.disp.visual.name.encode("Wakiru", Language::ENGLISH);
    snap.disp.stats.level = 139;
    snap.disp.stats.meseta = 1350;
    snap.character_id = spire_guard_character_id(snap.disp);
    snap.inventory = inventory_of(all, equipped.size());
    snap.imported_from = "wakiru-marker-13-07.bak";
    expect_eq(21u, snap.inventory.num_items);
    auto after_freeze = inventory_of(equipped, equipped.size());
    InventoryRemovals none;

    // $restore check lists exactly the 14 documented items and the 1350 Meseta
    auto plan = inventory_restore_missing(snap.inventory, 1350, after_freeze, 0, none);
    expect_eq(lost.size(), plan.items.size());
    for (size_t z = 0; z < lost.size(); z++) {
      expect_eq(lost[z], code(plan.items[z]));
      expect_eq(0xFFFFFFFFu, plan.items[z].id.load()); // fresh ids come from the game at drop time
    }
    expect_eq(1350u, plan.meseta);

    // Wakiru (protected) is allowed; the protected-character cheat gate does not stop $restore either
    expect_eq(R::NONE, inventory_restore_refusal(true, false, true, 3117539659, snap.character_id, &snap));
    expect(!DataIndex::spire_cheat_refused("$restore", true));

    // Check mode: nothing to drop, nothing marked, a real $restore still possible afterwards
    expect(inventory_restore_take(snap, plan, true, 123).empty());
    expect(!snap.restored && (snap.restored_time_usecs == 0));
    expect_eq(R::NONE, inventory_restore_refusal(true, false, true, 3117539659, snap.character_id, &snap));
    // Once per backup: the first $restore marks it, the second is refused and drops nothing
    auto taken = inventory_restore_take(snap, plan, false, 456);
    expect_eq(lost.size(), taken.items.size());
    expect(snap.restored && (snap.restored_time_usecs == 456));
    auto json_after = inventory_backup_to_json(snap);
    expect(inventory_backup_from_json(json_after).restored); // the mark is in the file
    expect_eq(R::ALREADY_RESTORED, inventory_restore_refusal(true, false, true, 3117539659, snap.character_id, &snap));
    snap.restored = false;

    // Own character only: a backup of another serial or another character on the same card is refused
    expect_eq(R::OTHER_CHARACTER, inventory_restore_refusal(true, false, false, 3117539666, snap.character_id, &snap));
    PlayerDispDataV4 bidx;
    bidx.visual.name.encode("BidX", Language::ENGLISH);
    expect_eq(R::OTHER_CHARACTER,
        inventory_restore_refusal(true, false, false, 3117539659, spire_guard_character_id(bidx), &snap));
    // Spire run refused, and outside a game (the drop needs a game)
    expect_eq(R::SPIRE_GAME, inventory_restore_refusal(true, true, false, 3117539659, snap.character_id, &snap));
    expect_eq(R::NOT_IN_GAME, inventory_restore_refusal(false, false, false, 3117539659, snap.character_id, &snap));
    expect_eq(R::NO_BACKUP, inventory_restore_refusal(true, false, false, 3117539659, snap.character_id, nullptr));
    // Only a game that did not end with a 98: LeftNormally refused, Exit or an imported snapshot allowed, neither refused
    InventoryBackup b = snap;
    b.imported_from.clear();
    expect_eq(R::UNKNOWN_END, inventory_restore_refusal(true, false, false, 3117539659, b.character_id, &b));
    b.has_exit = true;
    expect_eq(R::NONE, inventory_restore_refusal(true, false, false, 3117539659, b.character_id, &b));
    b.left_normally = true;
    expect_eq(R::LEFT_NORMALLY, inventory_restore_refusal(true, false, false, 3117539659, b.character_id, &b));
    b.left_normally = false;
    b.spire = true;
    expect_eq(R::SPIRE_BACKUP, inventory_restore_refusal(true, false, false, 3117539659, b.character_id, &b));

    // Anti-dup: what the player dropped, traded, sold or banked since the current game began is not missing, and
    // neither is what he already has again (a ground Guard Wave, a fed mag, a Syncesta made with $item)
    InventoryRemovals removed;
    inventory_removals_add(removed, parse("002200000000021900000000 00000000")); // dropped the Caduceus (6x2A)
    inventory_removals_add(removed, parse("030102000003000000000000 00000000")); // sold 3 Trifluid (6x29)
    ItemData meseta_drop;
    meseta_drop.data1[0] = 0x04;
    meseta_drop.data2d = 350;
    inventory_removals_add(removed, meseta_drop); // dropped 350 Meseta
    auto now_has = equipped;
    now_has.emplace_back("0101290300042C0005000000 00000000"); // Guard Wave, other bytes (ground)
    now_has.emplace_back("02000600F401000000000000 2800000A"); // the mag, fed to LV6
    now_has.emplace_back("030E04000000000000000000 00000000"); // Syncesta
    auto dup_plan = inventory_restore_missing(snap.inventory, 1350, inventory_of(now_has, equipped.size()), 0, removed);
    std::vector<std::string> dup_codes;
    for (const auto& it : dup_plan.items) {
      dup_codes.emplace_back(code(it));
    }
    expect_eq(10u, dup_codes.size());
    for (const char* gone : {"002200000000021900000000 00000000", "0101290000042C0005000000 00000000",
             "02000500F401000000000000 2800000A", "030E04000000000000000000 00000000"}) {
      expect(std::find(dup_codes.begin(), dup_codes.end(), gone) == dup_codes.end());
    }
    expect(std::find(dup_codes.begin(), dup_codes.end(), "030102000007000000000000 00000000") != dup_codes.end());
    expect_eq(1000u, dup_plan.meseta);
    // Nothing missing: nothing to drop
    expect(inventory_restore_missing(snap.inventory, 1350, snap.inventory, 1350, none).empty());

    // The previous backup is the newest one that is not the current game's own
    std::vector<std::string> names = {"20261025-120000-000000.json", "20261025-130741-000000.json", "20261025-150000-000000.json"};
    expect_eq(1, inventory_backup_pick_previous(names, "20261025-150000-000000.json"));
    expect_eq(2, inventory_backup_pick_previous(names, ""));
    expect_eq(-1, inventory_backup_pick_previous({"20261025-150000-000000.json"}, "20261025-150000-000000.json"));

    phosg::log_info_f("-- Inventory backup: $restore picks an unrestored import over later join backups (regie pso-regie-10)");
    using F = InventoryBackupPickFlags;
    std::vector<std::string> pick_names = {"20261025-120000-000000.json", "20261025-130000-000000.json",
        "20261025-140000-000000.json", "20261025-150000-000000.json", "20261025-160000-000000.json"};
    const std::string current_game = "20261025-160000-000000.json";
    // Import (index 1), then two later join backups and the current game: the import wins
    expect_eq(1, inventory_backup_pick_restore(pick_names, {F{}, F{true, false}, F{}, F{}, F{}}, current_game));
    // The import is restored: the normal rule again (newest that is not the current game)
    expect_eq(3, inventory_backup_pick_restore(pick_names, {F{}, F{true, true}, F{}, F{}, F{}}, current_game));
    // Two unrestored imports: the newest import
    expect_eq(2, inventory_backup_pick_restore(pick_names, {F{true, false}, F{}, F{true, false}, F{}, F{}}, current_game));
    // A newer import already restored: the newest unrestored import before it, not the later join backup
    expect_eq(2, inventory_backup_pick_restore(pick_names, {F{true, false}, F{}, F{true, false}, F{true, true}, F{}}, current_game));
    // The current game's own file is never picked, also not when it is an unrestored import
    expect_eq(1, inventory_backup_pick_restore(pick_names, {F{}, F{true, false}, F{}, F{}, F{true, false}}, current_game));
    expect_eq(3, inventory_backup_pick_restore(pick_names, {F{}, F{}, F{}, F{}, F{true, false}}, current_game));
    expect_eq(-1, inventory_backup_pick_restore({current_game}, {F{true, false}}, current_game));
    // No import: the old behaviour
    expect_eq(3, inventory_backup_pick_restore(pick_names, {F{}, F{}, F{}, F{}, F{}}, current_game));
    expect_eq(4, inventory_backup_pick_restore(pick_names, {F{}, F{}, F{}, F{}, F{}}, ""));
    expect_eq(-1, inventory_backup_pick_restore({}, {}, current_game));
  }

  phosg::log_info_f("-- Inventory backup: hard ends (register item karakter-verlies-battle, 2026-10-25)");
  {
    using V = InventoryLossVerdict;
    using F = InventoryBackupPickFlags;
    auto inv = [](const std::vector<uint64_t>& codes, size_t equipped) {
      PlayerInventory i;
      i.language = Language::ENGLISH;
      i.num_items = codes.size();
      for (size_t z = 0; z < codes.size(); z++) {
        i.items[z].state = (z < equipped) ? 2 : 1;
        i.items[z].flags = (z < equipped) ? 0x00000008 : 0;
        i.items[z].data = ItemData(codes[z], 0);
        i.items[z].data.id = 0x00010000 + z;
      }
      return i;
    };
    // Rule b (Exit at every hard end): a disconnect or a server stop records the server's view as Exit; a 98 only
    // LeftNormally. The game is a Spire backup only if the guard took its snapshot (q925 and a Battle game without a
    // quest stay restorable)
    InventoryBackup b;
    b.serial_number = 3117539659;
    b.disp.visual.name.encode("BidX", Language::ENGLISH);
    b.character_id = spire_guard_character_id(b.disp);
    b.quest_number = 925;
    b.inventory = inv({0x0001000000000000, 0x0101000000000000, 0x0034001A00000000, 0x0300000000030000}, 2);
    b.disp.stats.meseta = 1350;
    InventoryBackup hard = b;
    auto exit_inv = inv({0x0001000000000000, 0x0101000000000000, 0x0034001A00000000, 0x0300000000030000}, 2);
    inventory_backup_record_end(hard, false, false, &exit_inv, 1350);
    expect(hard.has_exit && !hard.left_normally && !hard.spire);
    expect_eq(4u, hard.exit_inventory.num_items);
    expect_eq(1350u, hard.exit_meseta);
    InventoryBackup clean = b;
    inventory_backup_record_end(clean, true, false, nullptr, 0);
    expect(clean.left_normally && !clean.has_exit && !clean.spire);
    InventoryBackup run = b;
    inventory_backup_record_end(run, false, true, &exit_inv, 1350);
    expect(run.spire && run.has_exit);
    // Rule c ($restore after a Spire hard end): refused only where the guard restores
    expect(inventory_restore_refusal(true, false, false, b.serial_number, b.character_id, &hard) == InventoryRestoreRefusal::NONE);
    expect(inventory_restore_refusal(true, false, false, b.serial_number, b.character_id, &run) == InventoryRestoreRefusal::SPIRE_BACKUP);

    // Rule a (next login): Sega's penalty leaves only the equipped items and no meseta
    auto after = inv({0x0001000000000000, 0x0101000000000000}, 2);
    auto plan = inventory_restore_missing(hard.exit_inventory, hard.exit_meseta, after, 0, InventoryRemovals());
    expect_eq(2u, plan.items.size());
    expect_eq(1350u, plan.meseta);
    expect(inventory_loss_verdict(hard, false, plan.items.size(), plan.meseta) == V::RESTORABLE);
    expect(inventory_loss_verdict(clean, false, plan.items.size(), plan.meseta) == V::NONE); // a 98: the card saved
    expect(inventory_loss_verdict(run, false, plan.items.size(), plan.meseta) == V::NONE); // the guard restores it
    expect(inventory_loss_verdict(hard, false, 0, 0) == V::NONE); // nothing missing
    InventoryBackup done = hard;
    done.restored = true;
    expect(inventory_loss_verdict(done, false, 2, 0) == V::NONE);
    // No end at all (crash or SIGKILL): not restorable, unless the older connection is still in that game
    expect(inventory_loss_verdict(b, false, 2, 0) == V::SERVER_STOP);
    expect(inventory_loss_verdict(b, true, 2, 0) == V::RESTORABLE);
    std::string text = inventory_loss_text(V::RESTORABLE, 2, 1350);
    expect(text.find("2 items en 1350 Meseta") != std::string::npos);
    expect(text.find("%srestore in een gewone game") != std::string::npos); // add_color turns %s into a literal $
    expect(add_color(text).find("$restore") != std::string::npos);
    expect(inventory_loss_text(V::SERVER_STOP, 1, 0).find("1 item bij") != std::string::npos);
    expect(inventory_loss_text(V::NONE, 2, 0).empty());

    // Rule d (retention): a loss-marked or imported backup that is not restored never rotates out and wins the pick
    std::vector<std::string> names;
    for (size_t z = 0; z < 25; z++) {
      names.emplace_back(std::format("20261025-{:02}0000-000000.json", z));
    }
    std::vector<F> flags(25);
    flags[0].loss = true;
    auto del = inventory_backup_files_to_delete(names, 20, flags);
    expect_eq(4u, del.size());
    expect(std::find(del.begin(), del.end(), names[0]) == del.end());
    expect_eq(names[1], del.front());
    expect_eq(names[4], del.back());
    flags[0].restored = true; // once restored it rotates like any other
    expect_eq(5u, inventory_backup_files_to_delete(names, 20, flags).size());
    expect_eq(5u, inventory_backup_files_to_delete(names, 20).size());
    expect(inventory_backup_pinned(F{false, false, true}) && inventory_backup_pinned(F{true, false, false}));
    expect(!inventory_backup_pinned(F{false, true, true}) && !inventory_backup_pinned(F{}));
    expect_eq(1, inventory_backup_pick_restore({names[0], names[1], names[2]}, {F{}, F{false, false, true}, F{}}, names[2]));
    // On disk: a reconnect loop of 30 joins after the loss keeps the loss-marked backup
    std::string root = std::format("{}/newserv-inventory-loss-test-{}", std::filesystem::temp_directory_path().string(), getpid());
    std::filesystem::remove_all(root);
    std::string dir = inventory_backup_character_directory(root, b.serial_number, b.disp);
    InventoryBackup marked = hard;
    marked.time_usecs = 1792930061000000ULL;
    marked.loss_time_usecs = 1792930100000000ULL;
    marked.loss_items = 2;
    marked.loss_meseta = 1350;
    std::string marked_path = inventory_backup_save(dir, marked, INVENTORY_BACKUP_KEEP);
    auto reloaded = inventory_backup_from_json(phosg::JSON::parse(phosg::load_file(marked_path)));
    expect_eq(marked.loss_time_usecs, reloaded.loss_time_usecs);
    expect_eq(2u, reloaded.loss_items);
    expect_eq(1350u, reloaded.loss_meseta);
    for (size_t z = 1; z <= 30; z++) {
      InventoryBackup join = b;
      join.time_usecs = marked.time_usecs + z * 1000000;
      inventory_backup_save(dir, join, INVENTORY_BACKUP_KEEP);
    }
    expect(std::filesystem::exists(marked_path));
    expect_eq(21u, inventory_backup_list(dir).size());
    std::filesystem::remove_all(root);
  }

  phosg::log_info_f("-- $infhp revive on GC: sent with EnableV3V4ProtectedSubcommands off (regie pso-regie-10)");
  {
    expect(protected_command_allowed_v3_v4(false, true, true, true)); // the $infhp revive, config off
    expect(!protected_command_allowed_v3_v4(false, false, true, true)); // other protected commands keep the config
    expect(protected_command_allowed_v3_v4(true, false, true, true));
    expect(!protected_command_allowed_v3_v4(false, true, false, true)); // no function calls: nothing to send
    expect(!protected_command_allowed_v3_v4(true, true, true, false));
  }

  phosg::log_info_f("-- Boss lookup on a Spire Run F1 floor order: Dragon index and floor/type match (2026-10-25)");
  {
    // q921 as deployed on 2026-10-25 had two Pan Arms in Caves 2 (floor 04), each 3 server entries; the client
    // builds no Pan Arms there, so its Dragon index was 4 lower than the server's and 6x12 hit the wrong enemy
    using ESE = MapFile::EnemySetEntry;
    auto make_set = [](uint16_t base_type, uint8_t floor) {
      ESE e;
      e.base_type = base_type;
      e.floor = floor;
      e.room = 1;
      e.wave_number = 1;
      return e;
    };
    auto dat = std::make_shared<std::string>();
    auto add_section = [&](uint8_t floor, const std::vector<ESE>& sets) {
      MapFile::SectionHeader h;
      h.le_type = static_cast<uint32_t>(MapFile::SectionHeader::Type::ENEMY_SETS);
      h.data_size = sets.size() * sizeof(ESE);
      h.section_size = sizeof(h) + h.data_size;
      h.floor = floor;
      dat->append(reinterpret_cast<const char*>(&h), sizeof(h));
      dat->append(reinterpret_cast<const char*>(sets.data()), h.data_size);
    };
    add_section(0x04, {make_set(0x0065, 0x04), make_set(0x0065, 0x04), make_set(0x0060, 0x04)}); // Pan Arms x2, Grass
    add_section(0x0B, {make_set(0x00C0, 0x0B)}); // Dragon
    add_section(0x0C, {make_set(0x00C1, 0x0C)}); // De Rol Le (+10 body, +9 mines)
    add_section(0x0E, {make_set(0x00C8, 0x0E)}); // Dark Falz (root DF3, darvants, DF3/DF2/DF1 aliases)
    add_section(0x11, {make_set(0x0044, 0x11)}); // an enemy on the last floor
    MapFile::SectionHeader end_h;
    end_h.le_type = 0;
    end_h.section_size = 0;
    end_h.floor = 0;
    end_h.data_size = 0;
    dat->append(reinterpret_cast<const char*>(&end_h), sizeof(end_h));

    std::array<std::shared_ptr<const MapFile>, NUM_VERSIONS> map_files;
    map_files.at(static_cast<size_t>(Version::GC_V3)) = std::make_shared<MapFile>(dat);
    auto supermap = std::make_shared<SuperMap>(map_files, SetDataTableBase::default_floor_to_area(Version::GC_V3, Episode::EP1));
    auto rand_crypt = std::make_shared<MT19937Generator>(0);
    auto map_state = std::make_shared<MapState>(
        0, Difficulty::NORMAL, 0, 0, MapState::DEFAULT_RARE_ENEMIES, rand_crypt, supermap);

    // Server view: Pan Arms 3 + 3, Grass 1, so the Dragon is index 7 (on the client, which builds no Pan Arms in
    // Caves 2, it was index 3: the 6x12 of 2026-10-25 21:00 came in with that index)
    expect_eq(EnemyType::DRAGON, map_state->enemy_state_for_index(Version::GC_V3, 7)->super_ene->type);
    expect_eq(EnemyType::PAN_ARMS, map_state->enemy_state_for_index(Version::GC_V3, 3)->super_ene->type);

    auto lookup = [&](uint8_t floor, EnemyType type) {
      return map_state->enemy_state_for_floor_type(Version::GC_V3, floor, type);
    };
    auto dragon = lookup(0x0B, EnemyType::DRAGON);
    expect_eq(EnemyType::DRAGON, dragon->super_ene->type);
    expect_eq(7, map_state->index_for_enemy_state(Version::GC_V3, dragon));
    // floor and type must both match: no De Rol Le on the Dragon floor (was: the floor's first enemy, the Dragon)
    expect_raises(std::out_of_range, [&]() { lookup(0x0B, EnemyType::DE_ROL_LE); });
    expect_eq(EnemyType::DE_ROL_LE, lookup(0x0C, EnemyType::DE_ROL_LE)->super_ene->type);
    expect_eq(EnemyType::DE_ROL_LE_MINE, lookup(0x0C, EnemyType::DE_ROL_LE_MINE)->super_ene->type);
    // Dark Falz phases: DF2 is an alias of the root DF3, so the final boss drop still resolves to the root
    auto df2 = lookup(0x0E, EnemyType::DARK_FALZ_2);
    expect_eq(EnemyType::DARK_FALZ_2, df2->super_ene->type);
    expect(df2->alias_target_ene_st != nullptr);
    expect_eq(EnemyType::DARK_FALZ_3, df2->alias_target_ene_st->super_ene->type);
    expect_eq(EnemyType::DARK_FALZ_3, lookup(0x0E, EnemyType::DARK_FALZ_3)->super_ene->type);
    // last floor: the search runs to the end of the enemy list (was: stopped at index 0x12)
    expect_eq(EnemyType::BOOMA, lookup(0x11, EnemyType::BOOMA)->super_ene->type);
  }

  phosg::log_info_f("-- Entity id past the last enemy: forwarded unchanged, never a disconnect (Spire sweep 2026-10-25)");
  {
    // forward_subcommand_with_entity_id_transcode_t (6x76 and 25 more subcommands) looked the id up with
    // enemy_state_for_index, whose .at() throws past the last enemy; the throw disconnected the sender
    using ESE = MapFile::EnemySetEntry;
    std::vector<ESE> sets(2);
    for (auto& e : sets) {
      e.base_type = 0x0044; // Booma
      e.floor = 0x01;
      e.room = 1;
      e.wave_number = 1;
    }
    auto dat = std::make_shared<std::string>();
    MapFile::SectionHeader h;
    h.le_type = static_cast<uint32_t>(MapFile::SectionHeader::Type::ENEMY_SETS);
    h.data_size = sets.size() * sizeof(ESE);
    h.section_size = sizeof(h) + h.data_size;
    h.floor = 0x01;
    dat->append(reinterpret_cast<const char*>(&h), sizeof(h));
    dat->append(reinterpret_cast<const char*>(sets.data()), h.data_size);
    MapFile::SectionHeader end_h;
    end_h.le_type = 0;
    end_h.section_size = 0;
    end_h.floor = 0;
    end_h.data_size = 0;
    dat->append(reinterpret_cast<const char*>(&end_h), sizeof(end_h));
    std::array<std::shared_ptr<const MapFile>, NUM_VERSIONS> map_files;
    map_files.at(static_cast<size_t>(Version::GC_V3)) = std::make_shared<MapFile>(dat);
    auto supermap = std::make_shared<SuperMap>(map_files, SetDataTableBase::default_floor_to_area(Version::GC_V3, Episode::EP1));
    auto map_state = std::make_shared<MapState>(
        0, Difficulty::NORMAL, 0, 0, MapState::DEFAULT_RARE_ENEMIES, std::make_shared<MT19937Generator>(0), supermap);

    // the bug's condition: the lookup the forwarder used throws for the index after the last enemy
    expect_raises(std::out_of_range, [&]() { map_state->enemy_state_for_index(Version::GC_V3, 2); });

    std::shared_ptr<const MapState::EnemyState> ene_st;
    std::shared_ptr<const MapState::ObjectState> obj_st;
    expect_eq("", entity_states_for_forwarding(map_state, Version::GC_V3, 0x1001, ene_st, obj_st));
    expect(ene_st != nullptr);
    expect_eq(EnemyType::BOOMA, ene_st->super_ene->type);
    expect(obj_st == nullptr);
    expect(!entity_states_for_forwarding(map_state, Version::GC_V3, 0x1002, ene_st, obj_st).empty());
    expect(ene_st == nullptr);
    expect(obj_st == nullptr);
    expect(!entity_states_for_forwarding(map_state, Version::GC_V3, 0x1FFF, ene_st, obj_st).empty());
    expect(ene_st == nullptr);
    // a player id (below 0x1000) and 0xFFFF name no entity: no lookup, no error
    expect_eq("", entity_states_for_forwarding(map_state, Version::GC_V3, 0x0001, ene_st, obj_st));
    expect(ene_st == nullptr);
    expect(obj_st == nullptr);
    expect_eq("", entity_states_for_forwarding(map_state, Version::GC_V3, 0xFFFF, ene_st, obj_st));
    expect(ene_st == nullptr);
    expect(obj_st == nullptr);
  }

  phosg::log_info_f("-- Spire run mag: step, bytes, merge, refusals, idempotence (F6, route B)");
  {
    auto f6_case = [](const char* name, const std::function<void()>& fn) {
      phosg::log_info_f("   F6 ({})", name);
      fn();
    };
    using Bytes = SpireMagBytes;
    auto word = [](const Bytes& b, size_t which) -> uint16_t { return b[4 + which * 2] | (b[5 + which * 2] << 8); };
    auto table = spire_mag_parse(phosg::load_file(SPIRE_RUN_MAG_FILE));
    const auto& hu = table.classes.at("HU");

    f6_case("every row of the shipped spire-mag.json", [&]() {
      auto names = phosg::JSON::parse(phosg::load_file("system/tables/names-v3.json"));
      expect(table.enabled);
      expect_eq(3u, table.classes.size());
      for (const char* cls : {"HU", "RA", "FO"}) {
        const auto& full = table.classes.at(cls);
        // F10: the Shell column is shipped for every class, as long as Steps, and keeps the same rules (randgevallen 4-5)
        expect_eq(std::string(""), full.shell_note);
        expect_eq(full.steps.size(), full.shell_steps.size());
        const auto shell = spire_mag_column(full, SPIRE_RUN_MAG_LEAN_SHELL);
        expect(shell.shell && !shell.fell_back);
        // fix round (F10S critic N3): step 0 equal, and from step 1 DEF swapped with the main stat (POW for HU/RA,
        // MIND for FO), the two other stats equal; the Id's are checked above and below (names-v3, a new one per step)
        expect(spire_mag_row_bytes(full.shell_steps[0]) == spire_mag_row_bytes(full.steps[0]));
        bool fo = (std::string(cls) == "FO");
        for (size_t s = 1; s < full.steps.size(); s++) {
          const auto& f = full.steps[s];
          const auto& h = full.shell_steps[s];
          expect_eq(fo ? f.mind : f.pow, h.def);
          expect_eq(f.def, fo ? h.mind : h.pow);
          expect_eq(fo ? f.pow : f.mind, fo ? h.pow : h.mind);
          expect_eq(f.dex, h.dex);
        }
        for (const SpireMagLine* line_p : {&full, &shell.line}) {
          const auto& line = *line_p;
          expect_eq(5u, line.steps.size());
          std::vector<SpireMagRow> rows = line.steps;
          for (const auto& f : line.floors) {
            rows.emplace_back(f.row);
          }
          for (const auto& row : rows) {
            auto b = spire_mag_row_bytes(row);
            expect_eq(0x02, b[0]);
            expect_eq(row.id, b[1]);
            uint32_t sum = 0;
            for (size_t z = 0; z < 4; z++) {
              expect_eq(0, word(b, z) & 1); // every stat even
              sum += word(b, z) / 100;
            }
            expect_eq(sum, b[2]); // LL == the sum of the four levels
            expect(b[2] <= 200);
            expect(names.contains(std::format("02{:02X}00", b[1]))); // a type-02 id of V3
          }
          for (size_t s = 1; s < line.steps.size(); s++) {
            auto prev = spire_mag_row_bytes(line.steps[s - 1]);
            auto cur = spire_mag_row_bytes(line.steps[s]);
            for (size_t z = 0; z < 4; z++) {
              expect(word(cur, z) >= word(prev, z)); // monotone per stat
            }
            expect(cur[1] != prev[1]); // a visible evolve at every step
            expect(cur[2] > prev[2]);
          }
          // every step under every start floor and an already fed mag keeps the same rules
          for (uint32_t bonus : {0u, 5u, 10u, 14u}) {
            Bytes prev{};
            for (size_t s = 0; s < line.steps.size(); s++) {
              auto b = spire_mag_step_bytes(line, s, bonus, std::nullopt);
              uint32_t sum = 0;
              for (size_t z = 0; z < 4; z++) {
                expect_eq(0, word(b, z) & 1);
                sum += word(b, z) / 100;
                expect(!s || (word(b, z) >= word(prev, z)));
              }
              expect_eq(sum, b[2]);
              expect(b[2] <= 200);
              prev = b;
            }
          }
        }
      }
      expect_eq(std::string("HU"), spire_mag_archetype(0));
      expect_eq(std::string("RA"), spire_mag_archetype(11));
      expect_eq(std::string("FO"), spire_mag_archetype(10));
      expect_eq(std::string(""), spire_mag_archetype(12));
      expect_eq(std::set<uint8_t>({4, 8, 11, 15, 18, 21, 24, 27}), table.boss_rooms);
    });

    f6_case("HU start floor L22", [&]() {
      Bytes l22 = {0x02, 0x01, 0x16, 0x00, 0x4C, 0x04, 0xE8, 0x03, 0x00, 0x00, 0x64, 0x00};
      expect(l22 == spire_mag_step_bytes(hu, 0, 14, std::nullopt)); // a Lv15 start (+14) begins at Varuna L22
      Bytes l13 = {0x02, 0x01, 0x0D, 0x00, 0x20, 0x03, 0xF4, 0x01, 0x00, 0x00, 0x00, 0x00};
      expect(l13 == spire_mag_step_bytes(hu, 0, 5, std::nullopt)); // Lv6 start: Varuna L13 (8/5/0/0)
      expect(l13 == spire_mag_step_bytes(hu, 0, 10, std::nullopt)); // Lv11 start: the same floor
      Bytes l5 = {0x02, 0x00, 0x05, 0x00, 0xF4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
      expect(l5 == spire_mag_step_bytes(hu, 0, 0, std::nullopt)); // no bought levels: Mag L5
      // after the Dragon under the L22 floor: merged per stat (11/18/0/1), the step's kind (Varuna) and LL 30
      auto s1 = spire_mag_step_bytes(hu, 1, 14, std::nullopt);
      expect_eq(0x01, s1[1]);
      expect_eq(30, s1[2]);
      expect_eq(1100, word(s1, 0));
      expect_eq(1800, word(s1, 1));
      expect_eq(100, word(s1, 3));
    });

    f6_case("merge: a fed POW 23.50 against the step's 18.00", [&]() {
      Bytes fed = {0x02, 0x00, 0x1E, 0x00, 0xBC, 0x02, 0x2E, 0x09, 0x00, 0x00, 0x00, 0x00}; // DEF 7.00, POW 23.50
      auto b = spire_mag_step_bytes(hu, 1, 0, fed);
      expect_eq(2350, word(b, 1)); // POW stays 23.50
      expect_eq(700, word(b, 0));
      expect_eq(30, b[2]); // LL recomputed: 7 + 23
      expect_eq(0x01, b[1]); // the step's kind (Varuna)
      expect_eq(0x00, b[3]);
      // a mirrored mag that would push the merge past 200 keeps its own stats
      Bytes big = {0x02, 0x05, 0xC8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x4E, 0x00, 0x00}; // DEX 200.00
      auto c = spire_mag_step_bytes(hu, 4, 0, big);
      expect_eq(200, c[2]);
      expect_eq(20000, word(c, 2));
    });

    f6_case("idempotence: EXAM twice, ASK after a resume", [&]() {
      SpireRunMagLedger led;
      led.run_id = 0x1234;
      led.start_step = spire_mag_start_step(table, 1);
      expect_eq(0u, led.start_step);
      auto d = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 1, {}, 0); // kit
      expect(d.result);
      expect_eq(0u, d.expected_step);
      expect_eq(0, led.issued_step); // counted at the answer (B-REGIE-14-10)
      // the same ASK again (a reloaded floor) never issues the step twice: the known degrade of the protocol
      auto again = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 1, {}, 0);
      expect(!again.result);
      expect(spire_run_mag_result(led, true));
      expect_eq(0, led.issued_step);
      expect(!spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 2, {}, 0).result); // the next room: nothing owed
      expect(spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 4, {}, 0).result); // the Dragon
      expect(spire_run_mag_result(led, true));
      expect_eq(1, led.issued_step);
      // EXAM k8 twice issues one step
      auto e8 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 8, {}, 0);
      expect(e8.result);
      expect_eq(2u, e8.expected_step);
      expect(spire_run_mag_result(led, true));
      auto e8b = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 8, {}, 0);
      expect(!e8b.result);
      expect_eq(2u, e8b.expected_step);
      expect_eq(2, led.issued_step);
      expect(!spire_run_mag_result(led, true)); // nothing offered: a stray RESULT changes nothing
      expect_eq(2, led.issued_step);
      // the server's own boss clear of k8 counted in this game changes nothing either
      expect(!spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 9, {4, 8}, 0).result);
      // ASK after a resume: the ledger from the savestate holds 2 bosses and issued_step 2
      SpireRunMagLedger resumed;
      resumed.run_id = 0x1234;
      resumed.boss_rooms = {4, 8};
      resumed.issued_step = 2;
      resumed.last_mirrored = e8.bytes;
      auto r = spire_run_mag_decide(resumed, hu, SPIRE_RUN_MAG_ASK, 9, {}, 0);
      expect(!r.result);
      expect_eq(2u, r.expected_step);
      // the last step is the cap: a boss after Dark Falz adds nothing
      SpireRunMagLedger full;
      full.boss_rooms = {4, 8, 11, 15};
      full.issued_step = 4;
      expect(!spire_run_mag_decide(full, hu, SPIRE_RUN_MAG_EXAM, 18, {}, 0).result);
      expect_eq(4u, spire_mag_expected_step(5, 0, 5, true));
      // a practice run from k9 starts after two bosses
      expect_eq(2u, spire_mag_start_step(table, 9));
      expect_eq(0u, spire_mag_start_step(table, 4));
    });

    f6_case("pack full: the step stays owed", [&]() {
      SpireRunMagLedger led;
      led.issued_step = 0;
      led.last_mirrored = spire_mag_step_bytes(hu, 0, 0, std::nullopt);
      auto e = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 4, {}, 0);
      expect(e.result);
      expect(spire_run_mag_result(led, false)); // item_create2 gave FFFFFFFF
      expect_eq(0, led.issued_step);
      expect(led.last_mirrored == spire_mag_step_bytes(hu, 0, 0, std::nullopt));
      auto a = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 5, {}, 0); // the next room start
      expect(a.result);
      expect(a.bytes == e.bytes);
      expect(led.boss_rooms.count(4));
    });

    // B-REGIE-14-10: a step counts as issued when the server answers it with result 1; a RESULT 0 gives it back, a
    // RESULT 1 only confirms, and a RESULT that never arrives never yields a second issue of the same step.
    f6_case("counting: answer, the RESULT lost, then ASK", [&]() {
      SpireRunMagLedger led;
      led.run_id = 0x2222;
      auto kit = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 1, {}, 0);
      expect(kit.result);
      expect_eq(0, led.issued_step); // counted at the answer
      // a floor load killed the thread after the swap, before the RESULT: the client holds the kit mag
      auto after = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 2, {}, 0);
      expect(!after.result);
      expect_eq(0u, after.expected_step);
      expect_eq(0, led.issued_step);
      // the mag of the lost RESULT shows in the overlay: the ledger takes it as created, so the savestate keeps it
      expect(spire_run_mag_follow_mirror(led, {kit.bytes}));
      expect(led.last_mirrored == kit.bytes);
      expect(!spire_run_mag_result(led, true)); // a RESULT after that has nothing to confirm
      expect_eq(0, led.issued_step);
      // the Dragon's EXAM, its RESULT lost, then the same EXAM again (a reloaded floor): one issue of step 1
      auto e4 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 4, {}, 0);
      expect(e4.result);
      expect_eq(1, led.issued_step);
      expect(!spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 4, {}, 0).result);
      expect(!spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 5, {}, 0).result);
      expect_eq(1, led.issued_step);
    });

    f6_case("counting: answer, RESULT 0, then ASK gives the same step", [&]() {
      SpireRunMagLedger led;
      led.run_id = 0x3333;
      led.issued_step = 0;
      led.last_mirrored = spire_mag_step_bytes(hu, 0, 0, std::nullopt);
      auto e4 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 4, {}, 0);
      expect(e4.result);
      expect_eq(1, led.issued_step); // counted at the answer
      expect(spire_run_mag_result(led, false)); // pack full: the step is given back
      expect_eq(0, led.issued_step);
      expect(led.last_mirrored == spire_mag_step_bytes(hu, 0, 0, std::nullopt)); // the client kept the old mag
      auto a5 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 5, {}, 0);
      expect(a5.result);
      expect_eq(1u, a5.expected_step);
      expect(a5.bytes == e4.bytes);
      expect_eq(1, led.issued_step);
    });

    f6_case("counting: answer, RESULT 1, then the next boss's EXAM gives the next step", [&]() {
      SpireRunMagLedger led;
      led.run_id = 0x4444;
      led.issued_step = 0;
      led.last_mirrored = spire_mag_step_bytes(hu, 0, 0, std::nullopt);
      auto e4 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 4, {}, 0);
      expect(e4.result);
      expect_eq(1, led.issued_step); // counted at the answer
      expect(spire_run_mag_result(led, true)); // only confirms
      expect_eq(1, led.issued_step);
      expect(led.last_mirrored == e4.bytes);
      expect(!spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 5, {}, 0).result);
      auto e8 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 8, {}, 0);
      expect(e8.result);
      expect_eq(2u, e8.expected_step);
      expect_eq(2, led.issued_step);
      expect(!(e8.bytes == e4.bytes));
    });

    f6_case("counting: a resume with the issued step persisted issues nothing again", [&]() {
      SpireRunMagLedger led;
      led.run_id = 0x5555;
      led.issued_step = 1;
      led.boss_rooms = {4};
      led.last_mirrored = spire_mag_step_bytes(hu, 1, 0, std::nullopt);
      auto e8 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 8, {}, 0);
      expect(e8.result);
      led.lean = SPIRE_RUN_MAG_LEAN_SHELL; // F10: a Shell run (chosen at the kit)
      // the RESULT is lost; the next room start writes the savestate with this ledger
      SpireSavestate ss;
      ss.run_id = led.run_id;
      ss.values.assign(SPIRE_SAVESTATE_NUM_VALUES, 0);
      ss.run_mag = led;
      auto back = spire_savestate_from_json(spire_savestate_to_json(ss, 1));
      expect(back.run_mag.has_value());
      expect_eq(2, back.run_mag->issued_step);
      SpireRunMagLedger resumed = *back.run_mag;
      expect(!spire_run_mag_decide(resumed, hu, SPIRE_RUN_MAG_ASK, 9, {}, 0).result);
      expect(!spire_run_mag_decide(resumed, hu, SPIRE_RUN_MAG_EXAM, 8, {}, 0).result);
      expect_eq(2, resumed.issued_step);
      // F10: the lean comes back with the ledger; a resume shows no menu, so the next EXAM carries lean bits 0 and
      // still gets the Shell row (the column follows the ledger, not the bits)
      expect_eq(SPIRE_RUN_MAG_LEAN_SHELL, resumed.lean);
      auto last = resumed.last_mirrored;
      auto e11 = spire_run_mag_handle(resumed, table, hu, SPIRE_RUN_MAG_EXAM, 11, {}, {}, 0, 0);
      expect(e11.decision.result && e11.shell);
      expect_eq(3u, e11.decision.expected_step);
      expect(e11.decision.bytes == spire_mag_step_bytes(spire_mag_column(hu, SPIRE_RUN_MAG_LEAN_SHELL).line, 3, 0, last));
      expect(!(e11.decision.bytes == spire_mag_step_bytes(hu, 3, 0, last)));
      expect_eq(4200, word(e11.decision.bytes, 0)); // DEF 42: ShellSteps row 3
      expect_eq(SPIRE_RUN_MAG_LEAN_SHELL, resumed.lean);
      // an old savestate without "Lean" loads as no choice (the default column)
      SpireRunMagLedger old_led = led;
      old_led.lean = SPIRE_RUN_MAG_LEAN_NONE;
      ss.run_mag = old_led;
      std::string old_json = spire_savestate_to_json(ss, 1);
      size_t lean_pos = old_json.find("\"Lean\"");
      expect(lean_pos != std::string::npos);
      old_json.replace(lean_pos, 6, "\"Xean\"");
      expect_eq(SPIRE_RUN_MAG_LEAN_NONE, spire_savestate_from_json(old_json).run_mag->lean);
      // fix round (F10S critic N6): a "Lean" outside 0-2 (an edited file) reads as no choice, never as a wrapped byte
      auto with_lean = [&](const std::string& value) {
        SpireRunMagLedger l = led;
        l.lean = SPIRE_RUN_MAG_LEAN_FANG;
        ss.run_mag = l;
        std::string j = spire_savestate_to_json(ss, 1);
        size_t pos = j.find("\"Lean\"");
        size_t colon = j.find(':', pos);
        size_t end = j.find_first_of(",}\n", colon);
        j.replace(colon + 1, end - colon - 1, " " + value);
        return spire_savestate_from_json(j).run_mag->lean;
      };
      expect_eq(SPIRE_RUN_MAG_LEAN_SHELL, with_lean("2"));
      expect_eq(SPIRE_RUN_MAG_LEAN_NONE, with_lean("258")); // would wrap to 2 (Shell) as a uint8_t
      expect_eq(SPIRE_RUN_MAG_LEAN_NONE, with_lean("3"));
      expect_eq(SPIRE_RUN_MAG_LEAN_NONE, with_lean("-1"));
      // fix round (F10S critic N1): a savestate with run id 0 is no savestate (a run with id 0 has no ledger, and the
      // 'Z' handler would use none); the file is refused as unreadable, never half read
      SpireSavestate zero = ss;
      zero.run_id = 0;
      zero.run_mag.reset();
      expect_raises(std::runtime_error, [&]() { spire_savestate_from_json(spire_savestate_to_json(zero, 1)); });
    });

    f6_case("the ledger follows the feed mirror", [&]() {
      SpireRunMagLedger led;
      auto issued = spire_mag_step_bytes(hu, 1, 0, std::nullopt);
      Bytes fed = issued;
      fed[6] = 0x2E; // POW 18.00 -> 23.50
      fed[7] = 0x09;
      fed[2] = 30;
      expect(!spire_run_mag_follow_mirror(led, {fed})); // no mag issued yet: nothing to follow
      led.issued_step = 1;
      led.last_mirrored = issued;
      expect(!spire_run_mag_follow_mirror(led, {fed, issued})); // two type-02 items: never guess
      expect(!spire_run_mag_follow_mirror(led, {})); // the mag is gone (dropped): the ledger keeps its bytes
      expect(spire_run_mag_follow_mirror(led, {fed}));
      expect(led.last_mirrored == fed);
      expect(!spire_run_mag_follow_mirror(led, {issued})); // a lower level is not a fed run mag
      expect(led.last_mirrored == fed);
    });

    f6_case("refusals", [&]() {
      using R = SpireRunMagRefusal;
      SpireRunMagState ok;
      ok.gc_v3 = true;
      ok.spire_game = true;
      ok.has_overlay = true;
      ok.run_in_progress = true;
      ok.enabled = true;
      ok.has_table = true;
      ok.overlay_mags = 1;
      expect_eq(R::NONE, spire_run_mag_refusal(ok));
      auto with = [&](const std::function<void(SpireRunMagState&)>& fn) {
        SpireRunMagState st = ok;
        fn(st);
        return spire_run_mag_refusal(st);
      };
      expect_eq(R::NOT_GC_V3, with([](auto& st) { st.gc_v3 = false; }));
      expect_eq(R::NO_SPIRE_GAME, with([](auto& st) { st.spire_game = false; }));
      expect_eq(R::NO_OVERLAY, with([](auto& st) { st.has_overlay = false; }));
      expect_eq(R::RESTORE_PENDING, with([](auto& st) { st.restore_pending = true; }));
      expect_eq(R::MARKER, with([](auto& st) { st.run_in_progress = false; }));
      expect_eq(R::BASE_RETURNED, with([](auto& st) { st.base_returned = true; }));
      expect_eq(R::RUN_AGAIN, with([](auto& st) { st.run_again = true; }));
      expect_eq(R::REFRESHED_BY_61, with([](auto& st) { st.refreshed_by_61 = true; }));
      expect_eq(R::DISABLED, with([](auto& st) { st.enabled = false; }));
      expect_eq(R::NO_TABLE, with([](auto& st) { st.has_table = false; }));
      expect_eq(R::TWO_MAGS, with([](auto& st) { st.overlay_mags = 2; }));
      expect_eq(R::NONE, with([](auto& st) { st.overlay_mags = 0; }));
      // amendment 8: the q925 base game, the real character with its real mag live and no run copy
      expect_eq(R::NO_OVERLAY, with([](auto& st) {
        st.has_overlay = false;
        st.overlay_mags = 1;
      }));
      // Enabled false answers 0 even where everything else would give a mag
      expect_eq(R::DISABLED, with([](auto& st) { st.enabled = false; st.has_table = false; }));
      expect_eq(std::string("BASE_RETURNED"), std::string(spire_run_mag_refusal_name(R::BASE_RETURNED)));
      expect_eq(std::string("NO_LEDGER"), std::string(spire_run_mag_refusal_name(R::NO_LEDGER))); // fix round N1
      expect_eq(0x5A000000u, SPIRE_RUN_MAG_TAG);
      expect_eq(0x5A000601u, spire_run_mag_answer(SPIRE_RUN_MAG_TAG, 0x06, true));
      expect_eq(0x5A00FF00u, spire_run_mag_answer(SPIRE_RUN_MAG_TAG, SPIRE_RUN_MAG_NO_OLD, false));
    });

    // routeb-f6-critic.md C3-6: each case below fails under the mutation it names
    f6_case("handler: old_type is data1[1] of the one run mag, else 0xFF (C3-6 mutation 1)", [&]() {
      SpireRunMagLedger led;
      auto kit = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_ASK, 1, {}, {}, 0, 0);
      expect(kit.decision.result);
      expect_eq(SPIRE_RUN_MAG_NO_OLD, kit.old_type); // no mag yet: the quest deletes nothing
      expect_eq(0x5A00FF01u, kit.answer);
      expect(spire_run_mag_result(led, true));
      // the Dragon: the run copy holds the kit mag (kind 0x00), so the quest must delete kind 0x00 after the create
      auto dragon = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_EXAM, 4, {kit.decision.bytes}, {}, 0, 0);
      expect(dragon.decision.result);
      expect_eq(kit.decision.bytes[1], dragon.old_type);
      expect_eq(0x00, dragon.old_type);
      expect_eq(0x5A000001u, dragon.answer);
      expect(spire_run_mag_result(led, true));
      // De Rol Le with the Dragon's mag (Varuna, kind 0x01) in the pack
      auto rol = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_EXAM, 8, {dragon.decision.bytes}, {}, 0, 0);
      expect(rol.decision.result);
      expect_eq(0x01, dragon.decision.bytes[1]);
      expect_eq(0x01, rol.old_type);
      expect_eq(0x5A000101u, rol.answer);
      // two type-02 items: never name one (the refusal catches this first; the pure part stays safe on its own)
      SpireRunMagLedger two;
      expect_eq(SPIRE_RUN_MAG_NO_OLD,
          spire_run_mag_handle(two, table, hu, SPIRE_RUN_MAG_ASK, 1, {kit.decision.bytes, dragon.decision.bytes}, {}, 0, 0).old_type);
      // a refusal's answer
      expect_eq(0x5A00FF00u, SpireRunMagHandled().answer);
    });

    f6_case("handler: a lost EXAM is counted from the server's boss clears (C3-6 mutation 2)", [&]() {
      // Run from room 1: the kit mag issued; the EXAM of k4 never arrived, but the server counted the Dragon's clear
      SpireRunMagLedger led;
      led.issued_step = 0;
      led.last_mirrored = spire_mag_step_bytes(hu, 0, 0, std::nullopt);
      auto a5 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 5, {4}, 0);
      expect(a5.result);
      expect_eq(1u, a5.expected_step);
      expect(a5.bytes == spire_mag_step_bytes(hu, 1, 0, spire_mag_step_bytes(hu, 0, 0, std::nullopt)));
      expect(led.boss_rooms == std::set<uint8_t>({4}));
      expect_eq(1, led.issued_step);
    });

    f6_case("'V': the ledger never follows an overlay a 61 refreshed (C3-6 mutation 3, amendment 3)", [&]() {
      SpireRunMagLedger led;
      led.issued_step = 1;
      led.last_mirrored = spire_mag_step_bytes(hu, 1, 0, std::nullopt);
      // after a push the refreshed overlay holds the real mag (Pitri LV200, above the run mag's level)
      const Bytes pitri = {0x02, 0x1D, 0xC8, 0xA3, 0xF4, 0x01, 0x1C, 0x25, 0x10, 0x27, 0x00, 0x00};
      expect(!spire_run_mag_savestate_follow(&led, true, {pitri}));
      expect(led.last_mirrored == spire_mag_step_bytes(hu, 1, 0, std::nullopt));
      // the same overlay without the 61 is a fed run mag: followed (the gate is the only difference)
      SpireRunMagLedger fed = led;
      expect(spire_run_mag_savestate_follow(&fed, false, {pitri}));
      expect(fed.last_mirrored == pitri);
      expect(!spire_run_mag_savestate_follow(nullptr, false, {pitri}));
    });

    f6_case("handler: the start step is set by the first ASK only (C3-6 mutation 5)", [&]() {
      // A run from room 1: kit at k1, the Dragon and De Rol Le examined, then an ASK at k9 (a non-boss clear)
      SpireRunMagLedger led;
      auto kit = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_ASK, 1, {}, {}, 0, 0);
      expect_eq(0u, led.start_step);
      expect(spire_run_mag_result(led, true));
      auto e4 = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_EXAM, 4, {kit.decision.bytes}, {4}, 0, 0);
      expect(spire_run_mag_result(led, true));
      auto e8 = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_EXAM, 8, {e4.decision.bytes}, {4, 8}, 0, 0);
      expect_eq(2u, e8.decision.expected_step);
      expect(spire_run_mag_result(led, true));
      auto a9 = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_ASK, 9, {e8.decision.bytes}, {4, 8}, 0, 0);
      expect_eq(0u, led.start_step); // spire_mag_start_step(table, 9) would be 2
      expect_eq(2u, a9.decision.expected_step);
      expect(!a9.decision.result); // no skipped step
      expect_eq(2, led.issued_step);
      // a practice run that first asks at k9 starts after two bosses
      SpireRunMagLedger practice;
      auto p9 = spire_run_mag_handle(practice, table, hu, SPIRE_RUN_MAG_ASK, 9, {}, {}, 0, 0);
      expect_eq(2u, practice.start_step);
      expect(p9.decision.result);
      expect_eq(2u, p9.decision.expected_step);
    });

    f6_case("the offered mag is confirmed by the mirror only as the one type-02 item (C3-6 mutation 6)", [&]() {
      SpireRunMagLedger led;
      led.issued_step = 0;
      led.last_mirrored = spire_mag_step_bytes(hu, 0, 0, std::nullopt);
      auto e4 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 4, {}, 0);
      expect(e4.result && led.offered);
      // the offered mag next to the old one (created, old not deleted yet): not confirmed, the offer stays open
      expect(!spire_run_mag_follow_mirror(led, {e4.bytes, *led.last_mirrored}));
      expect(led.offered && (*led.offered == e4.bytes));
      expect(led.last_mirrored == spire_mag_step_bytes(hu, 0, 0, std::nullopt));
      // alone: confirmed as by a RESULT 1
      expect(spire_run_mag_follow_mirror(led, {e4.bytes}));
      expect(!led.offered && (led.last_mirrored == e4.bytes));
    });

    f6_case("texts", [&]() {
      expect_eq(std::string("Your mag evolves: Varuna. Equip it: Item Pack > Mag."), spire_run_mag_evolve_text("Varuna"));
      for (const std::string& t : {spire_run_mag_evolve_text("Ashvinau"), std::string(SPIRE_RUN_MAG_PACK_FULL_TEXT),
               std::string(SPIRE_RUN_MAG_REMINDER_TEXT), std::string(SPIRE_BOSS_BURST_TEXT),
               std::string(SPIRE_KIT_USE_TEXT), std::string(SPIRE_RESUME_MAG_TEXT)}) {
        expect_eq(t, spire_ticker_line(t)); // one ticker line each
      }
      expect_eq(std::string("Your Spire Mag comes back: equip it."), std::string(SPIRE_RESUME_MAG_TEXT));
      // bundle 2 S: the burst text names no mag (a run may have none) and holds for Shifta only and Shifta + Deband
      expect_eq(std::string("Boss ahead: you are buffed"), std::string(SPIRE_BOSS_BURST_TEXT));
      expect(std::string(SPIRE_BOSS_BURST_TEXT).find("mag") == std::string::npos);
      expect(std::string(SPIRE_BOSS_BURST_TEXT).find("Mag") == std::string::npos);
    });

    // F10 mag lean (routeb2-f10-randgevallen.md, "Besluit na de critic" 1, 2, 6, 7, 8)
    const auto hu_shell = spire_mag_column(hu, SPIRE_RUN_MAG_LEAN_SHELL).line;
    using LS = SpireRunMagLeanStore;

    f6_case("F10: a Shell run with a start bonus starts on the start floor (fix round, F10S critic N2)", [&]() {
      // the floors are one list for both columns: spire_mag_column must carry them into the Shell line
      expect(spire_mag_step_bytes(hu_shell, 0, 14, std::nullopt) == spire_mag_step_bytes(hu, 0, 14, std::nullopt));
      SpireRunMagLedger led;
      auto kit = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_ASK, 1, {}, {}, 14, SPIRE_RUN_MAG_LEAN_SHELL);
      expect(kit.decision.result && kit.shell);
      expect_eq(0x01, kit.decision.bytes[1]); // Varuna L22, not Mag 5/0/0/0
      expect_eq(1100, word(kit.decision.bytes, 0));
      expect_eq(1000, word(kit.decision.bytes, 1));
      auto s1 = spire_mag_step_bytes(hu_shell, 1, 14, std::nullopt); // Shell 18/7/0/0 merged with the floor 11/10/0/1
      expect_eq(1800, word(s1, 0));
      expect_eq(1000, word(s1, 1)); // the floor's POW 10 beats the row's 7
      expect_eq(100, word(s1, 3));
      expect_eq(0x01, s1[1]);
    });

    f6_case("F10: the lean is bits 8-15 of the ASK/EXAM word", [&]() {
      expect_eq(2, spire_run_mag_lean_bits(0x5A040204)); // 'Z' EXAM, lean 2 (Shell), k4
      expect_eq(1, spire_run_mag_lean_bits(0x5A010101)); // 'Z' ASK, lean 1 (Fang/Mind), k1
      expect_eq(0, spire_run_mag_lean_bits(0x5A010001)); // an ASK from a quest before F10: no choice
      expect_eq(0x2A, spire_run_mag_lean_bits(0x5A012A01));
    });

    f6_case("F10: lean 2 picks the DEF-heavy row, lean 0 and 1 the default; only the row differs", [&]() {
      for (uint8_t lean : {SPIRE_RUN_MAG_LEAN_NONE, SPIRE_RUN_MAG_LEAN_FANG, SPIRE_RUN_MAG_LEAN_SHELL}) {
        bool is_shell = (lean == SPIRE_RUN_MAG_LEAN_SHELL);
        SpireRunMagLedger led;
        auto kit = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_ASK, 1, {}, {}, 0, lean);
        expect_eq(lean ? LS::STORED : LS::NONE, kit.lean_store);
        expect_eq(lean, led.lean);
        expect(kit.decision.result);
        expect(kit.decision.bytes == spire_mag_step_bytes(hu, 0, 0, std::nullopt)); // step 0 the same for every lean
        expect_eq(0x5A00FF01u, kit.answer);
        expect(spire_run_mag_result(led, true));
        expect(!led.offered_shell);
        auto e4 = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_EXAM, 4, {kit.decision.bytes}, {4}, 0, lean);
        expect(e4.decision.result);
        expect_eq(1u, e4.decision.expected_step);
        expect_eq(0x00, e4.old_type);
        expect_eq(0x5A000001u, e4.answer); // the same answer for every lean: create the new mag, delete kind 0x00
        expect(!e4.shell_fell_back);
        expect_eq(is_shell, e4.shell);
        expect_eq(is_shell, led.offered_shell);
        expect(e4.decision.bytes == spire_mag_step_bytes(is_shell ? hu_shell : hu, 1, 0, kit.decision.bytes));
        expect_eq(is_shell ? 1800 : 700, word(e4.decision.bytes, 0)); // DEF
        expect_eq(is_shell ? 700 : 1800, word(e4.decision.bytes, 1)); // POW
        expect_eq(0x01, e4.decision.bytes[1]); // Varuna in both columns (Sega's HU evolution reads no DEF)
        expect(spire_run_mag_result(led, true));
        expect(!led.offered_shell);
      }
    });

    f6_case("F10: a lean after a step above the kit's is ignored, as is a value above 2 (sub 5 too)", [&]() {
      SpireRunMagLedger led;
      led.issued_step = 1;
      led.boss_rooms = {4};
      led.last_mirrored = spire_mag_step_bytes(hu, 1, 0, std::nullopt);
      auto e8 = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_EXAM, 8, {*led.last_mirrored}, {4, 8}, 0,
          SPIRE_RUN_MAG_LEAN_SHELL);
      expect_eq(LS::AFTER_STEP, e8.lean_store);
      expect_eq(SPIRE_RUN_MAG_LEAN_NONE, led.lean);
      expect(e8.decision.result && !e8.shell);
      expect(e8.decision.bytes == spire_mag_step_bytes(hu, 2, 0, spire_mag_step_bytes(hu, 1, 0, std::nullopt)));
      // a Fang run never turns Shell midway either
      SpireRunMagLedger fang;
      fang.issued_step = 1;
      fang.lean = SPIRE_RUN_MAG_LEAN_FANG;
      expect_eq(LS::AFTER_STEP, spire_run_mag_lean_store(fang, SPIRE_RUN_MAG_LEAN_SHELL));
      expect_eq(SPIRE_RUN_MAG_LEAN_FANG, fang.lean);
      // with only the kit's step issued (0) the lean is still taken, and a second choice replaces the first
      SpireRunMagLedger kit;
      kit.issued_step = 0;
      expect_eq(LS::STORED, spire_run_mag_lean_store(kit, SPIRE_RUN_MAG_LEAN_SHELL));
      expect_eq(SPIRE_RUN_MAG_LEAN_SHELL, kit.lean);
      expect_eq(LS::STORED, spire_run_mag_lean_store(kit, SPIRE_RUN_MAG_LEAN_FANG));
      expect_eq(SPIRE_RUN_MAG_LEAN_FANG, kit.lean);
      // 0 stores nothing (it keeps a choice made earlier); above 2 is ignored
      SpireRunMagLedger none;
      expect_eq(LS::STORED, spire_run_mag_lean_store(none, SPIRE_RUN_MAG_LEAN_SHELL));
      expect_eq(LS::NONE, spire_run_mag_lean_store(none, SPIRE_RUN_MAG_LEAN_NONE));
      expect_eq(SPIRE_RUN_MAG_LEAN_SHELL, none.lean);
      expect_eq(LS::INVALID, spire_run_mag_lean_store(none, 3));
      expect_eq(LS::INVALID, spire_run_mag_lean_store(none, 0xFF));
      expect_eq(SPIRE_RUN_MAG_LEAN_SHELL, none.lean);
      expect_eq(std::string("stored"), std::string(spire_run_mag_lean_store_name(LS::STORED)));
    });

    f6_case("F10: a practice run from k >= 5 with Shell in its first ASK gets the Shell row of its start step", [&]() {
      SpireRunMagLedger p;
      auto p9 = spire_run_mag_handle(p, table, hu, SPIRE_RUN_MAG_ASK, 9, {}, {}, 0, SPIRE_RUN_MAG_LEAN_SHELL);
      expect_eq(LS::STORED, p9.lean_store);
      expect_eq(2u, p.start_step);
      expect(p9.decision.result && p9.shell);
      expect_eq(2u, p9.decision.expected_step);
      expect(p9.decision.bytes == spire_mag_row_bytes(hu.shell_steps[2])); // 30/9/1/0 Rudra
      expect_eq(3000, word(p9.decision.bytes, 0));
      expect_eq(900, word(p9.decision.bytes, 1));
      // FO from k16 (after four bosses): the last Shell step, Bana (FO's evolution reads DEF from L50 on)
      SpireRunMagLedger f;
      auto f16 = spire_run_mag_handle(f, table, table.classes.at("FO"), SPIRE_RUN_MAG_ASK, 16, {}, {}, 0,
          SPIRE_RUN_MAG_LEAN_SHELL);
      expect(f16.decision.result && f16.shell);
      expect_eq(4u, f16.decision.expected_step);
      expect_eq(0x24, f16.decision.bytes[1]);
      expect_eq(4600, word(f16.decision.bytes, 0)); // DEF
      expect_eq(1400, word(f16.decision.bytes, 3)); // MIND
    });

    f6_case("F10: a json without, with a short or with a bad ShellSteps answers Steps, never NO_TABLE", [&]() {
      const std::string row0 = R"({"Id": 0, "Def": 5, "Pow": 0, "Dex": 0, "Mind": 0})";
      const std::string fang1 = R"({"Id": 1, "Def": 7, "Pow": 18, "Dex": 0, "Mind": 0})";
      const std::string shell1 = R"({"Id": 1, "Def": 18, "Pow": 7, "Dex": 0, "Mind": 0})";
      auto json_with = [&](const std::string& shell) {
        return R"({"Enabled": true, "Classes": {"HU": {"Steps": [)" + row0 + ", " + fang1 + "]" + shell +
            R"(}}, "BossRooms": [4, 8]})";
      };
      auto steps_answer = [&](const SpireMagLine& line, bool want_shell) {
        SpireRunMagLedger led;
        led.issued_step = 0;
        led.lean = SPIRE_RUN_MAG_LEAN_SHELL;
        led.last_mirrored = spire_mag_row_bytes(line.steps[0]);
        auto t = spire_mag_parse(json_with(""));
        auto e4 = spire_run_mag_handle(led, t, line, SPIRE_RUN_MAG_EXAM, 4, {*led.last_mirrored}, {4}, 0, 0);
        expect(e4.decision.result); // a mag is answered: the table stays usable
        expect_eq(want_shell, e4.shell);
        expect_eq(!want_shell, e4.shell_fell_back);
        expect_eq(want_shell, led.offered_shell);
        expect_eq(want_shell ? 1800 : 700, word(e4.decision.bytes, 0));
      };
      for (const std::string& bad : {std::string(""), ", \"ShellSteps\": [" + row0 + "]",
               ", \"ShellSteps\": [" + row0 + R"(, {"Id": 1, "Def": 300, "Pow": 7, "Dex": 0, "Mind": 0}])",
               std::string(", \"ShellSteps\": 7"), ", \"ShellSteps\": [" + row0 + R"(, {"Id": 1, "Def": 18}])"}) {
        auto t = spire_mag_parse(json_with(bad)); // throws on none of them
        const auto& line = t.classes.at("HU");
        expect(line.shell_steps.empty());
        expect(!line.shell_note.empty());
        auto col = spire_mag_column(line, SPIRE_RUN_MAG_LEAN_SHELL);
        expect(col.fell_back && !col.shell);
        steps_answer(line, false);
      }
      expect_eq(std::string("no ShellSteps"), spire_mag_parse(json_with("")).classes.at("HU").shell_note);
      expect_eq(std::string("ShellSteps has 1 rows, Steps 2"),
          spire_mag_parse(json_with(", \"ShellSteps\": [" + row0 + "]")).classes.at("HU").shell_note);
      // the same json with a good ShellSteps: the Shell row
      auto good = spire_mag_parse(json_with(", \"ShellSteps\": [" + row0 + ", " + shell1 + "]"));
      expect_eq(std::string(""), good.classes.at("HU").shell_note);
      expect_eq(2u, good.classes.at("HU").shell_steps.size());
      steps_answer(good.classes.at("HU"), true);
      // a line whose shell_steps is not as long as steps (built by hand) falls back as well
      SpireMagLine short_line = hu;
      short_line.shell_steps.pop_back();
      short_line.shell_note.clear();
      auto col = spire_mag_column(short_line, SPIRE_RUN_MAG_LEAN_SHELL);
      expect(col.fell_back && !col.shell);
      expect_eq(hu.steps.size(), col.line.steps.size());
      expect(spire_mag_row_bytes(col.line.steps[1]) == spire_mag_row_bytes(hu.steps[1]));
      // lean 0 and 1 never fall back (nothing to fall back from)
      expect(!spire_mag_column(short_line, SPIRE_RUN_MAG_LEAN_FANG).fell_back);
      expect(!spire_mag_column(short_line, SPIRE_RUN_MAG_LEAN_NONE).fell_back);
    });

    f6_case("F10: the evolve text names Shell", [&]() {
      expect_eq(std::string("Your mag evolves: Varuna (Shell). Equip it: Item Pack > Mag."),
          spire_run_mag_evolve_text("Varuna", true));
      expect_eq(std::string("Your mag evolves: Varuna. Equip it: Item Pack > Mag."), spire_run_mag_evolve_text("Varuna", false));
      expect_eq(spire_run_mag_evolve_text("Bana", true), spire_ticker_line(spire_run_mag_evolve_text("Bana", true)));
      // fix round (F10S critic N4): the first mag of a run (the kit, or a practice run's first ASK) names Shell too
      expect_eq(std::string("Your mag (Shell) is in your pack: equip it (Item Pack > Mag)"), spire_run_mag_reminder_text(true));
      expect_eq(std::string(SPIRE_RUN_MAG_REMINDER_TEXT), spire_run_mag_reminder_text(false));
      expect_eq(std::string("Your mag is in your pack: equip it (Item Pack > Mag)"), spire_run_mag_reminder_text(false));
      // no run mag line is longer than the longest before this round: every Id of the shipped table, both columns
      auto names = phosg::JSON::parse(phosg::load_file("system/tables/names-v3.json"));
      size_t longest = 0;
      for (const auto& [cls, line] : table.classes) {
        std::vector<SpireMagRow> rows = line.steps;
        rows.insert(rows.end(), line.shell_steps.begin(), line.shell_steps.end());
        for (const auto& f : line.floors) {
          rows.emplace_back(f.row);
        }
        for (const auto& row : rows) {
          std::string name = names.get_string(std::format("02{:02X}00", row.id));
          for (bool shell : {false, true}) {
            longest = std::max(longest, spire_run_mag_evolve_text(name, shell).size());
          }
        }
      }
      for (bool shell : {false, true}) {
        longest = std::max(longest, spire_run_mag_reminder_text(shell).size());
      }
      expect_eq(SPIRE_RUN_MAG_TICKER_MAX, longest); // Kabanda (Shell): 61
      expect_eq(61u, spire_run_mag_evolve_text("Kabanda", true).size());
      expect(spire_run_mag_reminder_text(true).size() <= SPIRE_RUN_MAG_TICKER_MAX);
    });

    f6_case("bundle 2 S: the kit text after 'M' START", [&]() {
      expect(spire_kit_use_text_due(true, 1, 0, false, false)); // hp_mat only
      // v62: atp_grind alone no longer makes the text due (section "Spire bundle 8")
      expect(spire_kit_use_text_due(true, 3, 3, false, false));
      expect(!spire_kit_use_text_due(true, 0, 0, false, false)); // no kit level (an old meta file): no text
      expect(!spire_kit_use_text_due(false, 1, 1, false, false)); // LIST or BUY: no text
      expect(!spire_kit_use_text_due(true, 1, 1, true, false)); // resumed: the kit is not given
      expect(!spire_kit_use_text_due(true, 1, 1, false, true)); // a second START of this client in this run
    });

    f6_case("bundle 2 S: the boss burst text at the boss door", [&]() {
      std::set<uint8_t> sent;
      expect(!spire_boss_burst_text_due(4, table.boss_rooms, sent)); // the Dragon: no burst
      expect(!spire_boss_burst_text_due(7, table.boss_rooms, sent)); // no boss room
      expect(spire_boss_burst_text_due(8, table.boss_rooms, sent)); // De Rol Le
      expect(!spire_boss_burst_text_due(8, table.boss_rooms, sent)); // the same room again in this run: no second line
      expect(!spire_boss_burst_text_due(9, table.boss_rooms, sent));
      expect(spire_boss_burst_text_due(11, table.boss_rooms, sent)); // Vol Opt
      expect(sent == std::set<uint8_t>({8, 11}));
      std::set<uint8_t> other_client; // per client: the partner's own 'V' gets its own line
      expect(spire_boss_burst_text_due(8, table.boss_rooms, other_client));
      std::set<uint8_t> none;
      expect(!spire_boss_burst_text_due(8, {}, none)); // no boss rooms known: no text
    });

    f6_case("bundle 2 S: only an in-game 61 marks the run copy refreshed", [&]() {
      expect(spire_run_mag_command_refreshes(0x61));
      expect(!spire_run_mag_command_refreshes(0x98));
      expect(!spire_run_mag_command_refreshes(0x00));
    });

    f6_case("pack-full notice: once per given-back step per run (hotfix 1)", [&]() {
      SpireRunMagPackFullNotices told;
      expect(spire_run_mag_pack_full_notice(told, 0x64D5C0EF, true, false, 1)); // first RESULT 0 for step 1
      expect(!spire_run_mag_pack_full_notice(told, 0x64D5C0EF, true, false, 1)); // the same step again: silent
      expect(spire_run_mag_pack_full_notice(told, 0x64D5C0EF, true, false, 2)); // a later step: told
      expect(!spire_run_mag_pack_full_notice(told, 0x64D5C0EF, true, false, 2));
      expect(spire_run_mag_pack_full_notice(told, 0x11111111, true, false, 1)); // a new run: told again
      expect(!spire_run_mag_pack_full_notice(told, 0x11111111, true, true, 3)); // RESULT 1: never
      expect(!spire_run_mag_pack_full_notice(told, 0x11111111, false, false, 4)); // no offer settled: never
      expect(spire_run_mag_pack_full_notice(told, 0x11111111, true, false, 3)); // RESULT 1 recorded nothing
      // the notice leaves the ledger as spire_run_mag_result left it
      SpireRunMagLedger led;
      led.run_id = 0x22222222;
      led.issued_step = 0;
      led.last_mirrored = spire_mag_step_bytes(hu, 0, 0, std::nullopt);
      auto e = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 4, {}, 0);
      expect(e.result && (led.offered_step == 1));
      int32_t offered_step = led.offered_step;
      expect(spire_run_mag_result(led, false));
      expect_eq(0, led.issued_step);
      auto before = led;
      expect(spire_run_mag_pack_full_notice(told, led.run_id, true, false, offered_step));
      expect_eq(before.issued_step, led.issued_step);
      expect(before.offered == led.offered);
      expect(before.last_mirrored == led.last_mirrored);
      expect(before.boss_rooms == led.boss_rooms);
      expect(before.offered_shell == led.offered_shell);
      // a Shell run: the same step is given back and offered again at the next ASK (the text names no column)
      SpireRunMagLedger sl;
      sl.run_id = 0x33333333;
      sl.issued_step = 0;
      sl.lean = SPIRE_RUN_MAG_LEAN_SHELL;
      auto col = spire_mag_column(hu, sl.lean);
      expect(col.shell);
      sl.last_mirrored = spire_mag_step_bytes(col.line, 0, 0, std::nullopt);
      expect(spire_run_mag_decide(sl, col.line, SPIRE_RUN_MAG_EXAM, 4, {}, 0).result);
      int32_t shell_step = sl.offered_step;
      expect(spire_run_mag_result(sl, false));
      expect(spire_run_mag_pack_full_notice(told, sl.run_id, true, false, shell_step));
      expect(spire_run_mag_decide(sl, col.line, SPIRE_RUN_MAG_ASK, 5, {}, 0).result);
      expect_eq(shell_step, sl.offered_step); // the retry offers the given-back step
      expect(spire_run_mag_result(sl, false));
      expect(!spire_run_mag_pack_full_notice(told, sl.run_id, true, false, shell_step)); // still full: silent
      expect_eq(std::string("Pack full: your mag did not grow. Keep slots free"),
          std::string(SPIRE_RUN_MAG_PACK_FULL_TEXT));
      expect_eq(std::string("Pack full: no room for your mag. Keep slots free"),
          std::string(SPIRE_RUN_MAG_PACK_FULL_FIRST_TEXT));
      for (const char* t : {SPIRE_RUN_MAG_PACK_FULL_TEXT, SPIRE_RUN_MAG_PACK_FULL_FIRST_TEXT}) {
        expect(std::string(t).size() <= SPIRE_RUN_MAG_TICKER_MAX);
        expect_eq(std::string(t), spire_ticker_line(t));
      }
    });

    f6_case("pack-full decision per client key, run and step (hotfix 1, critic N3)", [&]() {
      SpireRunMagPackFullTold told;
      const std::string a = "3117539661.A", b = "3117539662.B";
      const uint32_t run = 0x64D5C0EF;
      // the lab case: two players of one run, both RESULT 0 for step 1 (newserv.log:183897, :184006): each told once
      auto da = spire_run_mag_pack_full_decide(told, a, run, true, false, 1, 0);
      expect(da.tell && (std::string(da.text) == SPIRE_RUN_MAG_PACK_FULL_TEXT));
      auto db = spire_run_mag_pack_full_decide(told, b, run, true, false, 1, 0);
      expect(db.tell && (std::string(db.text) == SPIRE_RUN_MAG_PACK_FULL_TEXT));
      expect(!spire_run_mag_pack_full_decide(told, a, run, true, false, 1, 0).tell);
      expect(!spire_run_mag_pack_full_decide(told, b, run, true, false, 1, 0).tell);
      expect(spire_run_mag_pack_full_decide(told, a, run, true, false, 2, 1).tell); // a later step
      expect(spire_run_mag_pack_full_decide(told, a, run + 1, true, false, 1, 0).tell); // a new run
      expect(!spire_run_mag_pack_full_decide(told, a, run + 1, true, false, 1, 0).tell);
      // no notice and nothing recorded: RESULT 1, nothing settled, no key, run id 0
      expect(!spire_run_mag_pack_full_decide(told, a, run, true, true, 3, 2).tell);
      expect(!spire_run_mag_pack_full_decide(told, a, run, false, false, 3, 2).tell);
      expect(!spire_run_mag_pack_full_decide(told, "", run, true, false, 3, 2).tell);
      expect(!spire_run_mag_pack_full_decide(told, a, 0, true, false, 3, 2).tell);
      expect(!spire_run_mag_pack_full_decide(told, a, 0, true, false, 3, 2).tell);
      expect_eq(2u, told.size());
      // the first mag of a run (nothing issued before the offer): its own text
      auto first = spire_run_mag_pack_full_decide(told, b, run, true, false, 0, -1);
      expect(first.tell && (std::string(first.text) == SPIRE_RUN_MAG_PACK_FULL_FIRST_TEXT));
      // a third player below the key limit wipes nothing: a and b stay silent for their told steps
      expect(spire_run_mag_pack_full_decide(told, "c", run, true, false, 1, 0).tell);
      expect(!spire_run_mag_pack_full_decide(told, a, run + 1, true, false, 1, 0).tell);
      expect(!spire_run_mag_pack_full_decide(told, b, run, true, false, 1, 0).tell);
      // the key limit: a new key at SPIRE_RUN_MAG_PACK_FULL_KEYS_MAX records wipes every record (told once more)
      SpireRunMagPackFullTold full;
      for (size_t i = 0; i < SPIRE_RUN_MAG_PACK_FULL_KEYS_MAX; i++) {
        expect(spire_run_mag_pack_full_decide(full, std::format("k{}", i), run, true, false, 1, 0).tell);
      }
      expect_eq(SPIRE_RUN_MAG_PACK_FULL_KEYS_MAX, full.size());
      auto old_key = spire_run_mag_pack_full_decide(full, "k0", run, true, false, 1, 0);
      expect(!old_key.tell && !old_key.wiped); // a known key: no wipe
      auto new_key = spire_run_mag_pack_full_decide(full, "new", run, true, false, 1, 0);
      expect(new_key.tell && new_key.wiped);
      expect_eq(1u, full.size());
      expect(spire_run_mag_pack_full_decide(full, "k0", run, true, false, 1, 0).tell); // told once more after the wipe
    });

    f6_case("hex round trip", [&]() {
      Bytes b = {0x02, 0x01, 0x16, 0x00, 0x4C, 0x04, 0xE8, 0x03, 0x00, 0x00, 0x64, 0x00};
      expect_eq(std::string("020116004C04E80300006400"), spire_run_mag_bytes_hex(b));
      expect(b == spire_run_mag_bytes_from_hex("020116004C04E80300006400"));
    });
  }

  phosg::log_info_f("-- Spire savestate: written from the run copy, replaced by the next, deleted only at a true run end (v44)");
  {
    // The run copy: a mag of the run (never the real mag: forbid_mags 0 strips it from the server's run copy,
    // Client.cc:696-697, and the GC client strips it from its live run copy, routeb-mag-vraag.md E1/E3), a run weapon
    // with grind and special, the Glass weapon, an empty slot and a stack of Monomates
    PlayerInventory overlay;
    overlay.num_items = 5;
    auto put = [&](size_t z, std::array<uint8_t, 12> d1, uint32_t id) {
      for (size_t i = 0; i < 12; i++) {
        overlay.items[z].data.data1[i] = d1[i];
      }
      overlay.items[z].data.id = id;
    };
    put(0, {0x02, 0x01, 0x16, 0x00, 0x4C, 0x04, 0xE8, 0x03, 0x00, 0x00, 0x64, 0x00}, 0x00810000); // a run mag (HU L22)
    put(1, {0x00, 0x01, 0x00, 0x05, 0x8A, 0, 0, 0, 0, 0, 0, 0}, 0x00810001); // Saber +5, special
    put(2, {0x00, 0x05, 0x00, 0x08, 0, 0, 0, 0, 0, 0, 0, 0}, 0x00810002); // the Glass weapon, +8
    put(4, {0x03, 0x00, 0x00, 0x00, 0x00, 0x04, 0, 0, 0, 0, 0, 0}, 0x00810003); // Monomate x4
    int32_t glass = 99;
    auto items = spire_savestate_items(overlay, 0x00810002, &glass, std::nullopt);
    expect_eq(3u, items.size()); // no ledger: no mag; no empty slot
    expect_eq(0x00, items[0].data1[0]);
    expect_eq(0x8A, items[0].data1[4]); // the special stays: the quest creates the item with its full data1
    expect_eq(0x05, items[0].data1[3]); // and the grind (B-E01-188: equip_item would make a plain one from data1[0-2])
    {
      // an equipped run weapon comes back as the same item (grind, special), unequipped: the equipped flag is the
      // inventory's, not the item's, and there is no route that equips an existing item (B-E01-188)
      PlayerInventory equipped = overlay;
      equipped.items[1].flags = 0x00000008;
      equipped.items[1].state = 2;
      int32_t g = 0;
      auto again = spire_savestate_items(equipped, 0, &g, std::nullopt);
      expect_eq(3u, again.size());
      expect(!memcmp(&again[0], &items[0], sizeof(ItemData)));
    }
    expect_eq(1, glass);
    expect_eq(0x04, items[2].data1[5]); // the stack count stays
    for (const auto& item : items) {
      expect(item.data1[0] != 0x02);
    }
    expect_eq(3u, spire_savestate_items(overlay, 0, &glass, std::nullopt).size());
    expect_eq(-1, glass); // no Glass item
    put(3, {0x03, 0x09, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0x00810004); // a Scape Doll: lives are a counter
    expect_eq(3u, spire_savestate_items(overlay, 0, &glass, std::nullopt).size());
    {
      // F6 (amendments 1 and 2): the run mag the ledger last mirrored comes back, matched on all 12 data1 bytes
      phosg::log_info_f("   F6 (savestate: the ledger's run mag kept, any other type-02 item and the Scape Doll dropped)");
      SpireRunMagBytes ledger_mag = SpireMagBytes{0x02, 0x01, 0x16, 0x00, 0x4C, 0x04, 0xE8, 0x03, 0x00, 0x00, 0x64, 0x00};
      auto kept = spire_savestate_items(overlay, 0x00810002, &glass, ledger_mag);
      expect_eq(4u, kept.size());
      expect_eq(0x02, kept[0].data1[0]);
      expect(!memcmp(kept[0].data1.data(), ledger_mag->data(), 12));
      expect_eq(2, glass); // the Glass index counts the kept mag
      for (const auto& item : kept) {
        expect(!((item.data1[0] == 0x03) && (item.data1[1] == 0x09))); // still no Scape Doll
      }
      // the same kind with other stats is not the ledger's mag (a partner's run mag of the same class, or the run
      // before an 'I'): dropped
      SpireRunMagBytes other_stats = ledger_mag;
      (*other_stats)[6] = 0xEA;
      expect_eq(3u, spire_savestate_items(overlay, 0, &glass, other_stats).size());
      // two items with the ledger's exact bytes: only one comes back
      PlayerInventory twice = overlay;
      twice.num_items = 6;
      twice.items[5] = twice.items[0];
      twice.items[5].data.id = 0x00810005;
      int32_t g2 = 0;
      auto one = spire_savestate_items(twice, 0, &g2, ledger_mag);
      expect_eq(4u, one.size());
      expect_eq(1u, std::count_if(one.begin(), one.end(), [](const ItemData& it) { return it.data1[0] == 0x02; }));
      // restore after the run: a 61 that still holds the run mag in the real mag's place is no verified push
      PlayerInventory real_inv = overlay;
      const uint8_t pitri[12] = {0x02, 0x1D, 0xC8, 0xA3, 0xF4, 0x01, 0x1C, 0x25, 0x10, 0x27, 0x00, 0x00}; // LV200
      for (size_t i = 0; i < 12; i++) {
        real_inv.items[0].data.data1[i] = pitri[i];
      }
      PlayerInventory after_push = real_inv;
      for (size_t i = 0; i < 12; i++) {
        after_push.items[0].data.data1[i] = (*ledger_mag)[i];
      }
      expect(!spire_guard_push_verified(true, spire_guard_same_inventory(after_push, 0, real_inv, 0) &&
          spire_guard_same_materials(after_push, real_inv)));
      expect(spire_guard_push_verified(true, spire_guard_same_inventory(real_inv, 0, real_inv, 0) &&
          spire_guard_same_materials(real_inv, real_inv)));
      // the ledger travels in the savestate file
      SpireSavestate ss;
      ss.run_id = 0xABCD0001;
      ss.values.assign(SPIRE_SAVESTATE_NUM_VALUES, 0);
      SpireRunMagLedger led;
      led.run_id = ss.run_id;
      led.issued_step = 2;
      led.start_step = 0;
      led.boss_rooms = {4, 8};
      led.last_mirrored = ledger_mag;
      led.lean = 2;
      ss.run_mag = led;
      auto back = spire_savestate_from_json(spire_savestate_to_json(ss, 1));
      expect(back.run_mag.has_value());
      expect_eq(0xABCD0001u, back.run_mag->run_id);
      expect_eq(2, back.run_mag->issued_step);
      expect_eq(std::set<uint8_t>({4, 8}), back.run_mag->boss_rooms);
      expect(back.run_mag->last_mirrored == ledger_mag);
      expect_eq(2, back.run_mag->lean);
      SpireSavestate no_mag = ss;
      no_mag.run_mag.reset();
      expect(!spire_savestate_from_json(spire_savestate_to_json(no_mag, 1)).run_mag.has_value());
      // the resume line comes only when the savestate brings a mag back
      expect_eq(std::string("Your Spire Mag comes back: equip it."), spire_run_resume_mag_ticker(true, true));
      expect(spire_run_resume_mag_ticker(true, false).empty());
      expect(spire_run_resume_mag_ticker(false, true).empty());
    }

    // 'V' writes only from a run copy, after 'B', before the run end, with all values
    const size_t n = SPIRE_SAVESTATE_NUM_VALUES;
    expect(spire_savestate_write_allowed(false, true, true, n, n));
    expect(!spire_savestate_write_allowed(true, true, true, n, n)); // after the run end: never
    expect(!spire_savestate_write_allowed(false, false, true, n, n)); // no overlay: never from the real character
    expect(!spire_savestate_write_allowed(false, true, false, n, n)); // before the client's 'B'
    expect(!spire_savestate_write_allowed(false, true, true, n - 1, n)); // a value lost on the way
    expect(!spire_savestate_write_allowed(false, true, true, n, n + 1));

    // The file: written, replaced by the next room start, read back exactly
    auto make_ss = [&](uint32_t run_id, uint32_t k, uint32_t lives) {
      SpireSavestate ss;
      ss.exists = true;
      ss.run_id = run_id;
      ss.episode = 0;
      ss.quest_number = 921;
      ss.start_room = 1;
      ss.values.assign(SPIRE_SAVESTATE_NUM_VALUES, 0);
      ss.values[SPIRE_SS_K] = k;
      ss.values[SPIRE_SS_LIVES] = lives;
      ss.values[SPIRE_SS_MESETA] = 4321;
      ss.values[SPIRE_SS_ELAPSED] = 1830;
      ss.items = items;
      ss.glass_index = 1;
      return ss;
    };
    auto dir = std::filesystem::temp_directory_path() / std::format("newserv-static-spire-savestate-{}", getpid());
    std::filesystem::create_directories(dir);
    std::string file = (dir / "0000000042.ABCDEF01.savestate.json").string();
    phosg::save_file(file, spire_savestate_to_json(make_ss(0x1234, 5, 2), 42));
    auto read = spire_savestate_from_json(phosg::load_file(file));
    expect(read.exists);
    expect_eq(0x1234u, read.run_id);
    expect_eq(5u, read.values[SPIRE_SS_K]);
    expect_eq(4321u, read.values[SPIRE_SS_MESETA]);
    expect_eq(3u, read.items.size());
    expect_eq(1, read.glass_index);
    for (size_t z = 0; z < items.size(); z++) {
      expect(!memcmp(&read.items[z], &items[z], sizeof(ItemData)));
    }
    phosg::save_file(file, spire_savestate_to_json(make_ss(0x1234, 6, 1), 42)); // room 6 starts: replaces room 5
    read = spire_savestate_from_json(phosg::load_file(file));
    expect_eq(6u, read.values[SPIRE_SS_K]);
    expect_eq(1u, read.values[SPIRE_SS_LIVES]);
    // a damaged file is refused, never half read
    expect_raises(std::runtime_error, [&]() { spire_savestate_from_json("{\"RunId\":1,\"Episode\":0,\"Quest\":921,"
        "\"StartRoom\":1,\"Practice\":false,\"Values\":[1,2],\"Items\":[]}"); });

    // Deleted at every true end of its own run: dead, time up, Give up, won, also when a player is gone by then (regie
    // 2026-10-25: a finished run must never be resumed); only the run timer's "player left" (a disconnect) keeps it
    expect(spire_savestate_end_deletes(false));
    expect(!spire_savestate_end_deletes(true)); // run timer: player left (the partner's Wii froze)
    {
      // the gone player's file is still deleted: it was written in the run, the present one is added, each once
      auto files = spire_savestate_end_files({"a.savestate.json", "b.savestate.json"}, {"a.savestate.json", ""});
      expect_eq(2u, files.size());
      expect_eq(std::string("a.savestate.json"), files[0]);
      expect_eq(std::string("b.savestate.json"), files[1]);
      expect(spire_savestate_end_files({}, {"", ""}).empty());
      expect_eq(1u, spire_savestate_end_files({}, {"c.savestate.json"}).size());
    }
    expect(spire_savestate_is_of_run(read, 0x1234));
    expect(!spire_savestate_is_of_run(read, 0x9999)); // a savestate of another run is not this game's to delete
    expect(!spire_savestate_is_of_run(read, 0));
    expect(!spire_savestate_is_of_run(SpireSavestate(), 0x1234));
    if (spire_savestate_end_deletes(false) && spire_savestate_is_of_run(read, 0x1234)) {
      std::filesystem::remove(file);
    }
    expect(!std::filesystem::exists(file));
    std::filesystem::remove_all(dir);
  }

  phosg::log_info_f("-- Spire savestate: resume only into the same room, variant and run; every item once per index (v44)");
  {
    SpireSavestate ss;
    ss.exists = true;
    ss.run_id = 0xA1;
    ss.episode = 0;
    ss.values.assign(SPIRE_SAVESTATE_NUM_VALUES, 0);
    ss.values[SPIRE_SS_K] = 5;
    ss.values[SPIRE_SS_VARIANT] = 1; // k5 elite
    ss.values[SPIRE_SS_LIVES] = 2;
    ss.values[SPIRE_SS_ELAPSED] = 31 * 60 + 5;
    expect(spire_savestate_matches(ss, 0, 5, 1)); // "Spire room 5 elite"
    expect(!spire_savestate_matches(ss, 0, 5, 0)); // the normal k5 quest: no free swap from elite to normal
    expect(!spire_savestate_matches(ss, 0, 6, 1)); // another room
    expect(!spire_savestate_matches(ss, 1, 5, 1)); // another episode
    expect(!spire_savestate_matches(SpireSavestate(), 0, 5, 1));
    expect_eq(std::string("Continue at Spire room 5 elite: 2 lives, 31 min played"), spire_savestate_continue_text(ss));
    ss.practice = true;
    expect_eq(std::string("Continue at Spire room 5 elite: 2 lives, 31 min played (practice run)"),
        spire_savestate_continue_text(ss));

    // Two clients: the leader's savestate sets the game's run; the partner resumes only from the same run
    // arguments: run_ended, game_resumed, game_run_id, own_matches, own_run_id
    expect(spire_resume_client(false, true, 0xA1, true, 0xA1)); // leader, and a partner of the same run
    expect(!spire_resume_client(false, true, 0xA1, true, 0xB2)); // a partner's savestate of another run: fresh
    expect(!spire_resume_client(false, true, 0xA1, false, 0xA1)); // the partner's savestate is of another room
    expect(!spire_resume_client(false, false, 0xC3, true, 0xC3)); // the leader starts fresh: so does the partner
    expect(!spire_resume_client(true, true, 0xA1, true, 0xA1)); // never after the run end ("dead stays dead")
    expect(!spire_resume_client(false, true, 0, true, 0));

    // Items: index + 1, the Glass flag on its own index, nothing outside the copy or without a resume answer
    expect_eq(1u, spire_resume_item_ack(false, true, 0, 3, 1));
    expect_eq(2u | SPIRE_RESUME_ITEM_GLASS, spire_resume_item_ack(false, true, 1, 3, 1));
    expect_eq(3u, spire_resume_item_ack(false, true, 2, 3, 1));
    expect_eq(3u, spire_resume_item_ack(false, true, 2, 3, 1)); // asked again after a floor load: the same item
    expect_eq(0u, spire_resume_item_ack(false, true, 3, 3, 1)); // past the end: nothing
    expect_eq(0u, spire_resume_item_ack(false, false, 0, 3, 1)); // a client answered fresh gets no item
    expect_eq(0u, spire_resume_item_ack(true, true, 0, 3, 1)); // after the run end: nothing
    // the start answer: no header register overlaps the item bytes, the ack or the answer itself
    for (const auto& [index, reg] : SPIRE_RESUME_HEADER) {
      expect(index < SPIRE_SAVESTATE_NUM_VALUES);
      expect((reg < SPIRE_RESUME_ITEM_FIRST_REGISTER) || (reg >= SPIRE_RESUME_ITEM_FIRST_REGISTER + 12));
      expect(reg != SPIRE_RESUME_ITEM_ACK_REGISTER);
      expect(reg != SPIRE_RESUME_ANSWER_REGISTER);
      expect(reg < 59); // the quest's own block r20-r58, below the engine registers
    }
  }

  phosg::log_info_f("-- Spire practice: a run from a later room counts for nothing; a resumed run keeps its start (v44)");
  {
    // arguments: start room, K0 (the run's first room), resumed, the savestate's practice bit
    expect(!spire_run_is_practice(1, 1, false, false)); // q921 from room 1
    expect(!spire_run_is_practice(16, 16, false, false)); // Spire Run II starts in k16: its first room
    expect(spire_run_is_practice(5, 1, false, false)); // q944 "Spire room 5" without a savestate
    expect(spire_run_is_practice(20, 16, false, false));
    expect(!spire_run_is_practice(5, 1, true, false)); // q944 resumed from a run that began in room 1: it counts
    expect(spire_run_is_practice(7, 1, true, true)); // resumed from a practice run: stays practice
    expect(!spire_run_is_practice(0, 0, false, false)); // a 'B' from before v44: as before
    // a new run in the records only from the first room, never on a resume or a practice start
    expect(spire_run_counts_as_new(1, false, false));
    expect(!spire_run_counts_as_new(5, false, true));
    expect(!spire_run_counts_as_new(5, true, false));
    expect(spire_run_counts_as_new(0, false, false));
    // practice stores no unlock or relic flag, the real run stores them all
    expect(spire_practice_blocks_flag(true, 0x0390));
    expect(spire_practice_blocks_flag(true, SPIRE_META_FLAG_PAY_WIN));
    expect(!spire_practice_blocks_flag(false, 0x0390));
    expect(!spire_practice_blocks_flag(true, 0x0021)); // outside the bridged range: not the bridge's
    // the one ticker line at the start
    SpireSavestate ss;
    ss.exists = true;
    ss.values.assign(SPIRE_SAVESTATE_NUM_VALUES, 0);
    ss.values[SPIRE_SS_K] = 5;
    ss.values[SPIRE_SS_LIVES] = 2;
    ss.values[SPIRE_SS_ELAPSED] = 1865;
    expect_eq(std::string("Run resumed at room 5: 2 lives, 31 min. Equip your gear."), spire_run_start_ticker(true, false, &ss));
    expect_eq(std::string("Practice run resumed at room 5: 2 lives, 31 min. Equip your gear."), spire_run_start_ticker(true, true, &ss));
    expect_eq(std::string("Practice run: records and unlocks do not count."), spire_run_start_ticker(false, true, nullptr));
    expect(spire_run_start_ticker(false, false, nullptr).empty());
    for (const auto& line : {spire_run_start_ticker(true, true, &ss), spire_run_start_ticker(false, true, nullptr)}) {
      expect_eq(line, spire_ticker_line(line)); // one line, no window in a run (v35)
    }
    // the mag line of a resume (regie 2026-10-26): on every resume, one line, no longer than the practice line
    expect_eq(std::string("Your Spire Mag comes back: equip it."), spire_run_resume_mag_ticker(true, true));
    expect(spire_run_resume_mag_ticker(false, true).empty());
    expect_eq(spire_run_resume_mag_ticker(true, true), spire_ticker_line(spire_run_resume_mag_ticker(true, true)));
    expect(spire_run_resume_mag_ticker(true, true).size() <= spire_run_start_ticker(false, true, nullptr).size());

    // the draw seed: the same run, room, n-th draw and inputs give the same draw; anything else another one
    std::vector<uint32_t> in(16, 3);
    uint64_t a = spire_draw_seed(0xA1, 5, 0, in);
    expect_eq(a, spire_draw_seed(0xA1, 5, 0, in)); // the room replayed from its savestate
    expect(a != spire_draw_seed(0xA2, 5, 0, in)); // another run
    expect(a != spire_draw_seed(0xA1, 6, 0, in)); // another room
    expect(a != spire_draw_seed(0xA1, 5, 1, in)); // the reroll: the second draw of the room
    in[9] ^= 1; // another relic
    expect(a != spire_draw_seed(0xA1, 5, 0, in));
  }

  phosg::log_info_f("-- Spire bundle 8 (ux-pass): draw.json names, overview line, server texts (v62, B-REGIE-16-13)");
  {
    // every case runs, a failed one is logged, and the section fails at its end if any did (so one run shows them all)
    size_t b8_failed = 0;
    auto b8_case = [&](const char* name, const std::function<void()>& fn) {
      phosg::log_info_f("   B8 ({})", name);
      try {
        fn();
      } catch (const std::exception& e) {
        b8_failed++;
        phosg::log_error_f("   B8 ({}) FAILED: {}", name, e.what());
      }
    };
    // a draw table with num_tags 2 (3 * 2 + 1 rows per band, each row every entry); per archetype its entries as
    // (name JSON value, rare), an empty name value leaves the key out (a draw.json from before bundle 8)
    using B8Entries = std::vector<std::pair<std::string, bool>>;
    auto b8_table_json = [](const std::array<B8Entries, 3>& arch_entries, const std::string& top_keys) {
      std::string archetypes;
      for (size_t a = 0; a < 3; a++) {
        std::string entries, members;
        for (size_t z = 0; z < arch_entries[a].size(); z++) {
          const auto& [name, rare] = arch_entries[a][z];
          entries += std::format("{}{{\"cap\":2,\"vanaf_k\":1,\"android_ok\":true,\"rare\":{},\"own_curse\":false,"
                                 "\"glass_ok\":false{}}}",
              z ? "," : "", rare ? "true" : "false", name.empty() ? "" : (",\"name\":" + name));
          members += std::format("{}{}", z ? "," : "", z);
        }
        std::string row = std::format("[{}]", members);
        archetypes += std::format("{}{{\"entries\":[{}],\"kbands\":[1],\"rows\":{{\"1\":[{},{},{},{},{},{},{}]}},"
                                  "\"slot_c_zero_tags\":[]}}",
            a ? "," : "", entries, row, row, row, row, row, row, row);
      }
      return std::format("{{\"num_tags\":2,\"rare_base\":5,\"rare_per_k\":3,\"uncommon_below\":40,\"curse_pct\":10,"
                         "\"max_attempts\":8,\"blood_pact_bonus\":15,\"relic_bit_r3\":4,\"relic_bit_r13\":8,"
                         "\"relic_bit_r14\":16,\"archetypes\":[{}]{}}}",
          archetypes, top_keys);
    };
    auto b8_filename = (std::filesystem::temp_directory_path() / std::format("newserv-static-spire-b8-{}.json", getpid())).string();
    auto b8_load = [&](const std::string& json) {
      phosg::save_file(b8_filename, json);
      auto t = SpireDrawTable::from_json_file(b8_filename);
      std::filesystem::remove(b8_filename);
      return t;
    };
    const B8Entries hu = {{"\"Saber\"", false}, {"\"Fire Shield\"", false}, {"\"Lavis Cannon\"", true}, {"\"Monomate x3\"", false}};
    const B8Entries ra = {{"\"Handgun\"", false}, {"\"Ranger Wall\"", false}, {"\"Spread Needle\"", true}, {"\"Dimate x2\"", false}};
    const B8Entries fo = {{"\"Cane\"", false}, {"\"Barrier\"", false}, {"\"Club of Laconium\"", true}, {"\"Trimate\"", false}};
    const B8Entries old_style = {{"", false}, {"", false}, {"", true}, {"", false}};
    const std::string new_keys = ",\"tag_names\":[\"Fire\",\"Guard\"],"
                                 "\"exams\":{\"4\":{\"boss\":\"Dragon\",\"modifiers\":[\"Armored: HP x1.3\",\"Furious: ATP x1.15\"]},"
                                 "\"8\":{\"boss\":\"De Rol Le\",\"modifiers\":[]}},"
                                 "\"exam_extras\":[\"Photon Drop x2\",\"Star Atomizer\"]";
    SpireDrawTable named, old;
    // the draw slots of r181: board slot 2/3/4/5 entry id at bits 0/6/12/18, curse code at bits 24/26/28/30
    auto drawn_of = [](uint32_t s2, uint32_t s3, uint32_t s4, uint32_t s5, uint32_t curse3, uint32_t curse4) -> uint32_t {
      return s2 | (s3 << 6) | (s4 << 12) | (s5 << 18) | (curse3 << 26) | (curse4 << 28);
    };
    SpireBridgeTextContext none, with_names;

    b8_case("loader: the new keys are read", [&]() {
      named = b8_load(b8_table_json({hu, ra, fo}, new_keys));
      expect_eq(std::string("Saber"), named.archetypes.at(0).entries.at(0).name);
      expect_eq(std::string("Lavis Cannon"), named.archetypes.at(0).entries.at(2).name);
      expect_eq(std::string("Spread Needle"), named.archetypes.at(1).entries.at(2).name);
      expect_eq(2u, named.tag_names.size());
      expect_eq(std::string("Guard"), named.tag_names.at(1));
      expect_eq(2u, named.exams.size());
      expect_eq(std::string("Dragon"), named.exams.at(4).boss);
      expect_eq(2u, named.exams.at(4).modifiers.size());
      expect_eq(std::string("Furious: ATP x1.15"), named.exams.at(4).modifiers.at(1));
      expect_eq(std::string("De Rol Le"), named.exams.at(8).boss);
      expect(named.exams.at(8).modifiers.empty());
      expect_eq(2u, named.exam_extras.size());
      expect_eq(std::string("Star Atomizer"), named.exam_extras.at(1));
    });
    with_names.draw = &named;
    with_names.archetype = 0;

    b8_case("loader: a draw.json without the keys loads, nothing named, the texts of before", [&]() {
      old = b8_load(b8_table_json({old_style, old_style, old_style}, ""));
      expect_eq(3u, old.archetypes.size());
      for (const auto& a : old.archetypes) {
        for (const auto& e : a.entries) {
          expect(e.name.empty());
        }
      }
      expect(old.tag_names.empty() && old.exams.empty() && old.exam_extras.empty());
      auto rnd0 = [](uint32_t) -> uint32_t { return 0; };
      uint32_t drawn = spire_draw(old, SpireDrawInput(), rnd0);
      expect_eq(1u, ((drawn >> 6) & 0x3F)); // the draw itself as before
      expect(spire_draw_overview_line(old, 0, drawn).empty()); // no names: no overview line
      SpireBridgeTextContext ctx;
      ctx.draw = &old;
      ctx.archetype = 0;
      // an older quest (bits 16-23 = 0) with this table: the texts of before
      expect_eq(std::string("Room 2"), spire_bridge_message_text(0x41000002, ctx));
      expect_eq(std::string("Room 5 - Elite (harder, better reward)"), spire_bridge_message_text(0x41000105, ctx));
      expect_eq(std::string("The way is open. Walk to a warp."), spire_bridge_message_text(0x43000602, ctx));
      expect_eq(std::string("Waiting for your partner..."), spire_bridge_message_text(0x50000000, ctx));
      expect_eq(std::string("Partner has not taken a reward. The way is open."), spire_bridge_message_text(0x43000702, ctx));
      // and the bundle 8 codes without names: nothing named
      expect_eq(std::string("Room 2."), spire_bridge_message_text(0x41010002, ctx));
      expect_eq(std::string("Room 4 - Exam."), spire_bridge_message_text(0x41020304, ctx));
      expect_eq(std::string("The way is open. Walk to the warp: Exam Room 4."), spire_bridge_message_text(0x43F30604, ctx));
      expect_eq(std::string("Exam reward: 600 meseta."), spire_bridge_message_text(0x43030A21, ctx));
    });

    b8_case("loader: a bad value is no name, never a failed table", [&]() {
      const B8Entries bad = {{"123", false}, {"\"A name of twenty-three!\"", false}, {"\"$C4Red\"", true}, {"\"Twenty-two chars name!\"", false}};
      auto t = b8_load(b8_table_json({bad, bad, bad},
          ",\"tag_names\":[\"Fire\",7,\"Much too long\"],\"exams\":{\"4\":{\"boss\":5,\"modifiers\":[\"ok\",3]},\"x\":{}},"
          "\"exam_extras\":\"no list\""));
      expect(t.archetypes.at(0).entries.at(0).name.empty()); // a number
      expect(t.archetypes.at(0).entries.at(1).name.empty()); // 23 characters
      expect(t.archetypes.at(0).entries.at(2).name.empty()); // a colour escape
      expect_eq(std::string("Twenty-two chars name!"), t.archetypes.at(0).entries.at(3).name); // 22: fits
      expect_eq(3u, t.tag_names.size());
      expect_eq(std::string("Fire"), t.tag_names.at(0));
      expect(t.tag_names.at(1).empty() && t.tag_names.at(2).empty());
      expect(t.exams.at(4).boss.empty());
      expect_eq(2u, t.exams.at(4).modifiers.size());
      expect(t.exams.at(4).modifiers.at(1).empty());
      expect_eq(1u, t.exams.size()); // "x" is no room number
      expect(t.exam_extras.empty());
    });

    b8_case("overview line: names, (rare), (cursed), page order, fewer slots, the limits", [&]() {
      // slot 3 Saber, slot 4 Fire Shield cursed, slot 5 Lavis Cannon (rare entry)
      expect_eq(std::string("Rewards: Saber, Fire Shield (cursed), Lavis Cannon (rare). Walk to choose."),
          spire_draw_overview_line(named, 0, drawn_of(0, 1, 2, 3, 0, 2)));
      // cursed wins over rare
      expect_eq(std::string("Rewards: Lavis Cannon (cursed), Saber, Fire Shield. Walk to choose."),
          spire_draw_overview_line(named, 0, drawn_of(0, 3, 1, 2, 3, 0)));
      // the archetype's own names; R14's slot 2 is the fourth page
      expect_eq(std::string("Rewards: Handgun, Ranger Wall, Spread Needle (rare), Dimate x2. Walk to choose."),
          spire_draw_overview_line(named, 1, drawn_of(4, 1, 2, 3, 0, 0)));
      // fewer than three: what was drawn
      expect_eq(std::string("Rewards: Cane, Barrier. Walk to choose."), spire_draw_overview_line(named, 2, drawn_of(0, 1, 2, 0, 0, 0)));
      expect(spire_draw_overview_line(named, 0, 0).empty()); // nothing drawn
      expect(spire_draw_overview_line(named, 3, drawn_of(0, 1, 2, 3, 0, 0)).empty()); // no such archetype
      expect(spire_draw_overview_line(named, 0, drawn_of(0, 1, 2, 9, 0, 0)).empty()); // past the table: no name
      expect(spire_draw_overview_line(old, 0, drawn_of(0, 1, 2, 3, 0, 0)).empty()); // a drawn entry without a name
      // over 90 with the suffixes: they go; still over: the count
      const B8Entries longer = {{"\"Eighteen chars abc\"", true}, {"\"Eighteen chars def\"", true}, {"\"Eighteen chars ghi\"", true},
          {"\"Twenty-two chars name!\"", false}};
      const B8Entries longest = {{"\"Twenty-two chars nam1\"", false}, {"\"Twenty-two chars nam2\"", false},
          {"\"Twenty-two chars nam3\"", false}, {"\"Twenty-two chars nam4\"", false}};
      auto t = b8_load(b8_table_json({longer, longest, longest}, ""));
      std::string no_suffix = spire_draw_overview_line(t, 0, drawn_of(0, 1, 2, 3, 0, 0));
      expect_eq(std::string("Rewards: Eighteen chars abc, Eighteen chars def, Eighteen chars ghi. Walk to choose."), no_suffix);
      expect_eq(std::string("Rewards: 3 ready. Walk to choose."), spire_draw_overview_line(t, 1, drawn_of(0, 1, 2, 3, 0, 0)));
      expect_eq(std::string("Rewards: 4 ready. Walk to choose."), spire_draw_overview_line(t, 0, drawn_of(4, 1, 2, 3, 0, 0)));
      for (uint32_t a = 0; a < 3; a++) {
        for (uint32_t drawn : {drawn_of(0, 1, 2, 3, 1, 2), drawn_of(4, 1, 2, 3, 3, 3), drawn_of(0, 4, 0, 0, 0, 0)}) {
          for (const auto* table : {&named, &t}) {
            std::string line = spire_draw_overview_line(*table, a, drawn);
            expect(line.size() <= SPIRE_DRAW_OVERVIEW_MAX);
            expect_eq(line, spire_ticker_line(line));
          }
        }
      }
    });

    b8_case("'C' 2 shows the kept overview line until the next 'Q' or a floor change", [&]() {
      expect_eq(std::string("Take a step: your reward list opens."), spire_bridge_message_text(0x43000202));
      expect_eq(std::string("Take a step: your reward list opens."), spire_bridge_message_text(0x43000202, with_names));
      SpireOverviewKept kept{"Rewards: Saber, Fire Shield, Lavis Cannon (rare). Walk to choose.", 7, 3};
      expect_eq(kept.line, spire_overview_kept_line(&kept, 7, 3));
      expect(spire_overview_kept_line(&kept, 7, 4).empty()); // another floor
      expect(spire_overview_kept_line(&kept, 8, 3).empty()); // another game
      expect(spire_overview_kept_line(nullptr, 7, 3).empty());
      SpireBridgeTextContext ctx = with_names;
      ctx.overview = spire_overview_kept_line(&kept, 7, 3);
      expect_eq(kept.line, spire_bridge_message_text(0x43000202, ctx));
      // the other codes ignore it
      expect_eq(std::string("Partner has not taken a reward. The way is open."), spire_bridge_message_text(0x43000702, ctx));
    });

    b8_case("'H', 'P', 'C' 11 and the boss clear", [&]() {
      expect_eq(std::string("Take a step: the menu opens."), spire_bridge_message_text(0x48000000));
      expect_eq(std::string("Your partner is ready. Walk to choose your reward."), spire_bridge_message_text(0x50000001));
      expect_eq(std::string("Waiting for your partner..."), spire_bridge_message_text(0x50000000)); // b = 0 as before
      expect_eq(std::string("Take a step: your run starts."), spire_bridge_message_text(0x43000B01));
      expect_eq(std::string("Take a step: your run starts."), spire_bridge_message_text(0x43000B00, with_names));
      expect_eq(std::string("Exam passed!"), std::string(spire_bridge_clear_text(true))); // SpireBossClearQuests
      expect_eq(std::string(SPIRE_BRIDGE_ROOM_CLEAR_TEXT), std::string(spire_bridge_clear_text(false)));
      expect_eq(std::string("Room clear! Take a step for your reward."), std::string(spire_bridge_clear_text(false)));
    });

    b8_case("'C' 6 names both pipes, an exam pipe its boss; bits 16-23 = 0 as before", [&]() {
      expect_eq(std::string("The way is open. Walk to a warp."), spire_bridge_message_text(0x43000605, with_names));
      expect_eq(std::string("The way is open: Room 5 ELITE or Room 5 REST. Walk close to a warp to read it."),
          spire_bridge_message_text(0x43210605, with_names));
      expect_eq(std::string("The way is open: Room 5 normal or Room 5 ELITE. Walk close to a warp to read it."),
          spire_bridge_message_text(0x43100605));
      expect_eq(std::string("The way is open: Room 12 REST or Room 12 normal. Walk close to a warp to read it."),
          spire_bridge_message_text(0x4302060C));
      expect_eq(std::string("The way is open. Walk to the warp: Exam Dragon."), spire_bridge_message_text(0x43F30604, with_names));
      expect_eq(std::string("The way is open. Walk to the warp: Exam Room 4."), spire_bridge_message_text(0x43F30604));
      expect_eq(std::string("The way is open. Walk to the warp: Exam Room 6."), spire_bridge_message_text(0x43F30606, with_names));
      expect_eq(std::string("The way is open. Walk to the warp: Room 3 normal."), spire_bridge_message_text(0x43F00603));
      expect_eq(std::string("The way is open. Walk to a warp."), spire_bridge_message_text(0x43050605)); // no such type
      expect_eq(std::string("The way is open. Walk to a warp."), spire_bridge_message_text(0x43FF0605));
    });

    b8_case("'A' names the tag, the rest, the exam boss and modifier; extra 0 of types 0 and 1 as before", [&]() {
      expect_eq(std::string("Room 2"), spire_bridge_message_text(0x41000002, with_names));
      expect_eq(std::string("Room 2. Next reward: Fire."), spire_bridge_message_text(0x41010002, with_names));
      expect_eq(std::string("Room 2."), spire_bridge_message_text(0x41010002)); // no tag name known
      expect_eq(std::string("Room 2."), spire_bridge_message_text(0x41030002, with_names)); // tag 3 of 2
      expect_eq(std::string("Room 5 - Elite (harder, better reward)"), spire_bridge_message_text(0x41000105, with_names));
      expect_eq(std::string("Room 5 - Elite: harder foes, rare reward. Next reward: Guard."),
          spire_bridge_message_text(0x41020105, with_names));
      expect_eq(std::string("Room 5 - Elite: harder foes, rare reward."), spire_bridge_message_text(0x41090105, with_names));
      expect_eq(std::string("Room 7 - Rest: no enemies, full HP, then the shop."), spire_bridge_message_text(0x41000207));
      expect_eq(std::string("Room 7 - Rest: no enemies, full HP, then the shop."), spire_bridge_message_text(0x41000207, with_names));
      expect_eq(std::string("Room 4 - Exam: Dragon."), spire_bridge_message_text(0x41000304, with_names));
      expect_eq(std::string("Room 4 - Exam: Dragon (Furious: ATP x1.15)."), spire_bridge_message_text(0x41020304, with_names));
      expect_eq(std::string("Room 4 - Exam: Dragon."), spire_bridge_message_text(0x41050304, with_names)); // modifier 5 of 2
      expect_eq(std::string("Room 8 - Exam: De Rol Le."), spire_bridge_message_text(0x41010308, with_names));
      expect_eq(std::string("Room 4 - Exam."), spire_bridge_message_text(0x41020304));
      expect_eq(std::string("Room 9"), spire_bridge_message_text(0x41000409, with_names)); // no such type: as before
    });

    b8_case("'C' 10 the exam reward, to the writer's archetype", [&]() {
      // rare entry 2 + 1 = 3, extra 0 + 1 = 1, n = 1
      expect_eq(std::string("Exam reward: 600 meseta, Lavis Cannon, Photon Drop x2."), spire_bridge_message_text(0x43030A21, with_names));
      expect_eq(std::string("Exam reward: 600 meseta, Lavis Cannon, Photon Drop x2. Inventory full: make room, it waits."),
          spire_bridge_message_text(0x43830A21, with_names));
      expect_eq(std::string("Exam reward: 500 meseta."), spire_bridge_message_text(0x43000A00, with_names));
      expect_eq(std::string("Exam reward: 1200 meseta, Star Atomizer."), spire_bridge_message_text(0x43000AE2, with_names));
      SpireBridgeTextContext ra_ctx = with_names;
      ra_ctx.archetype = 1;
      expect_eq(std::string("Exam reward: 700 meseta, Spread Needle."), spire_bridge_message_text(0x43030A40, ra_ctx));
      SpireBridgeTextContext unknown = with_names;
      unknown.archetype = -1; // the writer's class is not known: no rare name
      expect_eq(std::string("Exam reward: 700 meseta. Inventory full: make room, it waits."), spire_bridge_message_text(0x43830A40, unknown));
      expect_eq(std::string("Exam reward: 700 meseta."), spire_bridge_message_text(0x43090A40, with_names)); // rare 9 of 4
      expect_eq(std::string("Exam reward: 600 meseta."), spire_bridge_message_text(0x43030A21)); // no names known
      // too long: the extra goes before the rare name
      const B8Entries longest = {{"\"Twenty-two chars nam1\"", true}, {"\"Twenty-two chars nam2\"", true}, {"\"Twenty-two chars nam3\"", true},
          {"\"Twenty-two chars nam4\"", true}};
      auto t = b8_load(b8_table_json({longest, longest, longest}, ",\"exam_extras\":[\"Twenty-four characters!!\"]"));
      SpireBridgeTextContext long_ctx;
      long_ctx.draw = &t;
      long_ctx.archetype = 2;
      expect_eq(std::string("Exam reward: 1200 meseta, Twenty-two chars nam1, Twenty-four characters!!."),
          spire_bridge_message_text(0x43010AE1, long_ctx));
      expect_eq(std::string("Exam reward: 1200 meseta, Twenty-two chars nam1. Inventory full: make room, it waits."),
          spire_bridge_message_text(0x43810AE1, long_ctx));
    });

    b8_case("every text one line of at most 90 characters", [&]() {
      const B8Entries longest = {{"\"Twenty-two chars nam1\"", true}, {"\"Twenty-two chars nam2\"", true}, {"\"Twenty-two chars nam3\"", true},
          {"\"Twenty-two chars nam4\"", true}};
      auto t = b8_load(b8_table_json({longest, longest, longest},
          ",\"tag_names\":[\"Ten chars!\",\"Ten chars?\"],\"exams\":{\"4\":{\"boss\":\"A boss with a long name\","
          "\"modifiers\":[\"A modifier line that is long enough to overflow it\"]},"
          "\"200\":{\"boss\":\"A boss name so long that the exam line runs over the ninety characters it may have\",\"modifiers\":[]}},"
          "\"exam_extras\":[\"Twenty-four characters!!\"]"));
      expect_eq(std::string("Room 4 - Exam: A boss with a long name."), spire_bridge_message_text(0x41010304, SpireBridgeTextContext{&t, 0, ""}));
      expect_eq(std::string("Room 200 - Exam."), spire_bridge_message_text(0x413003C8, SpireBridgeTextContext{&t, 0, ""}));
      expect_eq(std::string("The way is open. Walk to the warp: Exam Room 200."), spire_bridge_message_text(0x43F306C8, SpireBridgeTextContext{&t, 0, ""}));
      size_t checked = 0, longest_seen = 0;
      for (const SpireDrawTable* table : std::vector<const SpireDrawTable*>{nullptr, &named, &old, &t}) {
        for (int arch : {-1, 0, 1, 2}) {
          SpireBridgeTextContext ctx{table, arch, ""};
          for (uint32_t bits = 0; bits < 0x100; bits++) {
            for (uint32_t k : {1u, 4u, 9u, 12u, 200u, 255u}) {
              for (uint32_t kind_point : {0x41000000u, 0x41000100u, 0x41000200u, 0x41000300u, 0x43000600u}) {
                std::string text = spire_bridge_message_text(kind_point | (bits << 16) | k, ctx);
                expect(!text.empty() && (text.size() <= SPIRE_BRIDGE_TEXT_MAX) && (text == spire_ticker_line(text)));
                longest_seen = std::max(longest_seen, text.size());
                checked++;
              }
            }
            for (uint32_t low : {0x00u, 0x01u, 0x02u, 0x1Fu, 0xE1u, 0xFFu}) {
              std::string text = spire_bridge_message_text(0x43000A00 | (bits << 16) | low, ctx);
              expect(!text.empty() && (text.size() <= SPIRE_BRIDGE_TEXT_MAX) && (text == spire_ticker_line(text)));
              longest_seen = std::max(longest_seen, text.size());
              checked++;
            }
          }
        }
      }
      phosg::log_info_f("   B8: {} texts checked, longest {} characters", checked, longest_seen);
      for (uint32_t value : {0x48000000u, 0x50000001u, 0x43000202u, 0x43000B01u}) {
        std::string text = spire_bridge_message_text(value);
        expect(text.size() <= SPIRE_BRIDGE_TEXT_MAX && (text == spire_ticker_line(text)));
      }
    });

    b8_case("kit ticker: HP Materials only, grinders are a grind on the kit weapon", [&]() {
      expect_eq(std::string("Use your HP Materials now (Item Pack)."), std::string(SPIRE_KIT_USE_TEXT));
      expect(spire_kit_use_text_due(true, 1, 0, false, false));
      expect(spire_kit_use_text_due(true, 2, 3, false, false));
      expect(!spire_kit_use_text_due(true, 0, 2, false, false)); // atp_grind only: no ticker
      expect(!spire_kit_use_text_due(true, 0, 0, false, false));
      expect(!spire_kit_use_text_due(true, 1, 0, true, false)); // resumed: the kit is not given
      expect(!spire_kit_use_text_due(true, 1, 0, false, true)); // already sent in this run
    });

    b8_case("the draw archetype of a class", [&]() {
      expect_eq(0, spire_draw_archetype_for_class(0)); // HUmar
      expect_eq(0, spire_draw_archetype_for_class(9)); // HUcaseal
      expect_eq(1, spire_draw_archetype_for_class(4)); // RAcast
      expect_eq(1, spire_draw_archetype_for_class(11)); // RAmarl
      expect_eq(2, spire_draw_archetype_for_class(6)); // FOmarl
      expect_eq(2, spire_draw_archetype_for_class(10)); // FOmar
      expect_eq(-1, spire_draw_archetype_for_class(12));
    });

    expect_eq(0u, b8_failed);
  }

  phosg::log_info_f("-- All static tests passed");
}
