#pragma once

#include <stdint.h>

#include <memory>
#include <phosg/JSON.hh>
#include <string>

#include "EnemyType.hh"
#include "ItemData.hh"
#include "ItemParameterTable.hh"
#include "LevelTable.hh"

class Client;
class Lobby;

// Spire drops (regie pso-regie-9, B-E01-108; rule book items 25-30 of pso-roguelike
// new-features/roguelike/output/spelsysteem-randgevallen-2026-10-25.md). In a Spire game (spire_game_is_spire) the server
// makes the drops (SpireDrops.DropMode, default SERVER_PRIVATE: every player his own drops, items 29 and 34) and passes
// every item it made for a player through spire_drops_filter_item before it is sent:
// - item 30 (OnlyRunItems): only weapons, frames, barriers, mates, fluids, Sol/Moon/Star Atomizers and meseta drop; any
//   other item (materials, Telepipes, mags, Scape Dolls, units, tech disks, grinders, ...) is removed;
// - items 25-26: a weapon the player's class cannot equip, or whose ATP/ATA/MST requirement is above the stats he has
//   LevelsAhead levels from now (Sega's level table; 0 = only what he can equip now), becomes a weapon he can equip now;
// - item 27 (PityDrops): after that many kept drops in a row without a weapon he can equip now, the next drop is one;
// - item 28 (BossWeaponPerPlayer): a boss's drop is exactly one weapon each player can equip now (Sega's own drop when it
//   already is one, else a picked one), also when Sega's table gave nothing.
// A picked weapon is the best common (not rare) weapon of a random weapon group 01-0C (Saber ... Wand) whose
// requirements the player meets now, without grind, special or percentages (see spire_drops_pick_weapon).
// Without the SpireDrops key nothing changes (the client makes the drops, the quest's rule block still forbids Scape
// Dolls). In the client drop mode (or when the client never sends 6x60/6xA2) the server never sees a drop, so none of
// this applies and the run plays with Sega's client drops: it degrades to the old behaviour, never to a lost item.
struct SpireDropsConfig {
  bool enabled = false; // the SpireDrops key is present
  ServerDropMode drop_mode = ServerDropMode::SERVER_PRIVATE; // DropMode
  bool only_run_items = true; // OnlyRunItems (item 30)
  uint32_t levels_ahead = 5; // LevelsAhead (item 26: "hooguit een gebied vooruit"; 0 = only usable now)
  uint32_t pity_drops = 8; // PityDrops (item 27; 0 = off)
  bool boss_weapon_per_player = true; // BossWeaponPerPlayer (item 28)

  static SpireDropsConfig from_json(const phosg::JSON& json);
  std::string str() const;
};

// Bits of ItemParameterTable::Weapon::usability_flags that this class must find set
uint8_t spire_weapon_usability_bits(uint8_t char_class);
// Item 30: true for the kinds a run drops (weapon, frame, barrier, mate, fluid, Sol/Moon/Star Atomizer, meseta)
bool spire_drops_is_run_item(const ItemData& item);

struct SpireDropPlayer {
  uint8_t char_class = 0;
  CharacterStats now; // the player's stats now
  CharacterStats ahead; // his stats levels_ahead levels later (Sega's level table)
};
// The stats of a character of this class at level (0-based) + levels_ahead, from his stats now (level capped at 199)
CharacterStats spire_drops_stats_ahead(
    const LevelTable& level_table, uint8_t char_class, uint32_t level, const CharacterStats& now, uint32_t levels_ahead);

enum class SpireWeaponFit {
  NOT_A_WEAPON = 0,
  NOW, // class can equip it, requirements met now
  AHEAD, // class can equip it, requirements met only levels_ahead levels later
  NO, // class cannot equip it, or the requirements are above even the stats ahead
};
SpireWeaponFit spire_drops_weapon_fit(const ItemParameterTable& pmt, const ItemData& item, const SpireDropPlayer& p);
// The weapon to give: see above. random picks the group. If no common weapon fits the player now (Sega's level-1
// rangers), the common weapons of his class he lacks the least for. Empty only if his class can equip none.
ItemData spire_drops_pick_weapon(const ItemParameterTable& pmt, const SpireDropPlayer& p, uint32_t random);

// The decision for one item made for one player, free of state for the static tests. Changes item (cleared or
// replaced) and pity (kept drops in a row without a weapon usable now). Returns why the item changed, or nullptr if it
// was kept as it is.
const char* spire_drops_filter_item(const SpireDropsConfig& cfg, const ItemParameterTable& pmt, ItemData& item,
    const SpireDropPlayer& p, bool is_boss, uint32_t& pity, uint32_t random);

// Item 28 applies to the drop of this enemy: a boss (EnemyType BOSS flag), except the Vol Opt parts before its end.
// Vol Opt's monitors, pillars, amp, core and phase 1 carry the BOSS flag too, but only VOL_OPT_2 falling ends the fight
// (k11, plan-mines-volopt-2026-10-25 section 5), so only VOL_OPT_2 gives each player his boss weapon. Dragon, De Rol Le
// and every other boss are unchanged. Dark Falz (k15): DARK_FALZ_1 carries the flag but is not the end, so it gives no
// boss drop; the final phase (DARK_FALZ_2 on Normal, DARK_FALZ_3 above, the type newserv's flag route drops for) does.
// Darvants have no BOSS flag. Only the head of an enemy set gives the boss drop (child_index 0, the drop target after
// alias resolution): Gol Dragon's set expands to the head plus 5 children of type GOL_DRAGON, and Barba Ray's ends on
// a second BARBA_RAY child; each child has its own drop flag, so a drop request for one would otherwise be a second
// boss drop. Dark Falz and Olga Flow children alias their head, which keeps child_index 0 as the target.
bool spire_drops_is_boss_drop(EnemyType type, uint16_t child_index = 0);

// Server glue. At quest load: in a Spire game with SpireDrops, set the lobby's drop mode and forget every client's pity
// counter. Per item made for client c (the receiving player; the requester in SERVER_SHARED): apply the filter, log a
// change. Both no-ops outside a Spire game or without SpireDrops; the filter never throws (a missing table or
// character keeps the item as it is).
void spire_drops_quest_loaded(std::shared_ptr<Lobby> l, std::shared_ptr<const Client> leader_c);
bool spire_drops_filter(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, ItemData& item, bool is_boss);
