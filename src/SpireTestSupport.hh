#pragma once

#include <stdint.h>

#include <map>
#include <string>

#include <phosg/JSON.hh>

#include "SpireRewards.hh"

// What a test that stayed in run_static_tests() (StaticTests.cc) and a test that moved to its owner's file
// (src/SpireTests*.cc) both need. One definition here, never a copy per file.

// The first room of every W the grid gives (rewards.room_w is room index -> W). It stood as a local of the rewards
// block in run_static_tests(); the grid test that stayed there and the per-cell weapon test in SpireTestsAttack.cc
// both read it.
inline std::map<uint8_t, uint32_t> spire_test_first_room_of_w(const SpireRewardTable& rewards) {
  std::map<uint8_t, uint32_t> first_room_of_w;
  for (const auto& [room, w] : rewards.room_w) {
    first_room_of_w.emplace(w, room);
  }
  return first_room_of_w;
}

// A JSON text on one line without spaces, as the reward grid was written before the seam. The generator writes the grid
// indented now; a test that edits the grid's text by position ("AtaModel":"safe", "RungWeaponSteps":[0,1,2,3], the
// first row of Disks) does so on this form, so what it tests does not hang on the file's layout.
inline std::string spire_test_compact_json(const std::string& text) {
  return phosg::JSON::parse(text).serialize();
}
