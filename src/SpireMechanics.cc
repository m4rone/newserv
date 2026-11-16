#include "SpireMechanics.hh"

#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>
#include <phosg/Strings.hh>

#include <filesystem>
#include <format>
#include <map>
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
// (SpireFlagBridge.cc spire_rewards_table_cached).
struct SwitchState {
  std::string path = SPIRE_SWITCHES_FILE;
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

const std::map<std::string, bool>& switches() {
  auto& st = switch_state();
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

bool register_kind(const SpireMechKind& reg, bool first) {
  auto& registry = kind_registry();
  if (!registry.by_kind.emplace(std::make_pair(static_cast<uint8_t>(reg.kind), !first), reg).second) {
    registry.conflicts.emplace_back(std::format("kind {}{}", static_cast<uint32_t>(reg.kind), first ? " (first route)" : ""));
  }
  return true;
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
  kind_registry().by_kind.erase(std::make_pair(static_cast<uint8_t>(kind), !first));
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
