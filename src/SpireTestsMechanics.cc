#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>
#include <phosg/Strings.hh>
#include <phosg/UnitTest.hh>

#include <stdint.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ReceiveSubcommands.hh"
#include "SpireDraw.hh"
#include "SpireFlagBridge.hh"
#include "SpireMechanics.hh"
#include "SpireMeta.hh"
#include "SpireRewards.hh"

// The tests of the seam's own rules (SpireMechanics.hh; docs/review-loop/goals/naad.md point 2): the table of reward
// kinds, the switches and the hooks H1-H11. What a kind gives is tested in its owner's file (SpireTestsAttack.cc and the others) and
// pinned by the seam's fingerprint (SpireTestsSeam.cc).
//
// "No offer" is a throw of spire_reward_offer here. At the bridge every throw of an offer ends in the entry's own item
// (SpireFlagBridge.cc spire_reward_make_offer catches it; the 'Q' leaves the slot to the quest and the pick answers
// own_item): that path needs a client and is not played here.

namespace {

// A kind of this file only, registered while the test runs: a new kind reads its own section, keeps it, and makes its
// offer from what the core hands it, without a line in the core
constexpr SpireRewardKind TEST_KIND = static_cast<SpireRewardKind>(250);
constexpr const char* TEST_KEY = "test.naad";
constexpr const char* TEST_FIRST_KEY = "test.naad.eerst";
struct TestSection {
  int64_t levels = 0;
};

void test_parse(const phosg::JSON& json, SpireRewardTable& ret) {
  auto section = std::make_shared<TestSection>();
  section->levels = json.at("MagFood").get_int("Rare"); // any key of the grid: the kind gets the grid's JSON itself
  ret.sections[TEST_KEY] = section;
}

std::optional<SpireRewardOffer> test_offer(const SpireRewardOfferInput& in, SpireRewardOffer ret) {
  auto section = std::static_pointer_cast<const TestSection>(in.table.sections.at(TEST_KEY));
  ret.food_levels = section->levels;
  ret.label = in.tier_label + " test";
  return ret;
}

// The route asked before the kind: only class 2 is its own (as an android would be the android route's)
std::optional<SpireRewardOffer> test_first_offer(const SpireRewardOfferInput& in, SpireRewardOffer ret) {
  if (in.char_class != 2) {
    return std::nullopt;
  }
  ret.label = in.tier_label + " first";
  return ret;
}

std::string offer_text(const SpireRewardOffer& o) {
  return std::format("{} {} {} {} {} {}", static_cast<int>(o.kind), o.label, spire_run_mag_bytes_hex(o.item), o.line, o.w,
      o.food_levels);
}

// The reason an offer is not made, "" when it is
template <typename FnT>
std::string no_offer(FnT&& fn) {
  try {
    fn();
  } catch (const std::runtime_error& e) {
    return e.what();
  }
  return "";
}

// --- The hooks H1-H11 ---
//
// Per hook three registrants of this file: 'L' ("aa", number 20, switch test.haak.aan) is registered first, then 'E'
// ("zz", number 10, no switch), then 'O' ("mm", number 15, switch test.haak.uit). The order of the calls is E, O, L:
// by number, not by name (aa, mm, zz) and not by registration (L, E, O). A hook is called with no client and no game:
// a registrant of this file reads neither.

std::string hook_calls; // one letter per call, in the order of the calls

// A registration of a test goes when the test leaves it, also on a failed expectation
struct HookGuard {
  SpireMechHookId hook;
  const char* name;
  ~HookGuard() {
    spire_mech_forget_hook(hook, name);
  }
};

template <char TAG>
std::shared_ptr<const std::string> t_quest_bin(std::shared_ptr<Client>, std::shared_ptr<const Lobby>,
    std::shared_ptr<const std::string> bin) {
  hook_calls += TAG;
  return std::make_shared<const std::string>(*bin + TAG);
}
std::shared_ptr<const std::string> t_quest_bin_null(std::shared_ptr<Client>, std::shared_ptr<const Lobby>,
    std::shared_ptr<const std::string>) {
  hook_calls += 'n';
  return nullptr;
}
template <char TAG>
void t_reward_room(const SpireMechRewardRoom& at, uint32_t& room_index, uint32_t& trede) {
  hook_calls += TAG;
  room_index = (room_index * 100) + TAG;
  trede += at.k;
}
void t_reward_room_throws(const SpireMechRewardRoom&, uint32_t& room_index, uint32_t& trede) {
  hook_calls += 't';
  room_index = 999;
  trede = 999;
  throw std::runtime_error("a registrant of the test that fails");
}
uint32_t type_weight_target = 0; // t_type_weight<'E'>: the one candidate value that keeps its weight (0: all do)
std::vector<uint32_t> type_weight_seen; // every candidate value t_type_weight<'E'> was asked about
template <char TAG>
uint32_t t_type_weight(const SpireMechTypeWeight& tw) {
  hook_calls += TAG;
  if (TAG != 'E') {
    return 1;
  }
  type_weight_seen.emplace_back(tw.value);
  return (!type_weight_target || (tw.value == type_weight_target)) ? 1 : 0;
}
uint32_t t_type_weight_three(const SpireMechTypeWeight& tw) {
  return (tw.value == 2) ? 3 : 1;
}
template <char TAG>
void t_player_hp(std::shared_ptr<Client>, std::shared_ptr<Lobby>, uint8_t subcommand) {
  hook_calls += TAG;
  hook_calls += std::format("{:02X}", subcommand);
}
template <char TAG>
void t_player_died(std::shared_ptr<Client>, std::shared_ptr<Lobby>) {
  hook_calls += TAG;
}
void t_player_died_throws(std::shared_ptr<Client>, std::shared_ptr<Lobby>) {
  hook_calls += 't';
  throw std::runtime_error("a registrant of the test that fails");
}
template <char TAG>
void t_tech_done(std::shared_ptr<Client>, std::shared_ptr<Lobby>, uint32_t technique, uint32_t level) {
  hook_calls += TAG;
  hook_calls += std::format("{}.{}", technique, level);
}
template <char TAG>
void t_enemy_killed(std::shared_ptr<Client>, std::shared_ptr<Lobby>, const DropReconcileResult& rec, uint8_t floor) {
  hook_calls += TAG;
  hook_calls += std::format("{}.{}", floor, rec.should_drop ? 1 : 0);
}
template <char TAG>
void t_wave_start(std::shared_ptr<Client>, std::shared_ptr<const Lobby>, uint32_t serial_number, uint32_t quest_number,
    uint32_t a, uint32_t b) {
  hook_calls += TAG;
  hook_calls += std::format("{}.{}.{}.{}", serial_number, quest_number, a, b);
}
template <char TAG>
void t_run_end(std::shared_ptr<Client>, std::shared_ptr<const Lobby>, SpireMechRunEnd why) {
  hook_calls += TAG;
  hook_calls += std::format("{}", static_cast<uint32_t>(why));
}
// H10 and H11: a registrant carries its own key, from the text into the struct's sections and back
template <char TAG>
void t_run_state_capture(std::shared_ptr<Client>, std::shared_ptr<const Lobby>, SpireSavestate& ss) {
  hook_calls += TAG;
  hook_calls += 'c';
  ss.sections[std::string("Test") + TAG] = std::make_shared<const int64_t>(ss.run_id + TAG);
}
template <char TAG>
void t_run_state_write(const SpireSavestate& ss, phosg::JSON& json) {
  hook_calls += TAG;
  hook_calls += 'w';
  auto it = ss.sections.find(std::string("Test") + TAG);
  if (it != ss.sections.end()) {
    json.emplace(std::string("Test") + TAG, *std::static_pointer_cast<const int64_t>(it->second));
  }
}
template <char TAG>
void t_run_state_read(const phosg::JSON& json, SpireSavestate& ret) {
  hook_calls += TAG;
  hook_calls += 'r';
  if (json.contains(std::string("Test") + TAG)) {
    ret.sections[std::string("Test") + TAG] = std::make_shared<const int64_t>(json.get_int(std::string("Test") + TAG));
  }
}
template <char TAG>
void t_run_state_resume(std::shared_ptr<Client>, std::shared_ptr<const Lobby>, const SpireSavestate& ss) {
  hook_calls += TAG;
  hook_calls += 's';
  hook_calls += std::format("{}", ss.sections.count(std::string("Test") + TAG));
}
template <char TAG>
constexpr SpireMechRunState T_RUN_STATE = {t_run_state_capture<TAG>, t_run_state_write<TAG>, t_run_state_read<TAG>,
    t_run_state_resume<TAG>};
template <char TAG>
void t_char_meta_read(const phosg::JSON& json, SpireMetaFile& ret) {
  hook_calls += TAG;
  hook_calls += 'r';
  if (json.contains(std::string("Test") + TAG)) {
    ret.sections[std::string("Test") + TAG] = std::make_shared<const int64_t>(json.get_int(std::string("Test") + TAG));
  }
}
template <char TAG>
void t_char_meta_write(const SpireMetaFile& file, phosg::JSON& json) {
  hook_calls += TAG;
  hook_calls += 'w';
  auto it = file.sections.find(std::string("Test") + TAG);
  if (it != file.sections.end()) {
    json.emplace(std::string("Test") + TAG, *std::static_pointer_cast<const int64_t>(it->second));
  }
}
template <char TAG>
constexpr SpireMechCharMeta T_CHAR_META = {t_char_meta_read<TAG>, t_char_meta_write<TAG>};

constexpr const char* HOOK_KEY_ON = "test.haak.aan";
constexpr const char* HOOK_KEY_OFF = "test.haak.uit";

// What every hook has in common. call makes the hook's one call of the core and answers what the registrants were
// handed and did, as text; none is that text without a registrant, and the three others are it for the calls E L,
// E O L and E. write writes the switches file.
template <typename FnT, typename RegisterT>
void expect_hook(SpireMechHookId hook, RegisterT&& reg, FnT late, FnT early, FnT off,
    const std::function<std::string()>& call, const std::function<void(const std::string&)>& write,
    const std::string& none, const std::string& e_l, const std::string& e_o_l, const std::string& e) {
  auto called = [&]() {
    hook_calls.clear();
    return call();
  };
  const auto names_before = spire_mech_hook_names(hook);
  write("{}");
  expect_eq(none, called()); // without a registrant: what the core had before the seam
  {
    HookGuard g1{hook, "aa"}, g2{hook, "zz"}, g3{hook, "mm"};
    reg(SpireMechHook<FnT>{"aa", HOOK_KEY_ON, 20, late});
    reg(SpireMechHook<FnT>{"zz", "", 10, early});
    reg(SpireMechHook<FnT>{"mm", HOOK_KEY_OFF, 15, off});
    expect(spire_mech_hook_conflicts().empty());
    std::vector<std::string> own;
    for (const auto& name : spire_mech_hook_names(hook)) {
      if ((name == "aa") || (name == "zz") || (name == "mm")) {
        own.emplace_back(name);
      }
    }
    expect(own == std::vector<std::string>({"zz", "mm", "aa"})); // by number
    write(R"({"test.haak.uit": false})");
    expect_eq(e_l, called()); // a registrant whose switch is off is not called
    write(R"({"test.haak.uit": true})");
    expect_eq(e_o_l, called()); // the fixed order
    write("{}");
    expect_eq(e_o_l, called()); // a missing key is on
    write(R"({"test.haak.aan": false, "test.haak.uit": false})");
    expect_eq(e, called()); // an empty switch key is no switch
    // a name registered twice on a hook keeps the first registration and is named
    reg(SpireMechHook<FnT>{"zz", "", 99, late});
    expect_eq(1u, spire_mech_hook_conflicts().size());
    expect_eq(e, called());
  }
  // nothing is left behind: the hook is as it was, for the tests after this one and for the server
  expect(spire_mech_hook_conflicts().empty());
  expect(names_before == spire_mech_hook_names(hook));
  write("{}");
  expect_eq(none, called());
}

void spire_tests_hooks() {
  using H = SpireMechHookId;
  auto dir = std::filesystem::temp_directory_path() / std::format("newserv-static-spire-hooks-{}", getpid());
  std::filesystem::create_directories(dir);
  std::string file = (dir / "spire-schakelaars.json").string();
  uint32_t writes = 0;
  std::function<void(const std::string&)> write = [&](const std::string& text) {
    phosg::save_file(file, text);
    std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(++writes));
  };
  spire_mech_set_switches_file(file);
  expect(spire_mech_hook_conflicts().empty());
  std::shared_ptr<Client> no_c;
  std::shared_ptr<Lobby> no_l;

  // Who is registered on the server: the run mag's savestate key on H10 (SpireMagState.cc), and nobody else anywhere
  for (uint8_t z = 1; z <= 11; z++) {
    auto names = spire_mech_hook_names(static_cast<H>(z));
    expect(names == ((z == 10) ? std::vector<std::string>({"mags"}) : std::vector<std::string>()));
  }

  // H1 quest_bin: without a registrant the client gets the very .bin the core handed in; each registrant gets what the
  // one before it answered
  auto bin = std::make_shared<const std::string>("bin");
  expect(spire_mech_quest_bin(no_c, no_l, bin) == bin);
  expect_hook<SpireMechQuestBinFn>(
      H::QUEST_BIN, spire_mech_register_quest_bin, t_quest_bin<'L'>, t_quest_bin<'E'>, t_quest_bin<'O'>,
      [&]() {
        auto out = spire_mech_quest_bin(no_c, no_l, bin);
        return hook_calls + " " + *out;
      },
      write, " bin", "EL binEL", "EOL binEOL", "E binE");
  {
    HookGuard g{H::QUEST_BIN, "nul"};
    spire_mech_register_quest_bin({"nul", "", 1, t_quest_bin_null});
    hook_calls.clear();
    expect(spire_mech_quest_bin(no_c, no_l, bin) == bin); // a registrant that answers nullptr leaves the .bin
    expect_eq(std::string("n"), hook_calls);
  }

  // H2 reward_room: without a registrant the room index and the rung stay the quest's; a registrant that fails leaves
  // them as they were before it
  SpireDrawEntry entry;
  auto room_call = [&]() {
    uint32_t room_index = 3, trede = 1;
    spire_mech_reward_room({no_c, no_l, 7, entry, 5, false}, room_index, trede);
    return std::format("{} {} {}", hook_calls, room_index, trede);
  };
  expect_hook<SpireMechRewardRoomFn>(
      H::REWARD_ROOM, spire_mech_register_reward_room, t_reward_room<'L'>, t_reward_room<'E'>, t_reward_room<'O'>,
      room_call, write, " 3 1", std::format("EL {} 11", ((3 * 100 + 'E') * 100) + 'L'),
      std::format("EOL {} 16", ((((3 * 100 + 'E') * 100) + 'O') * 100) + 'L'), std::format("E {} 6", 3 * 100 + 'E'));
  {
    HookGuard g1{H::REWARD_ROOM, "stuk"}, g2{H::REWARD_ROOM, "na"};
    spire_mech_register_reward_room({"stuk", "", 1, t_reward_room_throws});
    spire_mech_register_reward_room({"na", "", 2, t_reward_room<'E'>});
    hook_calls.clear();
    expect_eq(std::format("tE {} 6", 3 * 100 + 'E'), room_call());
  }

  // H3 type_weight, the hook alone: without a registrant one draw rnd(n) and no candidate is asked; with registrants
  // every candidate is asked once, by each of them in their order, and weights that are all 1 give the same draw
  std::vector<uint32_t> asked;
  std::function<uint32_t(uint32_t)> rnd = [&](uint32_t n) -> uint32_t {
    asked.emplace_back(n);
    return 6; // the draw the kinds reduce with % n
  };
  size_t candidates_asked = 0;
  std::function<SpireMechTypeWeight(size_t)> candidate = [&](size_t z) {
    candidates_asked++;
    return SpireMechTypeWeight{nullptr, SpireMechCandidate::UNIT, nullptr, static_cast<uint32_t>(z), 0};
  };
  auto weight_call = [&]() {
    asked.clear();
    candidates_asked = 0;
    size_t z = spire_mech_type_weight_pick(4, rnd, candidate);
    return std::format("{} {} {} {}", hook_calls, z, asked.size() == 1 ? asked[0] : 999, candidates_asked);
  };
  type_weight_target = 0;
  expect_hook<SpireMechTypeWeightFn>(
      H::TYPE_WEIGHT, spire_mech_register_type_weight, t_type_weight<'L'>, t_type_weight<'E'>, t_type_weight<'O'>,
      weight_call, write, " 2 4 0", "ELELELEL 2 4 4", "EOLEOLEOLEOL 2 4 4", "EEEE 2 4 4");
  {
    HookGuard g1{H::TYPE_WEIGHT, "drie"}, g2{H::TYPE_WEIGHT, "een"};
    spire_mech_register_type_weight({"drie", "", 1, t_type_weight_three});
    // candidate 2 weighs 3 of 6: the draw is rnd(6) and 6 % 6 = 0 falls on candidate 0, 2-4 on candidate 2
    expect_eq(std::string(" 0 6 4"), (hook_calls.clear(), weight_call()));
    std::function<uint32_t(uint32_t)> rnd4 = [](uint32_t) -> uint32_t { return 4; };
    expect_eq(2u, spire_mech_type_weight_pick(4, rnd4, candidate));
    std::function<uint32_t(uint32_t)> rnd5 = [](uint32_t) -> uint32_t { return 5; };
    expect_eq(3u, spire_mech_type_weight_pick(4, rnd5, candidate));
    // the weights of two registrants multiply: only candidate 2 keeps a weight, whatever the draw
    type_weight_target = 2;
    spire_mech_register_type_weight({"een", "", 2, t_type_weight<'E'>});
    expect_eq(2u, spire_mech_type_weight_pick(4, rnd5, candidate));
    expect_eq(2u, spire_mech_type_weight_pick(4, rnd, candidate));
    // every weight 0: the plain draw again
    type_weight_target = 99;
    expect_eq(std::string("EEEE 2 4 4"), (hook_calls.clear(), weight_call()));
    type_weight_target = 0;
  }

  // H3 through the kinds that choose (a unit, another line): with a registrant whose weights are all 1, and with one
  // whose switch is off, every draw and every offer is the one without a registrant
  auto rewards = spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE));
  auto offers_text = [&]() {
    std::string out;
    for (SpireRewardKind kind : {SpireRewardKind::UNIT, SpireRewardKind::NEW_LINE}) {
      for (uint8_t char_class = 0; char_class < 12; char_class++) {
        for (uint32_t tier = 0; tier < 4; tier++) {
          uint64_t state = (static_cast<uint64_t>(kind) << 16) | (char_class << 8) | tier;
          std::function<uint32_t(uint32_t)> stream = [&](uint32_t n) -> uint32_t {
            state = (state * 6364136223846793005ULL) + 1442695040888963407ULL;
            uint32_t v = n ? static_cast<uint32_t>((state >> 33) % n) : 0;
            out += std::format("[{} {}]", n, v);
            return v;
          };
          try {
            auto o = spire_reward_offer(rewards, kind, char_class, nullptr, 7, static_cast<SpireRewardTier>(tier), stream);
            out += offer_text(o) + "\n";
          } catch (const std::runtime_error& e) {
            out += std::string("THROW ") + e.what() + "\n";
          }
        }
      }
    }
    return out;
  };
  write("{}");
  const std::string offers_before = offers_text();
  expect(offers_before.find("[") != std::string::npos); // the offers do draw
  {
    HookGuard g1{H::TYPE_WEIGHT, "aa"}, g2{H::TYPE_WEIGHT, "mm"};
    type_weight_target = 0;
    type_weight_seen.clear();
    spire_mech_register_type_weight({"mm", HOOK_KEY_OFF, 15, t_type_weight<'E'>});
    write(R"({"test.haak.uit": false})");
    expect_eq(offers_before, offers_text());
    expect(type_weight_seen.empty()); // off: not asked
    write(R"({"test.haak.uit": true})");
    expect_eq(offers_before, offers_text());
    expect(!type_weight_seen.empty()); // on: asked, and nothing moved
    // and a registrant that leaves one unit its weight: that unit is the offer, whatever the draw
    auto first = [](uint32_t) -> uint32_t { return 0; };
    auto unit_code = [&]() {
      auto o = spire_reward_offer(rewards, SpireRewardKind::UNIT, 0, nullptr, 7, SpireRewardTier::RARE, first);
      return (static_cast<uint32_t>(o.item[0]) << 16) | (static_cast<uint32_t>(o.item[1]) << 8) | o.item[2];
    };
    type_weight_seen.clear();
    uint32_t plain = unit_code();
    expect(type_weight_seen.size() >= 2); // the class has more than one unit to choose from in this tier
    expect_eq(type_weight_seen.front(), plain);
    uint32_t other = type_weight_seen.back();
    expect(other != plain);
    type_weight_target = other;
    expect_eq(other, unit_code());
    type_weight_target = 0;
  }
  expect_eq(offers_before, offers_text());

  // H4 player_hp: only 6x4B and 6x4C are a hit; 6x4A and 6x2F share the handler and reach no registrant
  expect_hook<SpireMechPlayerHPFn>(
      H::PLAYER_HP, spire_mech_register_player_hp, t_player_hp<'L'>, t_player_hp<'E'>, t_player_hp<'O'>,
      [&]() {
        spire_mech_player_hp(no_c, no_l, 0x4A);
        spire_mech_player_hp(no_c, no_l, 0x2F);
        spire_mech_player_hp(no_c, no_l, 0x4B);
        spire_mech_player_hp(no_c, no_l, 0x4C);
        return hook_calls;
      },
      write, "", "E4BL4BE4CL4C", "E4BO4BL4BE4CO4CL4C", "E4BE4C");

  // H5 player_died; a registrant that fails does not keep the next one from its call
  expect_hook<SpireMechPlayerDiedFn>(
      H::PLAYER_DIED, spire_mech_register_player_died, t_player_died<'L'>, t_player_died<'E'>, t_player_died<'O'>,
      [&]() {
        spire_mech_player_died(no_c, no_l);
        return hook_calls;
      },
      write, "", "EL", "EOL", "E");
  {
    HookGuard g1{H::PLAYER_DIED, "stuk"}, g2{H::PLAYER_DIED, "na"};
    spire_mech_register_player_died({"stuk", "", 1, t_player_died_throws});
    spire_mech_register_player_died({"na", "", 2, t_player_died<'E'>});
    hook_calls.clear();
    spire_mech_player_died(no_c, no_l);
    expect_eq(std::string("tE"), hook_calls);
  }

  // H6 tech_done: the technique and its level
  expect_hook<SpireMechTechDoneFn>(
      H::TECH_DONE, spire_mech_register_tech_done, t_tech_done<'L'>, t_tech_done<'E'>, t_tech_done<'O'>,
      [&]() {
        spire_mech_tech_done(no_c, no_l, 15, 4);
        return hook_calls;
      },
      write, "", "E15.4L15.4", "E15.4O15.4L15.4", "E15.4");

  // H7 enemy_killed: the reconciled drop request and its floor; the request of a box reaches no registrant
  DropReconcileResult box;
  DropReconcileResult rec;
  rec.target_ene_st = std::make_shared<MapState::EnemyState>();
  rec.should_drop = false;
  expect_hook<SpireMechEnemyKilledFn>(
      H::ENEMY_KILLED, spire_mech_register_enemy_killed, t_enemy_killed<'L'>, t_enemy_killed<'E'>, t_enemy_killed<'O'>,
      [&]() {
        spire_mech_enemy_killed(no_c, no_l, box, 2);
        spire_mech_enemy_killed(no_c, no_l, rec, 3);
        return hook_calls;
      },
      write, "", "E3.0L3.0", "E3.0O3.0L3.0", "E3.0");

  // H8 wave_start: wave a of b
  expect_hook<SpireMechWaveStartFn>(
      H::WAVE_START, spire_mech_register_wave_start, t_wave_start<'L'>, t_wave_start<'E'>, t_wave_start<'O'>,
      [&]() {
        spire_mech_wave_start(no_c, no_l, 42, 929, 1, 3);
        return hook_calls;
      },
      write, "", "E42.929.1.3L42.929.1.3", "E42.929.1.3O42.929.1.3L42.929.1.3", "E42.929.1.3");

  // H9 run_end: which of the three ends
  expect_hook<SpireMechRunEndFn>(
      H::RUN_END, spire_mech_register_run_end, t_run_end<'L'>, t_run_end<'E'>, t_run_end<'O'>,
      [&]() {
        spire_mech_run_end(no_c, no_l, SpireMechRunEnd::BASE_RETURN);
        spire_mech_run_end(no_c, no_l, SpireMechRunEnd::RUN_AGAIN);
        spire_mech_run_end(no_c, no_l, SpireMechRunEnd::GAME_EXIT);
        return hook_calls;
      },
      write, "", "E1L1E2L2E3L3", "E1O1L1E2O2L2E3O3L3", "E1E2E3");

  // H10 run_state, the hook alone: the four moments, each registrant with a key of its own carried in the sections
  auto state_call = [&]() {
    SpireSavestate ss;
    ss.run_id = 1000;
    spire_mech_run_state_capture(no_c, no_l, ss);
    auto json = phosg::JSON::dict();
    spire_mech_run_state_write(ss, json);
    SpireSavestate back;
    spire_mech_run_state_read(json, back);
    spire_mech_run_state_resume(no_c, no_l, back);
    std::string keys;
    for (const char* key : {"TestE", "TestO", "TestL"}) {
      if (json.contains(key)) {
        keys += std::format(" {}={}", key, json.get_int(key));
      }
    }
    return hook_calls + keys;
  };
  expect_hook<SpireMechRunState>(
      H::RUN_STATE, spire_mech_register_run_state, T_RUN_STATE<'L'>, T_RUN_STATE<'E'>, T_RUN_STATE<'O'>, state_call, write,
      "", std::format("EcLcEwLwErLrEs1Ls1 TestE={} TestL={}", 1000 + 'E', 1000 + 'L'),
      std::format("EcOcLcEwOwLwErOrLrEs1Os1Ls1 TestE={} TestO={} TestL={}", 1000 + 'E', 1000 + 'O', 1000 + 'L'),
      std::format("EcEwErEs1 TestE={}", 1000 + 'E'));

  // H11 char_meta, the hook alone: read and write, a key of its own carried in the sections
  auto meta_call = [&]() {
    auto json = phosg::JSON::dict({{"TestE", 5}, {"TestO", 6}, {"TestL", 7}});
    SpireMetaFile meta;
    spire_mech_char_meta_read(json, meta);
    auto out = phosg::JSON::dict();
    spire_mech_char_meta_write(meta, out);
    std::string keys;
    for (const char* key : {"TestE", "TestO", "TestL"}) {
      if (out.contains(key)) {
        keys += std::format(" {}={}", key, out.get_int(key));
      }
    }
    return hook_calls + keys;
  };
  expect_hook<SpireMechCharMeta>(
      H::CHAR_META, spire_mech_register_char_meta, T_CHAR_META<'L'>, T_CHAR_META<'E'>, T_CHAR_META<'O'>, meta_call, write,
      "", "ErLrEwLw TestE=5 TestL=7", "ErOrLrEwOwLw TestE=5 TestO=6 TestL=7", "ErEw TestE=5");

  // The run mag's ledger is in the savestate whatever the switches say: its registration has no switch
  {
    SpireSavestate with_mag;
    with_mag.exists = true;
    with_mag.run_id = 78;
    with_mag.values.assign(SPIRE_SAVESTATE_NUM_VALUES, 0);
    SpireRunMagLedger led;
    led.run_id = 78;
    led.issued_step = 2;
    with_mag.run_mag = led;
    write("{}");
    const std::string on = spire_savestate_to_json(with_mag, 1);
    expect(on.find("\"RunMag\"") != std::string::npos);
    write(R"({"mags": false})");
    expect_eq(on, spire_savestate_to_json(with_mag, 1));
    auto back = spire_savestate_from_json(on);
    expect(back.run_mag.has_value());
    expect_eq(2, back.run_mag->issued_step);
    write("{}");
  }

  // H10 through the core: a registrant's key is written by spire_savestate_to_json and read by
  // spire_savestate_from_json; without the registrant the text is the one of before and the key is not read
  SpireSavestate ss;
  ss.exists = true;
  ss.run_id = 77;
  ss.values.assign(SPIRE_SAVESTATE_NUM_VALUES, 0);
  ss.sections["TestE"] = std::make_shared<const int64_t>(5);
  write("{}");
  const std::string ss_plain = spire_savestate_to_json(ss, 1);
  expect(ss_plain.find("TestE") == std::string::npos);
  std::string ss_with;
  {
    HookGuard g{H::RUN_STATE, "zz"};
    spire_mech_register_run_state({"zz", HOOK_KEY_ON, 99, T_RUN_STATE<'E'>});
    hook_calls.clear();
    ss_with = spire_savestate_to_json(ss, 1);
    expect_eq(std::string("Ew"), hook_calls);
    expect(ss_with.find("\"TestE\": 5") != std::string::npos);
    hook_calls.clear();
    auto back = spire_savestate_from_json(ss_with);
    expect_eq(std::string("Er"), hook_calls);
    expect_eq(1u, back.sections.count("TestE"));
    expect_eq(ss_with, spire_savestate_to_json(back, 1));
    write(R"({"test.haak.aan": false})"); // its switch off: not written, not read
    hook_calls.clear();
    expect_eq(ss_plain, spire_savestate_to_json(ss, 1));
    expect_eq(0u, spire_savestate_from_json(ss_with).sections.count("TestE"));
    expect_eq(std::string(""), hook_calls);
    write("{}");
  }
  expect_eq(ss_plain, spire_savestate_to_json(ss, 1));
  expect_eq(0u, spire_savestate_from_json(ss_with).sections.count("TestE"));
  expect_eq(ss_plain, spire_savestate_to_json(spire_savestate_from_json(ss_plain), 1));

  // H11 through the core: a registrant's key survives the file's load and save (every flag write loads the file first);
  // without the registrant the file is the one of before
  std::string meta_file = (dir / "meta.json").string();
  SpireMetaFile meta;
  meta.exists = true;
  meta.flags[std::make_pair(static_cast<uint8_t>(1), SPIRE_BRIDGE_FIRST_FLAG)] = true;
  meta.upgrades["hp_mat"] = 2;
  spire_meta_save_file(meta_file, 1, meta);
  const std::string meta_plain = phosg::load_file(meta_file);
  {
    HookGuard g{H::CHAR_META, "zz"};
    spire_mech_register_char_meta({"zz", HOOK_KEY_ON, 99, T_CHAR_META<'E'>});
    auto with = meta;
    with.sections["TestE"] = std::make_shared<const int64_t>(9);
    hook_calls.clear();
    spire_meta_save_file(meta_file, 1, with);
    expect_eq(std::string("Ew"), hook_calls);
    hook_calls.clear();
    auto loaded = spire_meta_load_file(meta_file);
    expect_eq(std::string("Er"), hook_calls);
    expect_eq(1u, loaded.sections.count("TestE"));
    expect(loaded.flags == meta.flags);
    expect(loaded.upgrades == meta.upgrades);
    const std::string meta_with = phosg::load_file(meta_file);
    spire_meta_save_file(meta_file, 1, loaded); // the load and save of a flag write
    expect_eq(meta_with, phosg::load_file(meta_file));
    expect_eq(9, *std::static_pointer_cast<const int64_t>(spire_meta_load_file(meta_file).sections.at("TestE")));
  }
  expect_eq(0u, spire_meta_load_file(meta_file).sections.count("TestE"));
  spire_meta_save_file(meta_file, 1, meta);
  expect_eq(meta_plain, phosg::load_file(meta_file));

  spire_mech_set_switches_file("");
  std::filesystem::remove_all(dir);
  expect(spire_mech_hook_conflicts().empty());
}

void spire_tests_mechanics() {
  auto rewards = spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE));
  auto first = [](uint32_t) -> uint32_t { return 0; };
  auto offer_of = [&](SpireRewardKind kind) {
    return spire_reward_offer(rewards, kind, 0, nullptr, 3, SpireRewardTier::RARE, first);
  };
  using K = SpireRewardKind;

  // The table: the kinds 1-5 are registered, each under its owner's switch; 6-8 are known numbers nobody registered;
  // the two points before a kind (4: the android route, 5: power first) are empty
  expect(spire_mech_kind_conflicts().empty());
  const std::vector<std::pair<K, std::string>> keys = {{K::WEAPON, "aanval"}, {K::NEW_LINE, "aanval"}, {K::MAG_FOOD, "mags"},
      {K::DISK, "force.schijf"}, {K::UNIT, "verdediging.units"}};
  for (const auto& [kind, key] : keys) {
    const auto* reg = spire_mech_kind(kind);
    expect(reg != nullptr);
    expect_eq(key, std::string(reg->switch_key));
    expect(!reg->default_off);
    expect(reg->offer && reg->pick);
    expect_eq(kind != K::MAG_FOOD, reg->result != nullptr); // mag food leaves no pick in flight
  }
  expect_eq(5u, spire_mech_kinds().size());
  for (int64_t k = 1; k <= 8; k++) {
    expect(spire_reward_kind_known(k));
    expect(!spire_mech_kind(static_cast<K>(k), true));
  }
  expect(!spire_reward_kind_known(0) && !spire_reward_kind_known(9));
  for (K kind : {K::FOCUS, K::MANA, K::ARMOR}) {
    expect(!spire_mech_kind(kind));
    // a known kind nobody registered gives no offer: the entry's own item
    expect_eq(std::format("reward kind {} is not registered", static_cast<uint32_t>(kind)), no_offer([&]() { offer_of(kind); }));
  }

  // The switches, read from a file of this test
  auto dir = std::filesystem::temp_directory_path() / std::format("newserv-static-spire-mechanics-{}", getpid());
  std::filesystem::create_directories(dir);
  std::string file = (dir / "spire-schakelaars.json").string();
  uint32_t writes = 0;
  auto write = [&](const std::string& text) {
    phosg::save_file(file, text);
    // a time of its own for every write: two writes within one tick of the file system's clock would read as one
    std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(++writes));
  };
  spire_mech_set_switches_file(file);

  // without a switches file everything is on, except what its asker names "default off"
  expect(spire_mech_enabled("aanval") && spire_mech_enabled("mags") && spire_mech_enabled("no such key"));
  expect(!spire_mech_enabled("verdediging.vallen", true));
  const std::vector<K> plain = {K::WEAPON, K::NEW_LINE, K::MAG_FOOD, K::UNIT}; // DISK needs the run copy's state
  std::vector<std::string> before;
  for (K kind : plain) {
    before.emplace_back(offer_text(offer_of(kind)));
  }
  auto expect_as_before = [&](size_t z) { expect_eq(before[z], offer_text(offer_of(plain[z]))); };
  const std::string disk_on = "a disk offer without the run copy's techniques"; // the disk's own code was reached
  expect_eq(disk_on, no_offer([&]() { offer_of(K::DISK); }));

  // a kind with its switch off gives no offer (the entry's own item); the others give what they gave
  write(R"({"aanval": false, "verdediging.vallen": true})");
  expect(!spire_mech_enabled("aanval") && spire_mech_enabled("mags"));
  expect(spire_mech_enabled("verdediging.vallen", true));
  expect_eq(std::string("reward kind 1 is switched off (aanval)"), no_offer([&]() { offer_of(K::WEAPON); }));
  expect_eq(std::string("reward kind 2 is switched off (aanval)"), no_offer([&]() { offer_of(K::NEW_LINE); }));
  expect_as_before(2);
  expect_as_before(3);
  expect_eq(disk_on, no_offer([&]() { offer_of(K::DISK); }));

  // the file is read again when it changes: a key that is gone is on again, and every kind has its own key
  write(R"({"mags": false, "force.schijf": false, "verdediging.units": false})");
  expect(spire_mech_enabled("aanval") && !spire_mech_enabled("mags"));
  expect_as_before(0); // with its switch on again a kind gives the offer it gave before
  expect_as_before(1);
  expect_eq(std::string("reward kind 3 is switched off (mags)"), no_offer([&]() { offer_of(K::MAG_FOOD); }));
  expect_eq(std::string("reward kind 4 is switched off (force.schijf)"), no_offer([&]() { offer_of(K::DISK); }));
  expect_eq(std::string("reward kind 5 is switched off (verdediging.units)"), no_offer([&]() { offer_of(K::UNIT); }));

  // an unreadable file leaves the last good state standing: no JSON, no object, a value that is no boolean
  for (const char* bad : {"{ this is no JSON", R"(["mags"])", R"({"aanval": false, "mags": "nee"})"}) {
    write(bad);
    expect(spire_mech_enabled("aanval") && !spire_mech_enabled("mags") && !spire_mech_enabled("verdediging.units"));
    expect_as_before(0);
    expect_eq(std::string("reward kind 3 is switched off (mags)"), no_offer([&]() { offer_of(K::MAG_FOOD); }));
  }

  // readable again: the new state
  write(R"({"mags": true})");
  expect(spire_mech_enabled("mags") && spire_mech_enabled("verdediging.units"));
  for (size_t z = 0; z < plain.size(); z++) {
    expect_as_before(z);
  }
  expect_eq(disk_on, no_offer([&]() { offer_of(K::DISK); }));

  // the file gone: everything on again
  write(R"({"aanval": false})");
  expect(!spire_mech_enabled("aanval"));
  std::filesystem::remove(file);
  expect(spire_mech_enabled("aanval"));
  for (size_t z = 0; z < plain.size(); z++) {
    expect_as_before(z);
  }

  // A new kind, from this file alone: its own section in the table, its offer from what the core hands it, "default
  // off" until its key is true, and a route that is asked before it
  spire_mech_register_kind({TEST_KIND, TEST_KEY, true, test_parse, test_offer, nullptr, nullptr});
  spire_mech_register_kind_first({TEST_KIND, TEST_FIRST_KEY, false, nullptr, test_first_offer, nullptr, nullptr});
  expect(spire_mech_kind_conflicts().empty());
  auto grid = spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE));
  expect_eq(1u, grid.sections.count(TEST_KEY));
  auto test_offer_of = [&](uint8_t char_class) {
    return spire_reward_offer(grid, TEST_KIND, char_class, nullptr, 3, SpireRewardTier::RARE, first);
  };
  expect_eq(std::string("reward kind 250 is switched off (test.naad)"), no_offer([&]() { test_offer_of(0); }));
  auto o = test_offer_of(2); // the route before the kind has its own switch, and it is on
  expect(o.first_route);
  expect_eq(std::string("RARE first"), o.label);
  write(R"({"test.naad": true})");
  o = test_offer_of(0); // not the route's: the kind itself
  expect(!o.first_route);
  expect_eq(std::string("RARE test"), o.label);
  expect_eq(rewards.mag_food[1], o.food_levels);
  expect(test_offer_of(2).first_route);
  write(R"({"test.naad": true, "test.naad.eerst": false})");
  o = test_offer_of(2); // the route off: the kind itself
  expect(!o.first_route);
  expect_eq(std::string("RARE test"), o.label);
  spire_mech_register_kind({TEST_KIND, TEST_KEY, false, nullptr, nullptr, nullptr, nullptr});
  expect_eq(1u, spire_mech_kind_conflicts().size()); // a second registration of a kind is named, the first stays
  expect(spire_mech_kind(TEST_KIND)->offer == test_offer);

  // leave nothing behind: the tests after this one see the table and the switches as the server has them
  spire_mech_forget_kind(TEST_KIND);
  spire_mech_forget_kind(TEST_KIND, true);
  expect(!spire_mech_kind(TEST_KIND) && !spire_mech_kind(TEST_KIND, true));
  expect_eq(5u, spire_mech_kinds().size());
  expect(spire_mech_kind_conflicts().empty());
  expect_eq(0u, spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE)).sections.count(TEST_KEY));
  spire_mech_set_switches_file("");
  std::filesystem::remove_all(dir);
  expect(spire_mech_enabled("aanval"));

  spire_tests_hooks();
}

[[maybe_unused]] const bool registered = spire_mech_register_tests("mechanics", spire_tests_mechanics);

} // namespace
