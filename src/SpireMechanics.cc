#include "SpireMechanics.hh"

#include <phosg/Strings.hh>

#include <format>
#include <map>
#include <stdexcept>

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

} // namespace

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
