#pragma once

#include <stdint.h>

#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

// Spire reward draw (v20, server side). The same draw the quest's rewards.py hook draw_rewards runs on the client, on
// the server: tag weighting, rarity, curse code, the 8 + 8 attempts in the sub-pool and the whole band, then the first
// valid entry in table order, with caps, duplicates, vanaf_k, tot_k and the android filter. The board, the pick dialog and
// item_create2 stay in the quest. The table comes from the quest build (build/spire-draw.json, written by rewards.py from
// data/rewards.json), so rewards.json stays the only source of the pool.

struct SpireDrawEntry {
  uint8_t cap = 2;
  uint8_t vanaf_k = 1;
  uint8_t tot_k = 0xFF; // last room k it is drawn in (rewards.json tot_k: an act-3 entry replaces it), 0xFF = all
  bool android_ok = true;
  bool rare = false; // z == 'R'
  bool own_curse = false; // vloek: never a drawn curse on top
  bool glass_ok = false; // weapon or tech disk: the only entries a Glass curse may ride on
};

struct SpireDrawArchetype {
  std::vector<SpireDrawEntry> entries;
  std::vector<uint8_t> kbands; // ascending, starts at 1
  // per k band: 3 * num_tags + 1 rows of member lists (pool indexes, repeated by weight); the last row is the band
  std::map<uint8_t, std::vector<std::vector<uint8_t>>> rows;
  std::vector<uint8_t> slot_c_zero_tags; // weight 0 in the variety slot
};

struct SpireDrawTable {
  uint8_t num_tags = 0;
  uint32_t rare_base = 5;
  uint32_t rare_per_k = 3;
  uint32_t uncommon_below = 40;
  uint32_t curse_pct = 10;
  uint32_t max_attempts = 8;
  uint32_t blood_pact_bonus = 15;
  uint32_t relic_bit_r3 = 0;
  uint32_t relic_bit_r13 = 0;
  uint32_t relic_bit_r14 = 0;
  std::vector<SpireDrawArchetype> archetypes; // HU, RA, FO

  static SpireDrawTable from_json_file(const std::string& filename);
};

// The quest's registers, in the order the quest sends them (SPIRE_DRAW_NUM_INPUTS values).
struct SpireDrawInput {
  uint32_t archetype = 0; // r156: 0 HU, 1 RA, 2 FO
  uint32_t race = 0; // 2 = android
  uint32_t k = 1;
  uint32_t path = 0; // r152: 1 = entered through an elite pipe (R15: rare in slot A)
  uint32_t choice = 0; // r130: 1 pipe A, 2 pipe B
  uint32_t tag_a = 0; // r132: tag index + 1, 0 none
  uint32_t tag_b = 0; // r133
  uint32_t lives = 0;
  uint32_t glass_item_id = 0; // r138
  uint32_t relics = 0; // r170
  uint32_t tags = 0; // r180: 3 bits per tag
  uint32_t state = 0; // r182: bits 8-15 rare bonus
  std::array<uint32_t, 4> picks = {}; // r183-r186: 3 bits per pool index, 10 per register
};
constexpr size_t SPIRE_DRAW_MAX_ENTRIES = 40; // 4 picks registers x 10 pool indexes
constexpr size_t SPIRE_DRAW_NUM_INPUTS = 16;
SpireDrawInput spire_draw_input_from_values(const std::vector<uint32_t>& values);

// Returns r181: entry id (pool index + 1) of board slot 2/3/4/5 at bits 0/6/12/18, curse code at bits 24/26/28/30. rnd(n)
// must return a uniform value in [0, n).
uint32_t spire_draw(const SpireDrawTable& table, const SpireDrawInput& in, const std::function<uint32_t(uint32_t)>& rnd);
