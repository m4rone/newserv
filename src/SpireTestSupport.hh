#pragma once

#include <stdint.h>

#include <map>

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
