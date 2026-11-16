#pragma once

#include <string>
#include <vector>

// SpireMechanics: the seam between the Spire core and its mechanics (pso-roguelike
// new-features/roguelike/design/mechanics/integratie.md par. 3; this repo's docs/review-loop/goals/naad.md). A
// mechanic builds in files of its own (src/Spire*.cc, taken by a glob in CMakeLists.txt) and registers here; the core
// calls what was registered and names no mechanic. Everything in this file carries the prefix spire_mech_.
//
// Every list of registrations is a static inside a function: a file registers from a static initialiser of its own,
// and the order in which the translation units start does not matter.

// --- Tests ---
//
// A test file (src/SpireTests*.cc) registers one function under a name of its own, with one line:
//   [[maybe_unused]] const bool registered = spire_mech_register_tests("mags", spire_tests_mags);
// run_static_tests() (StaticTests.cc) calls spire_mech_run_tests() once and names no test file.

using SpireMechTestFunction = void (*)();

// Registers a test file's function under its name. Always returns true (so it can initialise a static). A name
// registered twice keeps the first function and makes spire_mech_run_tests throw: no test runs twice, none is hidden.
bool spire_mech_register_tests(const char* name, SpireMechTestFunction fn);

// Runs every registered function exactly once, in the order of the names, and logs one line per test file on stderr
// before it runs. Throws std::logic_error when nothing was registered or a name was registered twice.
void spire_mech_run_tests();

// The names registered, in the order spire_mech_run_tests runs them
std::vector<std::string> spire_mech_test_names();
