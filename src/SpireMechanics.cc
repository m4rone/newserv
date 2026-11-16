#include "SpireMechanics.hh"

#include "ReceiveSubcommands.hh"

#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>
#include <phosg/Strings.hh>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {

struct TestRegistry {
  std::map<std::string, SpireMechTestFunction> by_name; // ordered: the order of the names is the order of the run
  std::vector<std::string> registered_twice;
};

// A static inside a function: built at the first registration, whichever translation unit starts first
TestRegistry& test_registry() {
  static TestRegistry registry;
  return registry;
}

// The switches as last read well. Read again only when the file's time changes, as the reward grid is
// (SpireFlagBridge.cc spire_rewards_table_cached), and the file's time is asked once per SPIRE_SWITCHES_LOOK_USECS.
struct SwitchState {
  std::string path = SPIRE_SWITCHES_FILE;
  bool looked = false; // the file was looked at, at looked_usecs
  uint64_t looked_usecs = 0;
  std::map<std::string, bool> values; // the last good state; empty without a file
  bool loaded = false;
  std::filesystem::file_time_type loaded_time{};
  bool refused = false; // the file of refused_time was unreadable: logged once, not read again until it changes
  std::filesystem::file_time_type refused_time{};
};

SwitchState& switch_state() {
  static SwitchState state;
  return state;
}

uint64_t (*switch_clock)() = nullptr; // tests only; nullptr: the steady clock

uint64_t switch_now_usecs() {
  if (switch_clock) {
    return switch_clock();
  }
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

const std::map<std::string, bool>& switches() {
  auto& st = switch_state();
  uint64_t now = switch_now_usecs();
  if (st.looked && ((now - st.looked_usecs) < SPIRE_SWITCHES_LOOK_USECS)) {
    return st.values; // within the second: the state of the last look
  }
  st.looked = true;
  st.looked_usecs = now;
  std::filesystem::file_time_type t;
  try {
    if (!std::filesystem::is_regular_file(st.path)) {
      st.values.clear(); // no file: everything at its default
      st.loaded = false;
      st.refused = false;
      return st.values;
    }
    t = std::filesystem::last_write_time(st.path);
  } catch (const std::exception&) {
    return st.values;
  }
  if ((st.loaded && (t == st.loaded_time)) || (st.refused && (t == st.refused_time))) {
    return st.values;
  }
  try {
    auto json = phosg::JSON::parse(phosg::load_file(st.path));
    std::map<std::string, bool> values;
    for (const auto& [key, value] : json.as_dict()) {
      values.emplace(key, value->as_bool());
    }
    st.values = std::move(values);
    st.loaded = true;
    st.loaded_time = t;
    st.refused = false;
  } catch (const std::exception& e) {
    st.refused = true;
    st.refused_time = t;
    phosg::log_warning_f("Spire switches: {} is unreadable ({}); the last good state stays", st.path, e.what());
  }
  return st.values;
}

struct KindRegistry {
  std::map<std::pair<uint8_t, bool>, SpireMechKind> by_kind; // (kind number, not the first route): the route sorts first
  std::vector<std::string> conflicts;
};

KindRegistry& kind_registry() {
  static KindRegistry registry;
  return registry;
}

std::string conflict_name(SpireRewardKind kind, bool first) {
  return std::format("kind {}{}", static_cast<uint32_t>(kind), first ? " (first route)" : "");
}

bool register_kind(const SpireMechKind& reg, bool first) {
  auto& registry = kind_registry();
  if (!registry.by_kind.emplace(std::make_pair(static_cast<uint8_t>(reg.kind), !first), reg).second) {
    registry.conflicts.emplace_back(conflict_name(reg.kind, first));
  }
  return true;
}

// One hook's registrations, in the order of the calls: by number, then by name. The function is kept behind a pointer
// without a type; only the hook's own code below gives it its type back.
struct HookEntry {
  std::string name;
  std::string switch_key;
  uint32_t order = 0;
  bool default_off = false;
  std::shared_ptr<const void> fn;
};

struct HookRegistry {
  std::map<SpireMechHookId, std::vector<HookEntry>> by_hook;
  std::vector<std::string> conflicts;
};

// A static inside a function, as the other registries: built at the first registration
HookRegistry& hook_registry() {
  static HookRegistry registry;
  return registry;
}

const char* hook_name(SpireMechHookId hook) {
  static const char* names[12] = {"", "quest_bin", "reward_room", "type_weight", "player_hp", "player_died", "tech_done",
      "enemy_killed", "wave_start", "run_end", "run_state", "char_meta"};
  size_t z = static_cast<size_t>(hook);
  return (z < 12) ? names[z] : "";
}

std::string hook_conflict_name(SpireMechHookId hook, const std::string& name) {
  return std::format("H{} {}: {}", static_cast<uint32_t>(hook), hook_name(hook), name);
}

template <typename FnT>
bool register_hook(SpireMechHookId hook, const SpireMechHook<FnT>& reg) {
  auto& registry = hook_registry();
  auto& entries = registry.by_hook[hook];
  HookEntry entry{reg.name, reg.switch_key, reg.order, reg.default_off, std::make_shared<const FnT>(reg.fn)};
  for (const auto& e : entries) {
    if (e.name == entry.name) {
      registry.conflicts.emplace_back(hook_conflict_name(hook, entry.name));
      return true;
    }
  }
  auto before = [](const HookEntry& a, const HookEntry& b) {
    return (a.order != b.order) ? (a.order < b.order) : (a.name < b.name);
  };
  entries.insert(std::upper_bound(entries.begin(), entries.end(), entry, before), std::move(entry));
  return true;
}

// Calls call(fn) for every registration of the hook whose switch is on, in the hook's order. guarded: a throw of one
// registrant is logged and the next one is called (the hook's moment is not a file being read or written).
template <typename FnT, typename CallT>
void each_hook(SpireMechHookId hook, bool guarded, CallT&& call) {
  const auto& by_hook = hook_registry().by_hook;
  auto it = by_hook.find(hook);
  if ((it == by_hook.end()) || it->second.empty()) {
    return;
  }
  const auto entries = it->second; // a copy: a registrant never changes the list under the calls
  for (const auto& e : entries) {
    if (!e.switch_key.empty() && !spire_mech_enabled(e.switch_key, e.default_off)) {
      continue;
    }
    const FnT& fn = *static_cast<const FnT*>(e.fn.get());
    if (!guarded) {
      call(fn);
      continue;
    }
    try {
      call(fn);
    } catch (const std::exception& ex) {
      phosg::log_warning_f("Spire hook {}: {} failed ({}); not called for this moment",
          hook_conflict_name(hook, e.name), e.name, ex.what());
    }
  }
}

// Whether any registration of the hook would be called now
bool hook_in_force(SpireMechHookId hook) {
  const auto& by_hook = hook_registry().by_hook;
  auto it = by_hook.find(hook);
  if (it == by_hook.end()) {
    return false;
  }
  for (const auto& e : it->second) {
    if (e.switch_key.empty() || spire_mech_enabled(e.switch_key, e.default_off)) {
      return true;
    }
  }
  return false;
}

} // namespace

bool spire_mech_enabled(const std::string& key, bool default_off) {
  const auto& values = switches();
  auto it = values.find(key);
  return (it != values.end()) ? it->second : !default_off;
}

void spire_mech_set_switches_file(const std::string& path) {
  auto& st = switch_state();
  st = SwitchState{};
  if (!path.empty()) {
    st.path = path;
  }
}

void spire_mech_set_switches_clock(uint64_t (*now_usecs)()) {
  switch_clock = now_usecs;
  switch_state().looked = false;
}

bool spire_mech_register_kind(const SpireMechKind& reg) {
  return register_kind(reg, false);
}

bool spire_mech_register_kind_first(const SpireMechKind& reg) {
  return register_kind(reg, true);
}

const SpireMechKind* spire_mech_kind(SpireRewardKind kind, bool first) {
  const auto& registry = kind_registry();
  auto it = registry.by_kind.find(std::make_pair(static_cast<uint8_t>(kind), !first));
  return (it != registry.by_kind.end()) ? &it->second : nullptr;
}

std::vector<const SpireMechKind*> spire_mech_kinds() {
  std::vector<const SpireMechKind*> ret;
  for (const auto& [key, reg] : kind_registry().by_kind) {
    ret.emplace_back(&reg);
  }
  return ret;
}

std::vector<std::string> spire_mech_kind_conflicts() {
  return kind_registry().conflicts;
}

void spire_mech_forget_kind(SpireRewardKind kind, bool first) {
  auto& registry = kind_registry();
  registry.by_kind.erase(std::make_pair(static_cast<uint8_t>(kind), !first));
  std::erase(registry.conflicts, conflict_name(kind, first));
}

bool spire_mech_register_tests(const char* name, SpireMechTestFunction fn) {
  auto& registry = test_registry();
  if (!registry.by_name.emplace(name, fn).second) {
    registry.registered_twice.emplace_back(name);
  }
  return true;
}

std::vector<std::string> spire_mech_test_names() {
  std::vector<std::string> names;
  for (const auto& [name, fn] : test_registry().by_name) {
    names.emplace_back(name);
  }
  return names;
}

void spire_mech_run_tests() {
  const auto& registry = test_registry();
  if (!registry.registered_twice.empty()) {
    throw std::logic_error(std::format("Spire test name registered twice: {}", registry.registered_twice.front()));
  }
  if (registry.by_name.empty()) {
    throw std::logic_error("no Spire test file is registered");
  }
  for (const auto& [name, fn] : registry.by_name) {
    phosg::log_info_f("-- Spire test file: {}", name);
    fn();
  }
}

std::vector<std::string> spire_mech_hook_names(SpireMechHookId hook) {
  std::vector<std::string> names;
  const auto& by_hook = hook_registry().by_hook;
  auto it = by_hook.find(hook);
  if (it != by_hook.end()) {
    for (const auto& e : it->second) {
      names.emplace_back(e.name);
    }
  }
  return names;
}

std::vector<std::string> spire_mech_hook_conflicts() {
  return hook_registry().conflicts;
}

void spire_mech_forget_hook(SpireMechHookId hook, const char* name) {
  auto& registry = hook_registry();
  auto it = registry.by_hook.find(hook);
  if (it != registry.by_hook.end()) {
    std::erase_if(it->second, [&](const HookEntry& e) { return e.name == name; });
  }
  std::erase(registry.conflicts, hook_conflict_name(hook, name));
}

using HookId = SpireMechHookId;

// H1
bool spire_mech_register_quest_bin(const SpireMechHook<SpireMechQuestBinFn>& reg) {
  return register_hook(HookId::QUEST_BIN, reg);
}

std::shared_ptr<const std::string> spire_mech_quest_bin(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l,
    std::shared_ptr<const std::string> bin) {
  each_hook<SpireMechQuestBinFn>(HookId::QUEST_BIN, true, [&](SpireMechQuestBinFn fn) {
    auto next = fn(c, l, bin);
    if (next) {
      bin = std::move(next);
    }
  });
  return bin;
}

// H2
bool spire_mech_register_reward_room(const SpireMechHook<SpireMechRewardRoomFn>& reg) {
  return register_hook(HookId::REWARD_ROOM, reg);
}

void spire_mech_reward_room(const SpireMechRewardRoom& at, uint32_t& room_index, uint32_t& trede) {
  each_hook<SpireMechRewardRoomFn>(HookId::REWARD_ROOM, true, [&](SpireMechRewardRoomFn fn) {
    uint32_t room_index_before = room_index, trede_before = trede;
    try {
      fn(at, room_index, trede);
    } catch (...) { // what a failed registrant left half changed does not count
      room_index = room_index_before;
      trede = trede_before;
      throw;
    }
  });
}

// H3
bool spire_mech_register_type_weight(const SpireMechHook<SpireMechTypeWeightFn>& reg) {
  return register_hook(HookId::TYPE_WEIGHT, reg);
}

size_t spire_mech_type_weight_pick(size_t n, const std::function<uint32_t(uint32_t)>& rnd,
    const std::function<SpireMechTypeWeight(size_t)>& candidate) {
  if (!hook_in_force(HookId::TYPE_WEIGHT)) {
    return rnd(n) % n; // the draw of before the seam, and the only one
  }
  // A weight above this is this: n is small, and the sum stays far inside the 32 bits rnd takes
  constexpr uint64_t MAX_WEIGHT = 0x10000;
  std::vector<uint32_t> weights(n, 1);
  uint64_t total = 0;
  for (size_t z = 0; z < n; z++) {
    auto tw = candidate(z);
    uint64_t weight = 1;
    each_hook<SpireMechTypeWeightFn>(HookId::TYPE_WEIGHT, true, [&](SpireMechTypeWeightFn fn) {
      weight = std::min<uint64_t>(weight * fn(tw), MAX_WEIGHT);
    });
    weights[z] = static_cast<uint32_t>(weight);
    total += weight;
  }
  if (!total || (total > 0xFFFFFFFF)) {
    return rnd(n) % n;
  }
  uint64_t roll = rnd(static_cast<uint32_t>(total)) % total;
  for (size_t z = 0; z < n; z++) {
    if (roll < weights[z]) {
      return z;
    }
    roll -= weights[z];
  }
  return n - 1; // not reached: roll < total
}

// H4
bool spire_mech_register_player_hp(const SpireMechHook<SpireMechPlayerHPFn>& reg) {
  return register_hook(HookId::PLAYER_HP, reg);
}

void spire_mech_player_hp(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t subcommand) {
  if ((subcommand != 0x4B) && (subcommand != 0x4C)) {
    return; // 6x4A (a fully shielded attack) shares the handler and is no hit
  }
  each_hook<SpireMechPlayerHPFn>(HookId::PLAYER_HP, true, [&](SpireMechPlayerHPFn fn) { fn(c, l, subcommand); });
}

// H5
bool spire_mech_register_player_died(const SpireMechHook<SpireMechPlayerDiedFn>& reg) {
  return register_hook(HookId::PLAYER_DIED, reg);
}

void spire_mech_player_died(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l) {
  each_hook<SpireMechPlayerDiedFn>(HookId::PLAYER_DIED, true, [&](SpireMechPlayerDiedFn fn) { fn(c, l); });
}

// H6
bool spire_mech_register_tech_done(const SpireMechHook<SpireMechTechDoneFn>& reg) {
  return register_hook(HookId::TECH_DONE, reg);
}

void spire_mech_tech_done(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint32_t technique, uint32_t level) {
  each_hook<SpireMechTechDoneFn>(HookId::TECH_DONE, true, [&](SpireMechTechDoneFn fn) { fn(c, l, technique, level); });
}

// H7
bool spire_mech_register_enemy_killed(const SpireMechHook<SpireMechEnemyKilledFn>& reg) {
  return register_hook(HookId::ENEMY_KILLED, reg);
}

void spire_mech_enemy_killed(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, const DropReconcileResult& rec,
    uint8_t floor) {
  if (!rec.target_ene_st) {
    return; // the drop request of a box
  }
  each_hook<SpireMechEnemyKilledFn>(HookId::ENEMY_KILLED, true, [&](SpireMechEnemyKilledFn fn) { fn(c, l, rec, floor); });
}

// H8
bool spire_mech_register_wave_start(const SpireMechHook<SpireMechWaveStartFn>& reg) {
  return register_hook(HookId::WAVE_START, reg);
}

void spire_mech_wave_start(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number,
    uint32_t quest_number, uint32_t a, uint32_t b) {
  each_hook<SpireMechWaveStartFn>(HookId::WAVE_START, true,
      [&](SpireMechWaveStartFn fn) { fn(c, l, serial_number, quest_number, a, b); });
}

// H9
bool spire_mech_register_run_end(const SpireMechHook<SpireMechRunEndFn>& reg) {
  return register_hook(HookId::RUN_END, reg);
}

void spire_mech_run_end(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, SpireMechRunEnd why) {
  each_hook<SpireMechRunEndFn>(HookId::RUN_END, true, [&](SpireMechRunEndFn fn) { fn(c, l, why); });
}

// H10
bool spire_mech_register_run_state(const SpireMechHook<SpireMechRunState>& reg) {
  return register_hook(HookId::RUN_STATE, reg);
}

void spire_mech_run_state_capture(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, SpireSavestate& ss) {
  each_hook<SpireMechRunState>(HookId::RUN_STATE, true, [&](const SpireMechRunState& fns) {
    if (fns.capture) {
      fns.capture(c, l, ss);
    }
  });
}

void spire_mech_run_state_write(const SpireSavestate& ss, phosg::JSON& json) {
  each_hook<SpireMechRunState>(HookId::RUN_STATE, false, [&](const SpireMechRunState& fns) {
    if (fns.write) {
      fns.write(ss, json);
    }
  });
}

void spire_mech_run_state_read(const phosg::JSON& json, SpireSavestate& ret) {
  each_hook<SpireMechRunState>(HookId::RUN_STATE, false, [&](const SpireMechRunState& fns) {
    if (fns.read) {
      fns.read(json, ret);
    }
  });
}

void spire_mech_run_state_resume(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, const SpireSavestate& ss) {
  each_hook<SpireMechRunState>(HookId::RUN_STATE, true, [&](const SpireMechRunState& fns) {
    if (fns.resume) {
      fns.resume(c, l, ss);
    }
  });
}

// H11
bool spire_mech_register_char_meta(const SpireMechHook<SpireMechCharMeta>& reg) {
  return register_hook(HookId::CHAR_META, reg);
}

void spire_mech_char_meta_read(const phosg::JSON& json, SpireMetaFile& ret) {
  each_hook<SpireMechCharMeta>(HookId::CHAR_META, false, [&](const SpireMechCharMeta& fns) {
    if (fns.read) {
      fns.read(json, ret);
    }
  });
}

void spire_mech_char_meta_write(const SpireMetaFile& file, phosg::JSON& json) {
  each_hook<SpireMechCharMeta>(HookId::CHAR_META, false, [&](const SpireMechCharMeta& fns) {
    if (fns.write) {
      fns.write(file, json);
    }
  });
}
