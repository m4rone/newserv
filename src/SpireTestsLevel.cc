#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>
#include <phosg/Strings.hh>
#include <phosg/UnitTest.hh>

#include <stdint.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Account.hh"
#include "Channel.hh"
#include "Client.hh"
#include "DataIndex.hh"
#include "GameServer.hh"
#include "Lobby.hh"
#include "Quest.hh"
#include "ServerState.hh"
#include "SpireDraw.hh"
#include "SpireLevel.hh"
#include "SpireMechanics.hh"
#include "SpireMeta.hh"
#include "SpireRewards.hh"
#include "SpireTestSupport.hh"

// The tests of the leveling mechanic (SpireLevel.hh; pso-roguelike new-features/roguelike/design/mechanics/leveling.md).
// Piece 1: the reward at the level the run copy really stands on, the registration on the seam's H2.

namespace {

struct LevelTestState : ServerState {
  LevelTestState() = default;
};
struct LevelTestChannel : Channel {
  LevelTestChannel()
      : Channel(Version::GC_V3, Language::ENGLISH, "static-test", phosg::TerminalFormat::END, phosg::TerminalFormat::END,
            false, false) {}
  virtual std::string default_name() const {
    return "static-test";
  }
  virtual bool connected() const {
    return true;
  }
  virtual void disconnect() {}
  virtual void send_raw(std::string&&) {}
  virtual asio::awaitable<void> recv_raw(void*, size_t) {
    throw std::logic_error("a static test client sends nothing");
    co_return;
  }
};

// Whether a table's text is refused
bool refused(const std::string& json_text) {
  try {
    spire_level_parse(json_text);
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

void spire_tests_leveling() {
  // The mechanic's own registration works in this test file (SpireMechanics.hh, the silence of the static tests)
  spire_mech_test_only({SPIRE_LEVELING_SWITCH});
  auto table = spire_level_parse(phosg::load_file(SPIRE_LEVELING_FILE));
  auto grid = spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE));

  // The table as it ships: on, the four difficulties at Sega's online gates (Lv1, 20, 40, 80), and one row of fifteen
  // rooms for Episode 1
  expect(table.enabled);
  expect(table.rung_levels == (std::array<uint32_t, 4>{0, 19, 39, 79}));
  expect_eq(1u, table.room_end_levels.size());
  const auto& ends = table.room_end_levels.at(1);
  expect_eq(static_cast<size_t>(SPIRE_LEVEL_EPISODE_ROOMS), ends.size());
  expect_eq(SPIRE_LEVEL_EPISODE_ROOMS + 1, static_cast<uint32_t>(spire_meta_episode_first_room(2)));
  expect_eq(1, spire_level_episode_of_room(1));
  expect_eq(1, spire_level_episode_of_room(15));
  expect_eq(2, spire_level_episode_of_room(16));
  // The row is the curve the core plays: the end level of room k is the level floor of room k + 1 (SpireMeta.hh
  // spire_room_level_floor, 0-based, known for rooms 1-8), and the run starts at Lv1
  expect_eq(0u, spire_room_level_floor(1, 1));
  for (uint8_t k = 1; k <= 7; k++) {
    expect_eq(spire_room_level_floor(1, k + 1) + 1, ends[k - 1]);
  }
  // and the column the reward grid computed its weapon levels on: at the first room of every W the end level is the
  // grid's run level of that W, for every class and line. So a copy offered the W of a room has at least the level the
  // grid computed that weapon for.
  size_t run_levels = 0;
  for (const auto& [w, first_room] : spire_test_first_room_of_w(grid)) {
    for (const auto& [char_class, cls] : grid.classes) {
      for (const auto& [line_name, line] : cls.lines) {
        auto w_it = line.w.find(w);
        if (w_it != line.w.end()) {
          expect_eq(w_it->second.run_level, ends.at(first_room - 1));
          run_levels++;
        }
      }
    }
  }
  expect(run_levels >= 12 * 5);

  // On the curve nothing moves, on every difficulty: a copy with the room's end level, or with less, is offered the
  // quest's own room, never an earlier one
  for (uint8_t rung = 0; rung < 4; rung++) {
    uint32_t r = table.rung_levels[rung];
    for (uint32_t k = 1; k <= SPIRE_LEVEL_EPISODE_ROOMS; k++) {
      expect_eq(k, spire_level_reward_room_index(table, k, k, r + ends[k - 1], rung));
      expect_eq(k, spire_level_reward_room_index(table, k, k, r + 1, rung));
      expect_eq(k, spire_level_reward_room_index(table, k, k, 1, rung)); // below the rung's own levels
    }
  }
  // Ahead of the curve the offer follows the level. Bought start levels, as the run grows today (design par. 2, the
  // column "N+14 vandaag": Lv15 through the forest): the first room offers the weapon level of De Rol Le's room.
  expect_eq(8u, spire_level_reward_room_index(table, 1, 1, 15, 0));
  expect(spire_reward_w(grid, 8) > spire_reward_w(grid, 1));
  expect_eq(1u, spire_level_reward_room_index(table, 1, 1, 4, 0));
  expect_eq(3u, spire_level_reward_room_index(table, 1, 1, 9, 0));
  expect_eq(10u, spire_level_reward_room_index(table, 1, 1, 18, 0));
  expect_eq(14u, spire_level_reward_room_index(table, 1, 1, 23, 0));
  expect_eq(15u, spire_level_reward_room_index(table, 1, 1, 24, 0)); // the ladder's top: the last room
  expect_eq(15u, spire_level_reward_room_index(table, 1, 1, 200, 0));
  // one level ahead moves a room only where the next room's end level is one level on
  expect_eq(5u, spire_level_reward_room_index(table, 5, 5, 11, 0));
  expect_eq(6u, spire_level_reward_room_index(table, 5, 5, 12, 0));
  expect_eq(7u, spire_level_reward_room_index(table, 7, 7, 14, 0));
  // The difficulty's own levels do not count: Lv23 on Hard is the end of room 1, Lv34 on Hard is Lv15 on Normal's
  // scale, and the same Lv34 on Normal is past the last room
  expect_eq(1u, spire_level_reward_room_index(table, 1, 1, 23, 1));
  expect_eq(8u, spire_level_reward_room_index(table, 1, 1, 34, 1));
  expect_eq(15u, spire_level_reward_room_index(table, 1, 1, 34, 0));
  expect_eq(15u, spire_level_reward_room_index(table, 1, 1, 103, 3));
  expect_eq(3u, spire_level_reward_room_index(table, 3, 3, 80, 3));
  expect_eq(15u, spire_level_key(table, 34, 1));
  expect_eq(0u, spire_level_key(table, 19, 1));
  // what a registrant before this one left is never lowered
  expect_eq(12u, spire_level_reward_room_index(table, 3, 12, 9, 0));
  // Every level on every difficulty: never below the quest's room, never past the row, never falling with the level,
  // and a later room only when the copy has that room's end level
  for (uint8_t rung = 0; rung < 4; rung++) {
    for (uint32_t k = 1; k <= SPIRE_LEVEL_EPISODE_ROOMS; k++) {
      uint32_t before = k;
      for (uint32_t level = 1; level <= 200; level++) {
        uint32_t index = spire_level_reward_room_index(table, k, k, level, rung);
        expect((index >= k) && (index <= SPIRE_LEVEL_EPISODE_ROOMS) && (index >= before));
        if (index > k) {
          expect(ends[index - 1] <= spire_level_key(table, level, rung));
        }
        before = index;
      }
      expect_eq(SPIRE_LEVEL_EPISODE_ROOMS, before); // Lv200 reaches the last room on every difficulty
    }
  }
  // Off, and an episode without a row (Episode 2: rooms k16-k30, room index 1-15): the quest's own room
  {
    auto off = table;
    off.enabled = false;
    for (uint32_t k = 1; k <= 2 * SPIRE_LEVEL_EPISODE_ROOMS; k++) {
      uint32_t own = spire_reward_room_index(k);
      expect_eq(own, spire_level_reward_room_index(off, k, own, 200, 0));
      if (k > SPIRE_LEVEL_EPISODE_ROOMS) {
        expect_eq(own, spire_level_reward_room_index(table, k, own, 200, 0));
      }
    }
  }

  // The parse is strict about what it reads and leaves a key of a later piece alone
  expect(!refused(R"({"Enabled": false, "RungLevels": [0, 19, 39, 79], "Episodes": {}})"));
  {
    auto small = spire_level_parse(
        R"({"Enabled": true, "Later": {"x": 1}, "RungLevels": [0, 5, 5, 9], "Episodes": {"2": {"RoomEndLevels": [3, 3, 7]}}})");
    expect(small.enabled && (small.rung_levels[3] == 9) && (small.room_end_levels.at(2).size() == 3));
    expect_eq(1u, spire_level_reward_room_index(small, 1, 1, 200, 0)); // no row for Episode 1
    expect_eq(3u, spire_level_reward_room_index(small, 16, 1, 7, 0));
    expect_eq(2u, spire_level_reward_room_index(small, 16, 1, 3, 0)); // two rooms end at Lv3: the last of them
  }
  expect(refused("{"));
  expect(refused(R"({"RungLevels": [0, 19, 39, 79], "Episodes": {}})")); // no Enabled
  expect(refused(R"({"Enabled": true, "Episodes": {}})")); // no RungLevels
  expect(refused(R"({"Enabled": true, "RungLevels": [0, 19, 39, 79]})")); // no Episodes
  expect(refused(R"({"Enabled": true, "RungLevels": [0, 19, 39], "Episodes": {}})"));
  expect(refused(R"({"Enabled": true, "RungLevels": [1, 19, 39, 79], "Episodes": {}})")); // Normal's is 0
  expect(refused(R"({"Enabled": true, "RungLevels": [0, 39, 19, 79], "Episodes": {}})")); // falling
  expect(refused(R"({"Enabled": true, "RungLevels": [0, 19, 39, 200], "Episodes": {}})"));
  expect(refused(R"({"Enabled": true, "RungLevels": [0, 19, 39, 79], "Episodes": {"3": {"RoomEndLevels": [4]}}})"));
  expect(refused(R"({"Enabled": true, "RungLevels": [0, 19, 39, 79], "Episodes": {"1": {}}})"));
  expect(refused(R"({"Enabled": true, "RungLevels": [0, 19, 39, 79], "Episodes": {"1": {"RoomEndLevels": []}}})"));
  expect(refused(R"({"Enabled": true, "RungLevels": [0, 19, 39, 79], "Episodes": {"1": {"RoomEndLevels": [6, 4]}}})"));
  expect(refused(R"({"Enabled": true, "RungLevels": [0, 19, 39, 79], "Episodes": {"1": {"RoomEndLevels": [0]}}})"));
  expect(refused(R"({"Enabled": true, "RungLevels": [0, 19, 39, 79], "Episodes": {"1": {"RoomEndLevels": [201]}}})"));
  expect(refused(R"({"Enabled": true, "RungLevels": [0, 19, 39, 79], "Episodes": {"1": {"RoomEndLevels": )"
                 R"([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]}}})")); // a sixteenth room

  // The registration: on H2 under the mechanic's name, and nothing registered twice
  {
    auto names = spire_mech_hook_names(SpireMechHookId::REWARD_ROOM);
    expect(std::find(names.begin(), names.end(), "leveling") != names.end());
    expect(spire_mech_hook_conflicts().empty());
  }

  // Through the seam's own call, as the bridge makes it at every offer (SpireFlagBridge.cc spire_reward_make_offer).
  // The switches and the table come from files of this test.
  auto dir = std::filesystem::temp_directory_path() / std::format("newserv-static-spire-level-{}", getpid());
  std::filesystem::create_directories(dir);
  std::string switches_file = (dir / "spire-schakelaars.json").string();
  std::string table_file = (dir / "spire-leveling.json").string();
  auto set_switches = [&](const std::string& text) {
    phosg::save_file(switches_file, text);
    spire_mech_set_switches_file(switches_file); // forgets what was read: the next question reads the file
  };
  set_switches("{}");

  constexpr uint32_t SERIAL = 4711, NORMAL_QUEST = 987711, HARD_QUEST = 981;
  expect_eq(0, spire_meta_ladder_quest_rung(NORMAL_QUEST));
  expect_eq(1, spire_meta_ladder_quest_rung(HARD_QUEST));
  auto state = std::make_shared<LevelTestState>();
  state->data = std::make_shared<DataIndex>();
  state->io_context = std::make_shared<asio::io_context>(1);
  auto server = std::make_shared<GameServer>(state);
  auto game_with = [&](uint32_t lobby_id, uint32_t quest_number) {
    auto l = std::make_shared<Lobby>(state, lobby_id, true);
    auto vq = std::make_shared<VersionedQuest>();
    vq->meta.quest_number = quest_number;
    l->quest = std::make_shared<Quest>(vq);
    return l;
  };
  auto normal_l = game_with(0x7E01, NORMAL_QUEST);
  auto hard_l = game_with(0x7E02, HARD_QUEST);
  auto c = std::make_shared<Client>(server, std::make_shared<LevelTestChannel>(), ServerBehavior::GAME_SERVER);
  auto login = std::make_shared<Login>();
  login->account = std::make_shared<Account>();
  login->gc_license = std::make_shared<GCLicense>();
  login->gc_license->serial_number = SERIAL;
  c->login = login;
  c->character_file(true, false); // a GC client's character data: an empty file, nothing is read

  SpireDrawEntry entry;
  // The room index of an offer of room k for the client's run copy at a level (1-based); the rung comes back as it went
  auto offer_room = [&](std::shared_ptr<const Lobby> l, uint32_t k, uint32_t level) -> uint32_t {
    if (c->has_overlay()) {
      c->character_file(false, true)->disp.stats.level = level - 1;
    }
    uint32_t room_index = spire_reward_room_index(k), trede = 1;
    spire_mech_reward_room({c, l, SERIAL, grid, entry, k, false}, room_index, trede);
    expect_eq(1u, trede);
    return room_index;
  };
  // without a client (the seam's own tests call the hook so) and without a run copy: the quest's own room
  {
    std::shared_ptr<Client> no_c;
    std::shared_ptr<Lobby> no_l;
    uint32_t room_index = 1, trede = 0;
    spire_mech_reward_room({no_c, no_l, SERIAL, grid, entry, 1, false}, room_index, trede);
    expect((room_index == 1) && (trede == 0));
  }
  expect_eq(1u, offer_room(normal_l, 1, 15));
  // with the run copy: its level, less the levels of the quest's difficulty
  c->create_battle_overlay(std::make_shared<BattleRules>(), nullptr);
  expect(c->has_overlay());
  expect_eq(8u, offer_room(normal_l, 1, 15));
  expect_eq(1u, offer_room(normal_l, 1, 4));
  expect_eq(9u, offer_room(normal_l, 9, 15)); // never an earlier room than the quest's
  expect_eq(1u, offer_room(hard_l, 1, 23));
  expect_eq(8u, offer_room(hard_l, 1, 34));
  expect_eq(15u, offer_room(normal_l, 1, 34));
  // the last room of the row is a room the grid knows: the core's bound leaves it
  expect_eq(SPIRE_LEVEL_EPISODE_ROOMS, std::max<uint32_t>(grid.room_w.rbegin()->first, grid.disk_levels.rbegin()->first));
  expect_eq(SPIRE_LEVEL_EPISODE_ROOMS, offer_room(normal_l, 1, 200));
  expect_eq(1u, offer_room(normal_l, 16, 200)); // Episode 2: no row
  // the switch off: the quest's own room; on again: the level's
  set_switches(R"({"leveling": false})");
  expect_eq(1u, offer_room(normal_l, 1, 15));
  set_switches(R"({"leveling": true})");
  expect_eq(8u, offer_room(normal_l, 1, 15));
  set_switches("{}");
  expect_eq(8u, offer_room(normal_l, 1, 15));
  // the table's own Enabled false, a broken table and a missing one: the quest's own room, and no throw
  spire_level_set_table_file(table_file);
  phosg::save_file(table_file,
      R"({"Enabled": false, "RungLevels": [0, 19, 39, 79], "Episodes": {"1": {"RoomEndLevels": [4, 6, 8]}}})");
  expect_eq(1u, offer_room(normal_l, 1, 15));
  phosg::save_file(table_file,
      R"({"Enabled": true, "RungLevels": [0, 19, 39, 79], "Episodes": {"1": {"RoomEndLevels": [4, 6, 8]}}})");
  expect_eq(3u, offer_room(normal_l, 1, 15)); // the file is read at every offer
  phosg::save_file(table_file, "{");
  expect_eq(1u, offer_room(normal_l, 1, 15));
  std::filesystem::remove(table_file);
  expect_eq(1u, offer_room(normal_l, 1, 15));
  spire_level_set_table_file("");
  expect_eq(8u, offer_room(normal_l, 1, 15));

  spire_mech_set_switches_file("");
  std::filesystem::remove_all(dir);
  phosg::log_info_f("   leveling: {} grid run levels on the table's row, 4 x 15 x 200 levels checked", run_levels);
}

[[maybe_unused]] const bool registered = spire_mech_register_tests("leveling", spire_tests_leveling);

} // namespace
